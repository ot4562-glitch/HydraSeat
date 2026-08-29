cmake_minimum_required(VERSION 3.20)
project(HydraSeat VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    string(TOLOWER "${MSVC_CXX_ARCHITECTURE_ID}" hydra_compiler_arch)
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" hydra_compiler_arch)
endif()
if(hydra_compiler_arch MATCHES "^(x86|i[3-6]86)$")
    set(HYDRA_GATE_C_ARCH_NAME x86)
elseif(hydra_compiler_arch MATCHES "^(x64|x86_64|amd64)$")
    set(HYDRA_GATE_C_ARCH_NAME x64)
else()
    message(FATAL_ERROR
        "Gate C supports only explicit x86 and x64 compiler targets; got '${hydra_compiler_arch}'")
endif()

set(HYDRA_GATE_C_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/gate-c" CACHE PATH
    "Deterministic root for architecture-specific Gate C artifacts")

function(hydra_set_gate_c_output target_name)
    set(runtime_dir "${HYDRA_GATE_C_OUTPUT_ROOT}/${HYDRA_GATE_C_ARCH_NAME}")
    set(archive_dir "${HYDRA_GATE_C_OUTPUT_ROOT}/lib/${HYDRA_GATE_C_ARCH_NAME}")
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${runtime_dir}"
        LIBRARY_OUTPUT_DIRECTORY "${runtime_dir}"
        ARCHIVE_OUTPUT_DIRECTORY "${archive_dir}"
    )
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${runtime_dir}"
            LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${runtime_dir}"
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${archive_dir}"
        )
    endforeach()
endfunction()

# Include directories
include_directories(include .)

# Enable UTF-8 encoding for MSVC string literals
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Windows system libraries for Win32 Raw Input, SetupAPI, DXGI, and Comctl32 GUI
if(WIN32)
    add_compile_definitions(_UNICODE UNICODE WIN32_LEAN_AND_MEAN)
    set(WIN32_LIBS User32.lib Cfgmgr32.lib Dxgi.lib Setupapi.lib Hid.lib Xinput.lib Comctl32.lib Comdlg32.lib)
endif()

# Find Qt6 components if available
find_package(Qt6 COMPONENTS Core Widgets QUIET)
find_package(Threads REQUIRED)

if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    set(UI_SOURCES
        ui/workspace_widget.cpp
        ui/main_window.cpp
    )
    set(UI_HEADERS
        ui/workspace_widget.hpp
        ui/main_window.hpp
    )
endif()

# Source files
set(HYDRA_HARDWARE_SOURCES
    src/hardware_detector.cpp
    src/raw_input_utils.cpp
)

set(HYDRA_SOURCES
    src/input_router.cpp
    src/input_isolation.cpp
    src/input_observation.cpp
    src/display_manager.cpp
    src/workspace_manager.cpp
    src/game_launcher.cpp
    src/gui_win32.cpp
    ${UI_SOURCES}
)
if(WIN32)
    list(APPEND HYDRA_SOURCES src/launcher_win32.cpp)
endif()

set(HYDRA_HARDWARE_HEADERS
    include/hydra/hardware_detector.hpp
    include/hydra/hardware_identity.hpp
    include/hydra/hid_usage.hpp
    include/hydra/raw_input_utils.hpp
)

set(HYDRA_HEADERS
    include/hydra/directinput_policy.hpp
    include/hydra/hidhide_probe.hpp
    include/hydra/input_metrics.hpp
    include/hydra/input_router.hpp
    include/hydra/input_isolation.hpp
    include/hydra/input_observation.hpp
    include/hydra/display_manager.hpp
    include/hydra/workspace_manager.hpp
    include/hydra/game_launcher.hpp
    include/hydra/gui_win32.hpp
    ${HYDRA_HARDWARE_HEADERS}
    ${UI_HEADERS}
)

add_library(hydra_hardware STATIC
    ${HYDRA_HARDWARE_SOURCES}
    ${HYDRA_HARDWARE_HEADERS}
)
if(WIN32)
    target_link_libraries(hydra_hardware PRIVATE User32.lib Cfgmgr32.lib Setupapi.lib Xinput.lib)
endif()

# P4-RUN-01 authoritative, headless production runtime state machine. The
# library is UI-independent and all mutable backends are injected explicitly.
add_library(hydra_runtime STATIC
    src/runtime_state.cpp
    src/runtime_host.cpp
    src/seat_game_lifecycle.cpp
    include/hydra/runtime_state.hpp
    include/hydra/runtime_host.hpp
    include/hydra/seat_game_lifecycle.hpp
)
target_link_libraries(hydra_runtime PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(hydra_runtime PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-IPC-01 portable fixed-width host protocol codec. Transport/authentication
# lives in a separate Windows-aware component so codec tests remain portable.
add_library(hydra_host_protocol STATIC
    src/host_protocol.cpp
    include/hydra/host_protocol.hpp
)
target_link_libraries(hydra_host_protocol PUBLIC hydra_runtime)
if(MSVC)
    target_compile_options(hydra_host_protocol PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_host_protocol PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_library(hydra_host_transport STATIC
    src/host_transport.cpp
    include/hydra/host_transport.hpp
)
target_link_libraries(hydra_host_transport PUBLIC hydra_host_protocol)
if(WIN32)
    target_link_libraries(hydra_host_transport PRIVATE Advapi32.lib Bcrypt.lib)
endif()
if(MSVC)
    target_compile_options(hydra_host_transport PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_host_transport PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-PROC-01 Seat-owned process trees. Windows Job Objects provide exact
# descendant containment and completion notifications; weaker capabilities are
# explicit and never grant ownership over unrelated processes.
add_library(hydra_process STATIC
    src/process_group.cpp
    src/process_launcher.cpp
    include/hydra/process_group.hpp
    include/hydra/process_launcher.hpp
)
target_link_libraries(hydra_process PUBLIC hydra_runtime Threads::Threads)
if(WIN32)
    target_link_libraries(hydra_process PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_process PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_process PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P5-LAUNCH-01 immutable exactly-two-Seat launch plan and Seat-local activation
# transaction. Production resource backends are injected behind a typed factory;
# the coordinator owns ordering, verification, reverse rollback, and isolation.
add_library(hydra_two_seat_launch STATIC
    src/two_seat_launch.cpp
    include/hydra/two_seat_launch.hpp
)
target_link_libraries(hydra_two_seat_launch PUBLIC hydra_process hydra_runtime)
if(MSVC)
    target_compile_options(hydra_two_seat_launch PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_two_seat_launch PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-WIN-01 process-identity-backed top-level window inventory. WinEvent
# callbacks only enqueue bounded hints; ownership/classification work stays on a
# dedicated message-pump worker and never adopts windows outside Seat groups.
add_library(hydra_window_tracking STATIC
    src/window_tracker.cpp
    include/hydra/window_identity.hpp
    include/hydra/window_tracker.hpp
)
target_link_libraries(hydra_window_tracking PUBLIC hydra_process Threads::Threads)
if(WIN32)
    target_link_libraries(hydra_window_tracking PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_window_tracking PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_window_tracking PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-DIS-01 read-only DisplayConfig + DXGI topology inventory. Stable target
# identity never relies on enumeration order and disconnected targets are kept.
add_library(hydra_display_topology STATIC
    src/display_topology.cpp
    include/hydra/display_identity.hpp
    include/hydra/display_topology.hpp
)
if(WIN32)
    target_link_libraries(hydra_display_topology PRIVATE User32.lib Dxgi.lib)
endif()
if(MSVC)
    target_compile_options(hydra_display_topology PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_display_topology PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-DIS-02 stable Seat display groups and reversible coordinate transforms.
add_library(hydra_seat_display_layout STATIC
    src/seat_display_layout.cpp
    include/hydra/seat_display_layout.hpp
)
target_link_libraries(hydra_seat_display_layout PUBLIC hydra_display_topology hydra_runtime)
if(MSVC)
    target_compile_options(hydra_seat_display_layout PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_seat_display_layout PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-WIN-02 typed placement planning and ownership-validated Win32 mutation.
# The policy surface is portable; the engine applies only to currently owned
# windows and captures rollback state before every mutation.
add_library(hydra_window_placement STATIC
    src/window_policy.cpp
    src/window_placement.cpp
    include/hydra/window_policy.hpp
    include/hydra/window_placement.hpp
)
target_link_libraries(hydra_window_placement PUBLIC hydra_window_tracking hydra_seat_display_layout)
if(WIN32)
    target_link_libraries(hydra_window_placement PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_window_placement PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_window_placement PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-POL-01 serialized desired-vs-observed Seat runtime policy coordinator.
add_library(hydra_seat_runtime_policy STATIC
    src/seat_runtime_policy.cpp
    include/hydra/seat_runtime_policy.hpp
)
target_link_libraries(hydra_seat_runtime_policy PUBLIC hydra_window_placement)
if(MSVC)
    target_compile_options(hydra_seat_runtime_policy PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_seat_runtime_policy PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-DIS-03 deterministic display hot-plug degradation and recovery planning.
add_library(hydra_display_recovery STATIC
    src/display_recovery.cpp
    include/hydra/display_recovery.hpp
)
target_link_libraries(hydra_display_recovery PUBLIC hydra_seat_display_layout hydra_seat_runtime_policy)
if(MSVC)
    target_compile_options(hydra_display_recovery PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_display_recovery PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(MSVC)
    target_compile_options(hydra_hardware PRIVATE /W4 /permissive-)
endif()

# P4-CTRL-01 Management Seat placement and global-control permission model.
add_library(hydra_management_seat STATIC
    src/management_seat.cpp
    include/hydra/management_seat.hpp
)
target_link_libraries(hydra_management_seat PUBLIC hydra_seat_display_layout)
if(WIN32)
    target_link_libraries(hydra_management_seat PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_management_seat PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_management_seat PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-CTRL-01 authoritative host/config-backed control-surface presentation model.
add_library(hydra_control_surface STATIC
    src/control_surface_model.cpp
    include/hydra/control_surface_model.hpp
)
target_link_libraries(hydra_control_surface PUBLIC hydra_management_seat hydra_runtime)
if(MSVC)
    target_compile_options(hydra_control_surface PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_control_surface PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P4-CTRL-02 verified Stop/Reconfigure/Save+Start control-surface state model.
add_library(hydra_session_control STATIC
    src/session_control_transition.cpp
    include/hydra/session_control_transition.hpp
)
target_link_libraries(hydra_session_control PUBLIC hydra_management_seat hydra_host_transport)
if(MSVC)
    target_compile_options(hydra_session_control PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_session_control PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-CTRL-01 bounded, process-local normalized XInput state. This component
# contains no physical polling and performs no ordinary XInput API hooking.
add_library(hydra_virtual_xinput STATIC
    src/virtual_xinput_state.cpp
    include/hydra/virtual_xinput_state.hpp
)
set_target_properties(hydra_virtual_xinput PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(hydra_virtual_xinput PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_virtual_xinput PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-CTRL-02 bounded, process-local DirectInput enumeration/visibility policy.
# The core is portable and owns no DirectInput COM object or physical device.
add_library(hydra_directinput_policy STATIC
    src/directinput_policy.cpp
    include/hydra/directinput_policy.hpp
)
set_target_properties(hydra_directinput_policy PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(hydra_directinput_policy PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_directinput_policy PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P5-CTRL-01 production controller source inventory/polling and Seat mapping.
# XInput slot numbers remain runtime-only hints; stable persistent identity is
# provided by capable backends such as DirectInput instance GUIDs (and a future
# optional GameInput provider). Polling never runs on the Raw Input callback.
add_library(hydra_controller_runtime STATIC
    src/controller_runtime.cpp
    src/controller_native.cpp
    include/hydra/controller_runtime.hpp
)
target_link_libraries(hydra_controller_runtime PUBLIC
    hydra_virtual_xinput hydra_directinput_policy Threads::Threads)
if(WIN32)
    target_link_libraries(hydra_controller_runtime PRIVATE
        Dinput8.lib Dxguid.lib Xinput.lib)
endif()
if(MSVC)
    target_compile_options(hydra_controller_runtime PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_controller_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-D-01 read-only HidHide availability probe. The portable core interprets
# bounded observations; the Windows path uses only service/interface/version
# queries and the documented GET_ACTIVE/GET_WLINVERSE IOCTLs.
add_library(hydra_hidhide_probe STATIC
    src/hidhide_probe.cpp
    include/hydra/hidhide_probe.hpp
)
if(WIN32)
    target_link_libraries(hydra_hidhide_probe PRIVATE
        Advapi32.lib Cfgmgr32.lib Version.lib)
endif()
if(MSVC)
    target_compile_options(hydra_hidhide_probe PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_hidhide_probe PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-D-02 guarded session-cloak transaction. The native Windows boundary uses
# HidHide's process-lifetime session blacklist; HydraSeat never appends session
# devices to the persistent blacklist. Activation is still runtime-gated by
# physical acceptance and explicit high-risk approval.
add_library(hydra_hidhide_session STATIC
    src/hidhide_session_backend.cpp
    src/hidhide_session_native.cpp
    include/hydra/hidhide_session_backend.hpp
)
target_link_libraries(hydra_hidhide_session PUBLIC hydra_hidhide_probe)
if(MSVC)
    target_compile_options(hydra_hidhide_session PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_hidhide_session PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-MET-01 fixed-capacity, process-local metrics recorder and deterministic
# off-path report generator. The recorder performs no I/O on the input path.
add_library(hydra_input_metrics STATIC
    src/input_metrics.cpp
    include/hydra/input_metrics.hpp
)
set_target_properties(hydra_input_metrics PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(hydra_input_metrics PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_input_metrics PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P5-MET-01 bounded privacy-safe integrated two-Seat evidence report. This
# aggregates existing input metrics plus Seat capability/timing outcomes and
# keeps physical-validation eligibility separate from controlled/synthetic pass.
add_library(hydra_session_metrics STATIC
    src/session_metrics.cpp
    include/hydra/session_metrics.hpp
)
target_link_libraries(hydra_session_metrics PUBLIC hydra_input_metrics)
if(MSVC)
    target_compile_options(hydra_session_metrics PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_session_metrics PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-SCHEMA-01 bounded versioned persistence/runtime schema family. This keeps
# Seat hardware, Player identity, Game metadata, two-player recipes, and
# temporary runtime bindings separate and contains no credential/script/runtime-handle fields.
add_library(hydra_profile_schema STATIC
    src/profile_schema.cpp
    include/hydra/profile_schema.hpp
)
if(MSVC)
    target_compile_options(hydra_profile_schema PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_profile_schema PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-MIG-01 read-only legacy parser plus staged, byte-preserving backup and
# directory-level transactional commit/rollback for the separated v1 stores.
add_library(hydra_profile_migration STATIC
    src/profile_migration.cpp
    include/hydra/profile_migration.hpp
)
target_link_libraries(hydra_profile_migration PUBLIC hydra_profile_schema)
if(MSVC)
    target_compile_options(hydra_profile_migration PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_profile_migration PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-CATALOG-01 provider-neutral, read-only candidate reconciliation into bounded
# stable GameRecords plus catalog-only icon/architecture/staleness metadata.
add_library(hydra_game_catalog STATIC
    src/game_catalog.cpp
    include/hydra/game_catalog.hpp
)
target_link_libraries(hydra_game_catalog PUBLIC hydra_profile_schema)
if(MSVC)
    target_compile_options(hydra_game_catalog PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_game_catalog PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-PROV-01 bounded lawful launcher-provider boundary. Provider-specific I/O
# stays behind this contract; the core validates discovery, account references,
# typed launch requests, revision freshness, and process evidence transactionally.
add_library(hydra_provider_adapter STATIC
    src/provider_adapter.cpp
    include/hydra/provider_adapter.hpp
)
target_link_libraries(hydra_provider_adapter PUBLIC hydra_game_catalog)
if(MSVC)
    target_compile_options(hydra_provider_adapter PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_provider_adapter PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-PROV-02 independently implemented read-only Steam metadata adapter.
# It parses bounded local KeyValues metadata, creates normal steam://run plans,
# keeps authentication in Steam, and never launches or mutates provider state.
add_library(hydra_steam_provider STATIC
    src/steam_provider.cpp
    include/hydra/steam_provider.hpp
)
target_link_libraries(hydra_steam_provider PUBLIC hydra_provider_adapter)
if(WIN32)
    target_link_libraries(hydra_steam_provider PRIVATE advapi32)
endif()

# P6-PROV-03D typed manual executable fallback. Native inspection is read-only;
# launch output is an exact executable path plus argument vector/working folder.
add_library(hydra_custom_executable_provider STATIC
    src/custom_executable_provider.cpp
    include/hydra/custom_executable_provider.hpp
)
target_link_libraries(hydra_custom_executable_provider PUBLIC hydra_provider_adapter)
if(MSVC)
    target_compile_options(hydra_custom_executable_provider PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_custom_executable_provider PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
if(MSVC)
    target_compile_options(hydra_steam_provider PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_steam_provider PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-PLAN-01 pure immutable provider-aware plan compiler. It resolves bounded
# Seat/Player/Game/setup/provider snapshots and performs exact hardware/capability
# preflight without launching a process or mutating provider/system state.
add_library(hydra_provider_launch_plan STATIC
    src/provider_launch_plan.cpp
    include/hydra/provider_launch_plan.hpp
)
target_link_libraries(hydra_provider_launch_plan PUBLIC
    hydra_provider_adapter hydra_profile_schema hydra_two_seat_launch)
if(MSVC)
    target_compile_options(hydra_provider_launch_plan PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_provider_launch_plan PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-PREFLIGHT-01 deterministic normal/expert requirement, risk, and typed
# mutation preview. The preview surface cannot carry arbitrary mutation payloads.
add_library(hydra_plan_preflight STATIC
    src/plan_preflight.cpp
    include/hydra/plan_preflight.hpp
)
target_link_libraries(hydra_plan_preflight PUBLIC hydra_provider_launch_plan)
if(MSVC)
    target_compile_options(hydra_plan_preflight PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_plan_preflight PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-PROFILE-01 typed two-player setup generation/editor model. Automatic
# generation is pure/read-only, while manual Save is transactional and keeps the
# previous valid setup on validation failure.
add_library(hydra_two_player_setup_editor STATIC
    src/two_player_setup_editor.cpp
    include/hydra/two_player_setup_editor.hpp
)
target_link_libraries(hydra_two_player_setup_editor PUBLIC hydra_profile_schema)
if(MSVC)
    target_compile_options(hydra_two_player_setup_editor PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_two_player_setup_editor PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P7-I18N-01 stable UI-string catalog. Protocol/schema/diagnostic identifiers stay
# canonical English while presentation strings have en-US/ko-KR/zh-CN entries.
add_library(hydra_ui_localization STATIC
    src/ui_localization.cpp
    src/ui_accessibility.cpp
    include/hydra/ui_localization.hpp
    include/hydra/ui_accessibility.hpp
)
if(MSVC)
    target_compile_options(hydra_ui_localization PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_ui_localization PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-UI-01 Qt-independent product-flow model. UI state is exercised with fake
# providers and immutable snapshots; the model performs no launch or mutation.
add_library(hydra_launcher_ui_model STATIC
    src/launcher_ui_model.cpp
    include/hydra/launcher_ui_model.hpp
)
target_link_libraries(hydra_launcher_ui_model PUBLIC
    hydra_plan_preflight hydra_two_player_setup_editor hydra_game_catalog)
if(MSVC)
    target_compile_options(hydra_launcher_ui_model PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_launcher_ui_model PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P7-SHELL-01 pure per-Seat presentation/policy state and its capability-
# restricted host adapter. Neither surface exposes whole-machine commands.
add_library(hydra_seat_launcher STATIC
    src/seat_launcher_model.cpp
    src/seat_host_client.cpp
    src/seat_launch_flow.cpp
    src/seat_notification_model.cpp
    src/seat_hotkey_model.cpp
    src/seat_navigation_model.cpp
    include/hydra/seat_launcher_model.hpp
    include/hydra/seat_host_client.hpp
    include/hydra/seat_launch_flow.hpp
    include/hydra/seat_notification_model.hpp
    include/hydra/seat_hotkey_model.hpp
    include/hydra/seat_navigation_model.hpp
)
target_link_libraries(hydra_seat_launcher PUBLIC
    hydra_host_transport hydra_launcher_ui_model)
if(MSVC)
    target_compile_options(hydra_seat_launcher PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_seat_launcher PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-IMPORT-01 versioned portable two-player setup package with provenance,
# path-variable redaction, strict decode, explicit local remap, and revalidation.
add_library(hydra_setup_package STATIC
    src/setup_package.cpp
    include/hydra/setup_package.hpp
)
target_link_libraries(hydra_setup_package PUBLIC hydra_two_player_setup_editor)
if(MSVC)
    target_compile_options(hydra_setup_package PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_setup_package PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P6-CLI-01 deterministic expert renderer plus redacted stable plan snapshots.
add_library(hydra_profile_cli STATIC
    src/profile_cli.cpp
    include/hydra/profile_cli.hpp
)
target_link_libraries(hydra_profile_cli PUBLIC hydra_profile_schema hydra_provider_launch_plan)
if(MSVC)
    target_compile_options(hydra_profile_cli PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_profile_cli PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_executable(hydraseat_profilectl src/profilectl_main.cpp)
target_link_libraries(hydraseat_profilectl PRIVATE hydra_profile_cli)
if(MSVC)
    target_compile_options(hydraseat_profilectl PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydraseat_profilectl PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P8-WATCH-01 versioned lease/rollback protocol and idempotent rollback registry.
# The portable core contains no arbitrary command execution or persistent-state I/O.
add_library(hydra_watchdog_core STATIC
    src/watchdog_protocol.cpp
    src/rollback_registry.cpp
    include/hydra/watchdog_protocol.hpp
    include/hydra/rollback_registry.hpp
)
if(MSVC)
    target_compile_options(hydra_watchdog_core PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_watchdog_core PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-D-02 durable recovery record + narrow RestoreSnapshotState executor. The
# watchdog/reset path restores only a numeric resource ID from the bounded
# HydraSeat recovery root and refuses stale external-state overwrite.
add_library(hydra_hidhide_session_recovery STATIC
    src/hidhide_session_recovery.cpp
    include/hydra/hidhide_session_recovery.hpp
)
target_link_libraries(hydra_hidhide_session_recovery PUBLIC
    hydra_hidhide_session hydra_watchdog_core)
if(MSVC)
    target_compile_options(hydra_hidhide_session_recovery PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_hidhide_session_recovery PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P5-AUD-01 read-only Core Audio endpoint inventory and two-Seat assignment
# validation. This target deliberately exposes no default-device, volume, or
# per-process routing mutation; those belong to later capability packets.
add_library(hydra_audio_inventory STATIC
    src/audio_endpoint_inventory.cpp
    src/audio_endpoint_native.cpp
    include/hydra/audio_endpoint_inventory.hpp
)
target_link_libraries(hydra_audio_inventory PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(hydra_audio_inventory PUBLIC Ole32.lib Uuid.lib)
endif()
if(MSVC)
    target_compile_options(hydra_audio_inventory PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_audio_inventory PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P5-AUD-02 exact-owned audio session observation and typed route transaction.
# The native Windows implementation is observation-only until a documented,
# reversible routing backend is selected; no private audio-policy COM API is used.
add_library(hydra_audio_routing STATIC
    src/audio_routing.cpp
    src/audio_session_native.cpp
    include/hydra/audio_routing.hpp
)
target_link_libraries(hydra_audio_routing PUBLIC hydra_audio_inventory)
if(MSVC)
    target_compile_options(hydra_audio_routing PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_audio_routing PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P8-JOURNAL-01 bounded durable crash journal and safe-mode marker. The journal
# is evidence only: action IDs are validated against the trusted watchdog plan
# and cannot introduce arbitrary commands or paths.
add_library(hydra_crash_journal STATIC
    src/crash_journal.cpp
    include/hydra/crash_journal.hpp
)
target_link_libraries(hydra_crash_journal PUBLIC hydra_watchdog_core)
if(WIN32)
    target_link_libraries(hydra_crash_journal PRIVATE Shell32.lib Ole32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_crash_journal PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_crash_journal PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P8-RESET-01 bounded runtime reset registration and verified emergency-reset
# execution. The crash journal remains evidence-only; reset authority is the
# journal-correlated exact watchdog manifest plus exact runtime-owner identity.
add_library(hydra_reset_actions STATIC
    src/reset_actions.cpp
    include/hydra/reset_actions.hpp
)
target_link_libraries(hydra_reset_actions PUBLIC
    hydra_crash_journal hydra_watchdog_core)
if(MSVC)
    target_compile_options(hydra_reset_actions PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_reset_actions PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-REC-01 Gate C recovery contract. The trusted rollback manifest contains
# only exact controlled-process termination identities; process-local shim and
# adapter state disappears with the owned target process.
add_library(hydra_gate_c_recovery STATIC
    src/gate_c_recovery.cpp
    include/hydra/gate_c_recovery.hpp
)
target_link_libraries(hydra_gate_c_recovery PUBLIC
    hydra_crash_journal hydra_watchdog_core)
if(MSVC)
    target_compile_options(hydra_gate_c_recovery PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_gate_c_recovery PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# Phase 3 controlled-process protocol and virtual input-state core.
add_library(hydra_gate_c_core STATIC
    src/gate_c_architecture.cpp
    src/gate_c_protocol.cpp
    src/gate_c_probe_snapshot.cpp
    src/gate_c_transport.cpp
    src/virtual_input_state.cpp
    include/hydra/gate_c_architecture.hpp
    include/hydra/gate_c_protocol.hpp
    include/hydra/gate_c_probe_snapshot.hpp
    include/hydra/gate_c_transport.hpp
    include/hydra/virtual_input_state.hpp
)
target_link_libraries(hydra_gate_c_core PUBLIC hydra_virtual_xinput)
set_target_properties(hydra_gate_c_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
if(WIN32)
    target_link_libraries(hydra_gate_c_core PUBLIC Bcrypt.lib)
endif()

# P3-RAW-01 fixed-width, versioned, bounded trace/parser contract. This
# library is portable and contains no Raw Input virtualization.
add_library(hydra_raw_input_probe_trace STATIC
    src/raw_input_probe_trace.cpp
    include/hydra/raw_input_probe_trace.hpp
)
if(MSVC)
    target_compile_options(hydra_raw_input_probe_trace PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_raw_input_probe_trace PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
if(MSVC)
    target_compile_options(hydra_gate_c_core PRIVATE /W4 /permissive-)
endif()

# P3-RAW-02 bounded process-local registration, synthetic-handle, packet, and
# queue model. Win32 API interposition remains in the startup-loaded shim.
add_library(hydra_virtual_raw_input STATIC
    src/virtual_raw_input.cpp
    include/hydra/virtual_raw_input.hpp
)
set_target_properties(hydra_virtual_raw_input PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(hydra_virtual_raw_input PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_virtual_raw_input PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# Process-local Gate C adapter boundary. The controlled target links this
# shared library normally; no process injection is performed.
add_library(hydra_gate_c_adapter SHARED
    src/gate_c_adapter.cpp
    include/hydra/gate_c_adapter.h
)
target_compile_definitions(hydra_gate_c_adapter PRIVATE
    HYDRA_GATE_C_ADAPTER_BUILD=1
)
target_link_libraries(hydra_gate_c_adapter PRIVATE
    hydra_gate_c_core hydra_virtual_raw_input)
hydra_set_gate_c_output(hydra_gate_c_adapter)
if(MSVC)
    target_compile_options(hydra_gate_c_adapter PRIVATE /W4 /permissive-)
endif()

# P3-API-02/03 and P3-RAW-02 bounded polling, cursor/focus, and Raw Input IAT
# transaction support. The startup-loaded DLL is selected per architecture
# and is never remotely injected.
add_library(hydra_gate_c_shim_support STATIC
    src/gate_c_cursor_focus_shim.cpp
    src/gate_c_raw_input_shim.cpp
    src/gate_c_shim_iat.cpp
    include/hydra/gate_c_cursor_focus_policy.hpp
    include/hydra/gate_c_raw_input_shim.hpp
    include/hydra/win32_iat_patch.hpp
)
set_target_properties(hydra_gate_c_shim_support PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(hydra_gate_c_shim_support PRIVATE /W4 /permissive-)
endif()

add_library(hydra_gate_c_shim SHARED
    src/gate_c_shim.cpp
    include/hydra/gate_c_shim_api.h
)
target_compile_definitions(hydra_gate_c_shim PRIVATE
    HYDRA_GATE_C_SHIM_BUILD=1
)
target_link_libraries(hydra_gate_c_shim PRIVATE
    hydra_gate_c_adapter hydra_gate_c_shim_support
)
set_target_properties(hydra_gate_c_shim PROPERTIES
    OUTPUT_NAME hydra_gate_c_shim
)
hydra_set_gate_c_output(hydra_gate_c_shim)
if(MSVC)
    target_compile_options(hydra_gate_c_shim PRIVATE /W4 /permissive-)
endif()

# P3-E-01 narrow bridge for a newly created, suspended, non-protected external
# acceptance target. It is not an attach/general injection surface and reuses
# the existing Gate C adapter, profiled shim, transport, and rollback contract.
if(WIN32)
    add_library(hydra_gate_c_external_bridge SHARED
        src/gate_c_external_bridge.cpp
        include/hydra/gate_c_external_profile.hpp
    )
    target_link_libraries(hydra_gate_c_external_bridge PRIVATE
        hydra_gate_c_core hydra_gate_c_adapter hydra_gate_c_shim User32.lib
    )
    set_target_properties(hydra_gate_c_external_bridge PROPERTIES
        OUTPUT_NAME hydra_gate_c_external_bridge
    )
    hydra_set_gate_c_output(hydra_gate_c_external_bridge)
    if(MSVC)
        target_compile_options(hydra_gate_c_external_bridge PRIVATE /W4 /permissive-)
    endif()

    add_executable(hydra_gate_c_external_harness
        src/gate_c_external_harness.cpp
        include/hydra/gate_c_external_profile.hpp
    )
    target_link_libraries(hydra_gate_c_external_harness PRIVATE
        hydra_gate_c_core User32.lib
    )
    hydra_set_gate_c_output(hydra_gate_c_external_harness)
    if(MSVC)
        target_compile_options(hydra_gate_c_external_harness PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME GateCExternalHarnessHelp
        COMMAND hydra_gate_c_external_harness --help)
endif()

# Core Executable target (Windows GUI subsystem)
add_executable(HydraSeat WIN32 src/main.cpp ${HYDRA_SOURCES} ${HYDRA_HEADERS})
target_link_libraries(HydraSeat PRIVATE
    hydra_hardware hydra_host_transport hydra_control_surface hydra_session_control
    hydra_launcher_ui_model hydra_steam_provider hydra_custom_executable_provider
    hydra_ui_localization)

if(WIN32)
    target_link_libraries(HydraSeat PRIVATE ${WIN32_LIBS})
endif()

# P4-RUN-01 headless per-user runtime authority. It deliberately has no Qt or
# GUI dependency; P4-IPC-01 will add the long-lived local control transport.
add_executable(hydra_host
    src/host_main.cpp
    src/workspace_manager.cpp
    include/hydra/workspace_manager.hpp
)
target_link_libraries(hydra_host PRIVATE hydra_runtime hydra_host_transport)
if(MSVC)
    target_compile_options(hydra_host PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_host PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_executable(hydra_hostctl src/hostctl_main.cpp)
target_link_libraries(hydra_hostctl PRIVATE hydra_host_transport)
if(MSVC)
    target_compile_options(hydra_hostctl PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_hostctl PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P7-SHELL-01 minimal per-Seat Windows launcher. It can issue only Seat-local
# game lifecycle commands through hydra_seat_launcher and remains hidden until
# its authoritative display group resolves against a fresh topology snapshot.
add_executable(hydra_seat_ui WIN32 src/seat_ui_main.cpp)
target_link_libraries(hydra_seat_ui PRIVATE
    hydra_seat_launcher hydra_seat_display_layout hydra_ui_localization)
if(WIN32)
    target_link_libraries(hydra_seat_ui PRIVATE User32.lib Shell32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_seat_ui PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_seat_ui PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(Qt6_FOUND)
    target_link_libraries(HydraSeat PRIVATE Qt6::Core Qt6::Widgets)
    target_compile_definitions(HydraSeat PRIVATE HYDRA_HAS_QT=1)
endif()

# P8-RESET-01 independently launchable emergency reset tool. It depends only on
# recovery/watchdog core libraries and never on Qt or the main control UI.
add_executable(hydra_reset src/reset_main.cpp)
target_link_libraries(hydra_reset PRIVATE
    hydra_reset_actions hydra_hidhide_session_recovery)
if(MSVC)
    target_compile_options(hydra_reset PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_reset PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
if(WIN32)
    # No GUI/User32 dependency is required.
endif()

# Phase 1 command-line hardware report (does not depend on Qt).
add_executable(hydra_detect
    src/hardware_detect_cli.cpp
    ${HYDRA_HARDWARE_HEADERS}
)
target_link_libraries(hydra_detect PRIVATE hydra_hardware)
if(WIN32)
    target_link_libraries(hydra_detect PRIVATE ${WIN32_LIBS})
endif()
if(MSVC)
    target_compile_options(hydra_detect PRIVATE /W4 /permissive-)
endif()

# Phase 3 capability planner diagnostics (analysis only; activates no backend).
add_executable(hydra_plan
    src/isolation_plan_cli.cpp
    src/input_isolation.cpp
    src/workspace_manager.cpp
    include/hydra/input_isolation.hpp
    include/hydra/workspace_manager.hpp
)
if(MSVC)
    target_compile_options(hydra_plan PRIVATE /W4 /permissive-)
endif()

# P3-D-01 bounded read-only diagnostics CLI. It never requests elevation and
# returns an observational report even when HidHide is absent or unverified.
add_executable(hydra_hidhide_probe_cli
    src/hidhide_probe_cli.cpp
)
set_target_properties(hydra_hidhide_probe_cli PROPERTIES
    OUTPUT_NAME hydra_hidhide_probe
)
target_link_libraries(hydra_hidhide_probe_cli PRIVATE hydra_hidhide_probe)
if(MSVC)
    target_compile_options(hydra_hidhide_probe_cli PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_hidhide_probe_cli PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P3-D-02 guarded lab exposes only read-only status and synthetic self-test
# while physical acceptance is deferred. No native cloak command is available.
add_executable(hydra_hidhide_session_lab
    src/hidhide_session_lab.cpp
)
target_link_libraries(hydra_hidhide_session_lab PRIVATE
    hydra_hidhide_probe hydra_hidhide_session)
if(MSVC)
    target_compile_options(hydra_hidhide_session_lab PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_hidhide_session_lab PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P5-AUD-01 read-only endpoint diagnostic used for eventual physical evidence.
# It cannot change default devices, volume, or per-process routing.
add_executable(hydra_audio_diag src/audio_diag_main.cpp)
target_link_libraries(hydra_audio_diag PRIVATE hydra_audio_routing)
if(MSVC)
    target_compile_options(hydra_audio_diag PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_audio_diag PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P8-WATCH-01 independent rollback-only watchdog executable. The host-side
# launcher is intentionally deferred to the owning runtime/recovery packet.
add_executable(hydra_watchdog
    src/watchdog_main.cpp
)
target_link_libraries(hydra_watchdog PRIVATE
    hydra_watchdog_core hydra_hidhide_session_recovery)
if(MSVC)
    target_compile_options(hydra_watchdog PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_watchdog PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    if(MINGW)
        target_link_options(hydra_watchdog PRIVATE -municode)
    endif()
endif()

# P3-MET-01 report/self-test CLI. Live producers use hydra_input_metrics
# directly; report generation remains outside latency-sensitive callbacks.
add_executable(hydra_input_metrics_cli
    src/input_metrics_cli.cpp
)
set_target_properties(hydra_input_metrics_cli PROPERTIES
    OUTPUT_NAME hydra_input_metrics
)
target_link_libraries(hydra_input_metrics_cli PRIVATE hydra_input_metrics)
if(MSVC)
    target_compile_options(hydra_input_metrics_cli PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_input_metrics_cli PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# Phase 3 Gate A/B observation and explicit-routing harness.
# The interactive windows are Windows-only, while --self-test is portable.
add_executable(hydra_input_lab
    src/input_lab.cpp
    src/input_router.cpp
    src/input_observation.cpp
    src/input_isolation.cpp
    src/workspace_manager.cpp
    include/hydra/input_router.hpp
    include/hydra/input_observation.hpp
    include/hydra/input_isolation.hpp
    include/hydra/workspace_manager.hpp
)
target_link_libraries(hydra_input_lab PRIVATE hydra_hardware)
if(WIN32)
    target_link_libraries(hydra_input_lab PRIVATE ${WIN32_LIBS})
endif()
if(MSVC)
    target_compile_options(hydra_input_lab PRIVATE /W4 /permissive-)
endif()

# Phase 3 Gate C controlled target process. This is HydraSeat-owned test code,
# not a game injector or general-purpose hook runtime.
add_executable(hydra_gate_c_target
    src/gate_c_target.cpp
)
target_link_libraries(hydra_gate_c_target PRIVATE hydra_gate_c_core hydra_gate_c_adapter)
hydra_set_gate_c_output(hydra_gate_c_target)
if(WIN32)
    target_link_libraries(hydra_gate_c_target PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_gate_c_target PRIVATE /W4 /permissive-)
endif()

# Controlled Win32 API probe. Baseline mode is read-only; explicit polling,
# cursor/focus, and Raw Input self-test modes startup-load the owned shim.
add_executable(hydra_gate_c_api_probe
    src/gate_c_api_probe.cpp
)
target_link_libraries(hydra_gate_c_api_probe PRIVATE
    hydra_gate_c_core hydra_gate_c_adapter
)
hydra_set_gate_c_output(hydra_gate_c_api_probe)
if(WIN32)
    target_link_libraries(hydra_gate_c_api_probe PRIVATE User32.lib)
endif()

# P3-RAW-01 standalone controlled process. It observes only its own process's
# native registration/message/data/buffer behavior and never installs hooks.
add_executable(hydra_gate_c_raw_input_probe
    src/gate_c_raw_input_probe.cpp
)
target_link_libraries(hydra_gate_c_raw_input_probe PRIVATE
    hydra_raw_input_probe_trace hydra_hardware
)
hydra_set_gate_c_output(hydra_gate_c_raw_input_probe)
if(WIN32)
    target_link_libraries(hydra_gate_c_raw_input_probe PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_gate_c_raw_input_probe PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_gate_c_raw_input_probe PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
if(MSVC)
    target_compile_options(hydra_gate_c_api_probe PRIVATE /W4 /permissive-)
endif()

# P3-CTRL-02 HydraSeat-owned DirectInput probe. Controlled fixture modes are
# portable. Windows additionally performs read-only DirectInput 8 enumeration;
# it never acquires a device, changes cooperative level, sends force feedback,
# or replaces a system DLL.
add_executable(hydra_directinput_probe
    src/directinput_probe.cpp
)
target_link_libraries(hydra_directinput_probe PRIVATE hydra_directinput_policy)
if(WIN32)
    target_link_libraries(hydra_directinput_probe PRIVATE Dinput8.lib Dxguid.lib)
endif()
if(MSVC)
    target_compile_options(hydra_directinput_probe PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_directinput_probe PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# P5-CTRL-01 read-only native controller inventory diagnostic. It never sends
# vibration and explicitly labels XInput slots as runtime-only identities.
add_executable(hydra_controller_diag src/controller_diag_main.cpp)
target_link_libraries(hydra_controller_diag PRIVATE hydra_controller_runtime)
if(MSVC)
    target_compile_options(hydra_controller_diag PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_controller_diag PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# Phase 3 Gate C host: launches only the controlled target executable and
# routes Seat-owned Raw Input observations over the versioned named-pipe protocol.
add_executable(hydra_gate_c_host
    src/gate_c_host.cpp
    src/input_router.cpp
    src/input_observation.cpp
    src/input_isolation.cpp
    src/workspace_manager.cpp
    include/hydra/input_router.hpp
    include/hydra/input_observation.hpp
    include/hydra/input_isolation.hpp
    include/hydra/workspace_manager.hpp
)
target_link_libraries(hydra_gate_c_host PRIVATE
    hydra_gate_c_core hydra_gate_c_recovery hydra_reset_actions
    hydra_hardware hydra_input_metrics)
hydra_set_gate_c_output(hydra_gate_c_host)
if(WIN32)
    target_link_libraries(hydra_gate_c_host PRIVATE ${WIN32_LIBS})
    add_dependencies(hydra_gate_c_host hydra_watchdog)
    add_custom_command(TARGET hydra_gate_c_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:hydra_watchdog>
            $<TARGET_FILE_DIR:hydra_gate_c_host>/hydra_watchdog.exe
        VERBATIM)
endif()
if(MSVC)
    target_compile_options(hydra_gate_c_host PRIVATE /W4 /permissive-)
endif()

# Test Suite Executable Target
enable_testing()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(WIN32)
    find_program(HYDRA_POWERSHELL_EXECUTABLE NAMES powershell.exe powershell)
endif()
add_executable(hydra_tests tests/test_hydra.cpp ${HYDRA_SOURCES} ${HYDRA_HEADERS})
target_link_libraries(hydra_tests PRIVATE
    hydra_hardware hydra_host_transport hydra_control_surface hydra_session_control
    hydra_launcher_ui_model hydra_steam_provider hydra_custom_executable_provider)
if(WIN32)
    target_link_libraries(hydra_tests PRIVATE ${WIN32_LIBS})
endif()
if(Qt6_FOUND)
    target_link_libraries(hydra_tests PRIVATE Qt6::Core Qt6::Widgets)
endif()
add_test(NAME EngineUnitTests COMMAND hydra_tests)

# P4-RUN-01 pure runtime transition/rollback/concurrency tests and controlled
# headless process self-test. No hardware or persistent system state is touched.
add_executable(runtime_host_tests tests/test_runtime_host.cpp)
target_link_libraries(runtime_host_tests PRIVATE hydra_runtime Threads::Threads)
if(MSVC)
    target_compile_options(runtime_host_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(runtime_host_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME RuntimeHostTests COMMAND runtime_host_tests)

# P4-SEAT-01 authoritative per-Seat game lifecycle model. These tests use
# deterministic owned-instance fakes and prove independent start/stop/restart,
# failure isolation, duplicate/concurrent command rejection, and the v1 limit.
add_executable(seat_game_lifecycle_tests tests/test_seat_game_lifecycle.cpp)
target_link_libraries(seat_game_lifecycle_tests PRIVATE hydra_runtime Threads::Threads)
if(MSVC)
    target_compile_options(seat_game_lifecycle_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_game_lifecycle_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatGameLifecycleTests COMMAND seat_game_lifecycle_tests)

# P5-LAUNCH-01 immutable plan compiler, all-resource activation failure-index
# rollback, natural-exit Seat-local cleanup, and RuntimeHost two-Seat isolation.
add_executable(two_seat_launch_tests tests/test_two_seat_launch.cpp)
target_link_libraries(two_seat_launch_tests PRIVATE hydra_two_seat_launch hydra_runtime)
if(MSVC)
    target_compile_options(two_seat_launch_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(two_seat_launch_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME TwoSeatLaunchTests COMMAND two_seat_launch_tests)
set_tests_properties(TwoSeatLaunchTests PROPERTIES LABELS "P5-LAUNCH-01;P4-SEAT-01")

add_test(NAME RuntimeHostProcessSelfTest COMMAND hydra_host --self-test)

add_executable(host_protocol_tests tests/test_host_protocol.cpp)
target_link_libraries(host_protocol_tests PRIVATE hydra_host_protocol)
if(MSVC)
    target_compile_options(host_protocol_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(host_protocol_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME HostProtocolTests COMMAND host_protocol_tests)
set_tests_properties(HostProtocolTests PROPERTIES LABELS "P4-IPC-01;P7-SHELL-01")

add_executable(host_ipc_process_tests tests/test_host_process.cpp)
target_link_libraries(host_ipc_process_tests PRIVATE hydra_host_transport)
add_dependencies(host_ipc_process_tests hydra_host hydra_hostctl)
if(MSVC)
    target_compile_options(host_ipc_process_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(host_ipc_process_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME HostIpcProcessTests COMMAND host_ipc_process_tests)
set_tests_properties(HostIpcProcessTests PROPERTIES
    LABELS "P4-IPC-01;P4-SEAT-01;P7-SHELL-01")

add_executable(seat_launcher_model_tests tests/test_seat_launcher_model.cpp)
target_link_libraries(seat_launcher_model_tests PRIVATE hydra_seat_launcher)
if(MSVC)
    target_compile_options(seat_launcher_model_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_launcher_model_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatLauncherModelTests COMMAND seat_launcher_model_tests)
set_tests_properties(SeatLauncherModelTests PROPERTIES LABELS "P7-SHELL-01")

add_executable(seat_launch_flow_tests tests/test_seat_launch_flow.cpp)
target_link_libraries(seat_launch_flow_tests PRIVATE hydra_seat_launcher)
if(MSVC)
    target_compile_options(seat_launch_flow_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_launch_flow_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatLaunchFlowTests COMMAND seat_launch_flow_tests)
set_tests_properties(SeatLaunchFlowTests PROPERTIES LABELS "P7-LAUNCH-01")

add_executable(seat_notification_model_tests
    tests/test_seat_notification_model.cpp)
target_link_libraries(seat_notification_model_tests PRIVATE hydra_seat_launcher)
if(MSVC)
    target_compile_options(seat_notification_model_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_notification_model_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatNotificationModelTests COMMAND seat_notification_model_tests)
set_tests_properties(SeatNotificationModelTests PROPERTIES LABELS "P7-NOTIFY-01")

add_executable(seat_hotkey_model_tests tests/test_seat_hotkey_model.cpp)
target_link_libraries(seat_hotkey_model_tests PRIVATE hydra_seat_launcher)
if(MSVC)
    target_compile_options(seat_hotkey_model_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_hotkey_model_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatHotkeyModelTests COMMAND seat_hotkey_model_tests)
set_tests_properties(SeatHotkeyModelTests PROPERTIES LABELS "P7-HOTKEY-01")

add_executable(seat_navigation_model_tests tests/test_seat_navigation_model.cpp)
target_link_libraries(seat_navigation_model_tests PRIVATE hydra_seat_launcher)
if(MSVC)
    target_compile_options(seat_navigation_model_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_navigation_model_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatNavigationModelTests COMMAND seat_navigation_model_tests)
set_tests_properties(SeatNavigationModelTests PROPERTIES LABELS "P7-CURSOR-01")

add_executable(ui_localization_tests tests/test_ui_localization.cpp)
target_link_libraries(ui_localization_tests PRIVATE hydra_ui_localization)
if(MSVC)
    target_compile_options(ui_localization_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ui_localization_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME UiLocalizationTests COMMAND ui_localization_tests)
set_tests_properties(UiLocalizationTests PROPERTIES LABELS "P7-I18N-01")

add_executable(ui_accessibility_tests tests/test_ui_accessibility.cpp)
target_link_libraries(ui_accessibility_tests PRIVATE hydra_ui_localization)
if(MSVC)
    target_compile_options(ui_accessibility_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ui_accessibility_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME UiAccessibilityTests COMMAND ui_accessibility_tests)
set_tests_properties(UiAccessibilityTests PROPERTIES LABELS "P7-A11Y-01")

if(WIN32)
    add_executable(seat_launch_flow_process_tests
        tests/test_seat_launch_flow_process.cpp)
    target_link_libraries(seat_launch_flow_process_tests PRIVATE
        hydra_seat_launcher hydra_process hydra_runtime)
    if(MSVC)
        target_compile_options(seat_launch_flow_process_tests PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(seat_launch_flow_process_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
    add_test(NAME SeatLaunchFlowProcessTests COMMAND seat_launch_flow_process_tests)
    set_tests_properties(SeatLaunchFlowProcessTests PROPERTIES
        LABELS "P7-LAUNCH-01")
endif()

if(WIN32)
    add_executable(seat_ui_process_tests tests/test_seat_ui_process.cpp)
    target_link_libraries(seat_ui_process_tests PRIVATE hydra_host_transport)
    add_dependencies(seat_ui_process_tests hydra_host hydra_seat_ui)
    if(MSVC)
        target_compile_options(seat_ui_process_tests PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(seat_ui_process_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
    add_test(NAME SeatUiProcessTests COMMAND seat_ui_process_tests)
    set_tests_properties(SeatUiProcessTests PROPERTIES
        LABELS "P7-SHELL-01;P7-REC-01" RESOURCE_LOCK "HydraSeatHostPipe")
    set_tests_properties(HostIpcProcessTests PROPERTIES
        RESOURCE_LOCK "HydraSeatHostPipe")
endif()

add_executable(hydra_process_tree_child tests/process_tree_child.cpp)
if(TARGET seat_launch_flow_process_tests)
    add_dependencies(seat_launch_flow_process_tests hydra_process_tree_child)
endif()
if(MSVC)
    target_compile_options(hydra_process_tree_child PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_process_tree_child PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(WIN32)
    # P5-MVP-01 complete controlled two-Seat production-path harness. It uses
    # real owned Job Object process trees while keeping display/input/audio/controller
    # evidence explicitly synthetic until physical validation is performed.
    add_executable(hydra_two_seat_mvp_lab src/two_seat_mvp_lab.cpp)
    target_link_libraries(hydra_two_seat_mvp_lab PRIVATE
        hydra_two_seat_launch hydra_session_metrics hydra_runtime hydra_process
        Threads::Threads)
    add_dependencies(hydra_two_seat_mvp_lab hydra_process_tree_child)
    if(MSVC)
        target_compile_options(hydra_two_seat_mvp_lab PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hydra_two_seat_mvp_lab PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
    add_test(NAME TwoSeatMvpControlledSelfTest
             COMMAND hydra_two_seat_mvp_lab --self-test)
    set_tests_properties(TwoSeatMvpControlledSelfTest PROPERTIES
        TIMEOUT 60
        LABELS "P5-MVP-01;P5-LAUNCH-01;P5-MET-01")

    add_executable(seat_game_process_lifecycle_tests
        tests/test_seat_game_process_lifecycle.cpp)
    target_link_libraries(seat_game_process_lifecycle_tests PRIVATE
        hydra_runtime hydra_process hydra_host_protocol hydra_host_transport
        Threads::Threads)
    add_dependencies(seat_game_process_lifecycle_tests hydra_process_tree_child)
    if(MSVC)
        target_compile_options(seat_game_process_lifecycle_tests PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(seat_game_process_lifecycle_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
    add_test(NAME SeatGameProcessLifecycleTests
             COMMAND seat_game_process_lifecycle_tests)
    set_tests_properties(SeatGameProcessLifecycleTests PROPERTIES
        TIMEOUT 45
        LABELS "P4-SEAT-01;P4-REC-01")
endif()

add_executable(process_group_tests tests/test_process_group.cpp)
target_link_libraries(process_group_tests PRIVATE hydra_process)
add_dependencies(process_group_tests hydra_process_tree_child)
if(MSVC)
    target_compile_options(process_group_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(process_group_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ProcessGroupTests COMMAND process_group_tests)

add_executable(hydra_window_test_app tests/window_test_app.cpp)
if(MSVC)
    target_compile_options(hydra_window_test_app PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_window_test_app PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_executable(window_tracker_tests tests/test_window_tracker.cpp)
target_link_libraries(window_tracker_tests PRIVATE hydra_window_tracking)
add_dependencies(window_tracker_tests hydra_window_test_app)
if(WIN32)
    target_link_libraries(window_tracker_tests PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(window_tracker_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(window_tracker_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME WindowTrackerTests COMMAND window_tracker_tests)

add_executable(hydra_display_diag src/display_diag_main.cpp)
target_link_libraries(hydra_display_diag PRIVATE hydra_display_topology)
if(MSVC)
    target_compile_options(hydra_display_diag PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hydra_display_diag PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_executable(display_topology_tests tests/test_display_topology.cpp)
target_link_libraries(display_topology_tests PRIVATE hydra_display_topology)
if(MSVC)
    target_compile_options(display_topology_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(display_topology_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME DisplayTopologyTests COMMAND display_topology_tests)

add_executable(seat_display_layout_tests tests/test_seat_display_layout.cpp)
target_link_libraries(seat_display_layout_tests PRIVATE hydra_seat_display_layout)
if(MSVC)
    target_compile_options(seat_display_layout_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_display_layout_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatDisplayLayoutTests COMMAND seat_display_layout_tests)

add_executable(window_policy_tests tests/test_window_policy.cpp)
target_link_libraries(window_policy_tests PRIVATE hydra_window_placement)
if(MSVC)
    target_compile_options(window_policy_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(window_policy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME WindowPolicyTests COMMAND window_policy_tests)

add_executable(seat_runtime_policy_tests tests/test_seat_runtime_policy.cpp)
target_link_libraries(seat_runtime_policy_tests PRIVATE hydra_seat_runtime_policy)
if(MSVC)
    target_compile_options(seat_runtime_policy_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(seat_runtime_policy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SeatRuntimePolicyTests COMMAND seat_runtime_policy_tests)

add_executable(display_recovery_tests tests/test_display_recovery.cpp)
target_link_libraries(display_recovery_tests PRIVATE hydra_display_recovery)
if(MSVC)
    target_compile_options(display_recovery_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(display_recovery_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME DisplayRecoveryTests COMMAND display_recovery_tests)

add_executable(management_seat_tests tests/test_management_seat.cpp)
target_link_libraries(management_seat_tests PRIVATE hydra_management_seat)
if(MSVC)
    target_compile_options(management_seat_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(management_seat_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ManagementSeatTests COMMAND management_seat_tests)

add_executable(session_control_transition_tests tests/test_session_control_transition.cpp)
target_link_libraries(session_control_transition_tests PRIVATE hydra_session_control)
if(MSVC)
    target_compile_options(session_control_transition_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(session_control_transition_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SessionControlTransitionTests COMMAND session_control_transition_tests)

add_executable(control_surface_model_tests tests/test_control_surface_model.cpp)
target_link_libraries(control_surface_model_tests PRIVATE hydra_control_surface)
if(MSVC)
    target_compile_options(control_surface_model_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(control_surface_model_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ControlSurfaceModelTests COMMAND control_surface_model_tests)
set_tests_properties(ControlSurfaceModelTests PROPERTIES
    LABELS "P4-CTRL-01;P4-REC-01;P4-SEAT-01")

# Pure Phase 1 identity checks: deterministic and independent of attached hardware.
add_executable(hardware_identity_tests
    tests/test_hardware_identity.cpp
    include/hydra/hardware_identity.hpp
)
if(MSVC)
    target_compile_options(hardware_identity_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME HardwareIdentityTests COMMAND hardware_identity_tests)

# Pure Phase 3 capability-planner tests. No process injection or driver access.
add_executable(isolation_planner_tests
    tests/test_isolation_planner.cpp
    src/input_isolation.cpp
    src/workspace_manager.cpp
    include/hydra/input_isolation.hpp
    include/hydra/workspace_manager.hpp
)
if(MSVC)
    target_compile_options(isolation_planner_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME IsolationPlannerTests COMMAND isolation_planner_tests)
add_test(NAME IsolationPlanObservation COMMAND hydra_plan observation-harness)

# P3-D-01 platform-independent policy/failure tests plus a bounded CLI test.
add_executable(hidhide_probe_tests
    tests/test_hidhide_probe.cpp
)
target_link_libraries(hidhide_probe_tests PRIVATE hydra_hidhide_probe)
if(MSVC)
    target_compile_options(hidhide_probe_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hidhide_probe_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME HidHideProbeTests COMMAND hidhide_probe_tests)
add_test(NAME HidHideProbeCliSelfTest
         COMMAND hydra_hidhide_probe_cli --self-test)
if(WIN32)
    add_test(NAME HidHideProbeNativeReadOnly
             COMMAND hydra_hidhide_probe_cli --probe)
endif()

# P3-D-02 fake/synthetic transaction coverage. It never invokes the native
# HidHide control device and is safe to run while physical acceptance is deferred.
add_executable(hidhide_session_backend_tests
    tests/test_hidhide_session_backend.cpp
)
target_link_libraries(hidhide_session_backend_tests PRIVATE hydra_hidhide_session)
if(MSVC)
    target_compile_options(hidhide_session_backend_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hidhide_session_backend_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME HidHideSessionBackendTests COMMAND hidhide_session_backend_tests)
set_tests_properties(HidHideSessionBackendTests PROPERTIES LABELS "P3-D-02")
add_test(NAME HidHideSessionLabSelfTest COMMAND hydra_hidhide_session_lab --self-test)
set_tests_properties(HidHideSessionLabSelfTest PROPERTIES LABELS "P3-D-02")

add_executable(hidhide_session_recovery_tests
    tests/test_hidhide_session_recovery.cpp
)
target_link_libraries(hidhide_session_recovery_tests PRIVATE
    hydra_hidhide_session_recovery)
if(MSVC)
    target_compile_options(hidhide_session_recovery_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hidhide_session_recovery_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME HidHideSessionRecoveryTests COMMAND hidhide_session_recovery_tests)
set_tests_properties(HidHideSessionRecoveryTests PROPERTIES LABELS "P3-D-02;P8-WATCH-01")

# P5-AUD-01 deterministic fake inventory/notification/Seat-assignment tests.
add_executable(audio_endpoint_inventory_tests
    tests/test_audio_endpoint_inventory.cpp
)
target_link_libraries(audio_endpoint_inventory_tests PRIVATE hydra_audio_inventory)
if(MSVC)
    target_compile_options(audio_endpoint_inventory_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(audio_endpoint_inventory_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME AudioEndpointInventoryTests COMMAND audio_endpoint_inventory_tests)
set_tests_properties(AudioEndpointInventoryTests PROPERTIES LABELS "P5-AUD-01")
add_test(NAME AudioEndpointDiagHelp COMMAND hydra_audio_diag --help)
set_tests_properties(AudioEndpointDiagHelp PROPERTIES LABELS "P5-AUD-01")

# P5-AUD-02 exact process/session ownership, late-session observation, typed
# mutable backend, apply verification, rollback, and unsupported production path.
add_executable(audio_routing_tests tests/test_audio_routing.cpp)
target_link_libraries(audio_routing_tests PRIVATE hydra_audio_routing)
if(MSVC)
    target_compile_options(audio_routing_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(audio_routing_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME AudioRoutingTests COMMAND audio_routing_tests)
set_tests_properties(AudioRoutingTests PROPERTIES LABELS "P5-AUD-02")

# P8-WATCH-01 portable protocol/registry tests and Windows process/fault tests.
add_executable(watchdog_protocol_tests
    tests/test_watchdog_protocol.cpp
)
target_link_libraries(watchdog_protocol_tests PRIVATE hydra_watchdog_core)
if(MSVC)
    target_compile_options(watchdog_protocol_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(watchdog_protocol_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME WatchdogProtocolTests COMMAND watchdog_protocol_tests)
if(WIN32)
    add_executable(watchdog_process_tests
        tests/test_watchdog_process.cpp
    )
    target_link_libraries(watchdog_process_tests PRIVATE hydra_watchdog_core)
    add_dependencies(watchdog_process_tests hydra_watchdog)
    if(MSVC)
        target_compile_options(watchdog_process_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME WatchdogProcessFaultTests COMMAND watchdog_process_tests)
    set_tests_properties(WatchdogProcessFaultTests PROPERTIES
        TIMEOUT 60
        LABELS "P4-REC-01;P8-WATCH-01")
endif()

# P8-JOURNAL-01 fixed-width codec/state-machine, storage, corruption, rotation,
# and startup safe-mode tests. Native storage tests use only a temporary directory.
add_executable(crash_journal_tests
    tests/test_crash_journal.cpp
)
target_link_libraries(crash_journal_tests PRIVATE hydra_crash_journal)
if(MSVC)
    target_compile_options(crash_journal_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(crash_journal_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME CrashJournalTests COMMAND crash_journal_tests)

# P8-RESET-01 codec/state/rollback/idempotency tests. Windows process tests run
# the actual hydra_reset executable against exact owned process identities.
add_executable(reset_actions_tests
    tests/test_reset_actions.cpp
)
target_link_libraries(reset_actions_tests PRIVATE hydra_reset_actions)
if(MSVC)
    target_compile_options(reset_actions_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(reset_actions_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ResetActionsTests COMMAND reset_actions_tests)
if(WIN32)
    add_executable(reset_process_tests tests/test_reset_process.cpp)
    target_link_libraries(reset_process_tests PRIVATE hydra_reset_actions)
    add_dependencies(reset_process_tests hydra_reset)
    if(MSVC)
        target_compile_options(reset_process_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME ResetProcessTests COMMAND reset_process_tests)
    set_tests_properties(ResetProcessTests PROPERTIES TIMEOUT 90)
endif()

# P3-REC-01 portable trusted-plan and durable recovery-journal lifecycle tests.
add_executable(gate_c_recovery_tests
    tests/test_gate_c_recovery.cpp
)
target_link_libraries(gate_c_recovery_tests PRIVATE hydra_gate_c_recovery)
if(MSVC)
    target_compile_options(gate_c_recovery_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gate_c_recovery_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME GateCRecoveryCoreTests COMMAND gate_c_recovery_tests)
if(WIN32)
    add_executable(gate_c_watchdog_recovery_tests
        tests/test_gate_c_watchdog_recovery.cpp
    )
    target_link_libraries(gate_c_watchdog_recovery_tests PRIVATE
        hydra_crash_journal)
    hydra_set_gate_c_output(gate_c_watchdog_recovery_tests)
    add_custom_target(stage_gate_c_watchdog_reset
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:hydra_reset>
            $<TARGET_FILE_DIR:gate_c_watchdog_recovery_tests>/hydra_reset.exe
        DEPENDS hydra_reset
        VERBATIM)
    add_dependencies(gate_c_watchdog_recovery_tests
        hydra_gate_c_host hydra_gate_c_target hydra_gate_c_api_probe
        hydra_gate_c_shim hydra_watchdog stage_gate_c_watchdog_reset)
    if(MSVC)
        target_compile_options(gate_c_watchdog_recovery_tests PRIVATE
            /W4 /permissive-)
    endif()
    add_test(NAME GateCWatchdogRecoveryTests
             COMMAND gate_c_watchdog_recovery_tests)
    set_tests_properties(GateCWatchdogRecoveryTests PROPERTIES TIMEOUT 180)
endif()

# Pure Gate A/B observation and explicit Seat-routing tests.
add_executable(input_observation_tests
    tests/test_input_observation.cpp
    src/input_observation.cpp
    src/input_isolation.cpp
    src/workspace_manager.cpp
    include/hydra/input_observation.hpp
    include/hydra/input_isolation.hpp
    include/hydra/input_router.hpp
    include/hydra/workspace_manager.hpp
)
if(MSVC)
    target_compile_options(input_observation_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME InputObservationTests COMMAND input_observation_tests)
add_test(NAME InputLabSelfTest COMMAND hydra_input_lab --self-test)

# Pure Gate C wire-protocol and process-local virtual-state tests.
add_executable(gate_c_protocol_tests
    tests/test_gate_c_protocol.cpp
)
target_link_libraries(gate_c_protocol_tests PRIVATE hydra_gate_c_core)
if(MSVC)
    target_compile_options(gate_c_protocol_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME GateCProtocolTests COMMAND gate_c_protocol_tests)

# P3-ARCH-01 process-architecture detection, PE validation, and the bounded
# architecture-specific artifact manifest/selector.
add_executable(gate_c_architecture_tests
    tests/test_gate_c_architecture.cpp
)
target_link_libraries(gate_c_architecture_tests PRIVATE hydra_gate_c_core)
hydra_set_gate_c_output(gate_c_architecture_tests)
if(MSVC)
    target_compile_options(gate_c_architecture_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME GateCArchitectureTests COMMAND gate_c_architecture_tests)

# P3-API-01 fixed-width, versioned OS/adapter comparison snapshot tests.
add_executable(gate_c_probe_snapshot_tests
    tests/test_gate_c_probe_snapshot.cpp
)
target_link_libraries(gate_c_probe_snapshot_tests PRIVATE hydra_gate_c_core)
if(MSVC)
    target_compile_options(gate_c_probe_snapshot_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME GateCProbeSnapshotTests COMMAND gate_c_probe_snapshot_tests)

# P3-RAW-01 portable trace/fixture and malformed data/buffer contract tests.
add_executable(raw_input_probe_trace_tests
    tests/test_raw_input_probe_trace.cpp
)
target_link_libraries(raw_input_probe_trace_tests PRIVATE
    hydra_raw_input_probe_trace
)
target_compile_definitions(raw_input_probe_trace_tests PRIVATE
    HYDRA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
if(MSVC)
    target_compile_options(raw_input_probe_trace_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(raw_input_probe_trace_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME RawInputProbeTraceTests COMMAND raw_input_probe_trace_tests)
add_test(NAME RawInputProbeSelfTest
         COMMAND hydra_gate_c_raw_input_probe --self-test)

# P3-RAW-02 portable registration/token/queue/data/buffer contract tests.
add_executable(virtual_raw_input_tests
    tests/test_virtual_raw_input.cpp
)
target_link_libraries(virtual_raw_input_tests PRIVATE hydra_virtual_raw_input)
if(MSVC)
    target_compile_options(virtual_raw_input_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(virtual_raw_input_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME VirtualRawInputTests COMMAND virtual_raw_input_tests)

# P3-CTRL-01 normalized XInput logical-slot, generation, metadata, battery,
# and vibration-route component tests.
add_executable(virtual_xinput_state_tests
    tests/test_virtual_xinput_state.cpp
)
target_link_libraries(virtual_xinput_state_tests PRIVATE hydra_virtual_xinput)
if(MSVC)
    target_compile_options(virtual_xinput_state_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(virtual_xinput_state_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME VirtualXInputStateTests COMMAND virtual_xinput_state_tests)

# P3-CTRL-02 stable instance-ID allowlist/order policy and controlled probe tests.
add_executable(directinput_policy_tests
    tests/test_directinput_policy.cpp
)
target_link_libraries(directinput_policy_tests PRIVATE hydra_directinput_policy)
if(MSVC)
    target_compile_options(directinput_policy_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(directinput_policy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME DirectInputPolicyTests COMMAND directinput_policy_tests)
add_test(NAME DirectInputControlledSeatA
         COMMAND hydra_directinput_probe --controlled-self-test seat-a)
add_test(NAME DirectInputControlledSeatB
         COMMAND hydra_directinput_probe --controlled-self-test seat-b)
if(WIN32)
    add_test(NAME DirectInputNativeObservationSelfTest
             COMMAND hydra_directinput_probe --native-observation-self-test)
endif()

# P5-CTRL-01 production source polling, stable/runtime identity separation,
# two-Seat mapping, reconnect generation, and exact vibration ownership tests.
add_executable(controller_runtime_tests tests/test_controller_runtime.cpp)
target_link_libraries(controller_runtime_tests PRIVATE hydra_controller_runtime)
if(MSVC)
    target_compile_options(controller_runtime_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(controller_runtime_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ControllerRuntimeTests COMMAND controller_runtime_tests)
set_tests_properties(ControllerRuntimeTests PROPERTIES LABELS "P5-CTRL-01")
add_test(NAME ControllerDiagHelp COMMAND hydra_controller_diag --help)
set_tests_properties(ControllerDiagHelp PROPERTIES LABELS "P5-CTRL-01")

# P3-MET-01 recorder/report contract, deterministic fixture, and CLI checks.
add_executable(input_metrics_tests
    tests/test_input_metrics.cpp
)
target_link_libraries(input_metrics_tests PRIVATE hydra_input_metrics)
if(MSVC)
    target_compile_options(input_metrics_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(input_metrics_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME InputMetricsTests COMMAND input_metrics_tests)
add_test(NAME InputMetricsCliSelfTest
         COMMAND hydra_input_metrics_cli --self-test)
add_test(NAME InputMetricsFixtureReport
         COMMAND hydra_input_metrics_cli --fixture-report)

# P5-MET-01 integrated evidence origin/verdict/privacy/zero-cross report tests.
add_executable(session_metrics_tests tests/test_session_metrics.cpp)
target_link_libraries(session_metrics_tests PRIVATE hydra_session_metrics)
if(MSVC)
    target_compile_options(session_metrics_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(session_metrics_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SessionMetricsTests COMMAND session_metrics_tests)
set_tests_properties(SessionMetricsTests PROPERTIES LABELS "P5-MET-01")

# P6-SCHEMA-01 strict version/bounds/Unicode/cross-reference persistence tests.
add_executable(profile_schema_tests tests/test_profile_schema.cpp)
target_link_libraries(profile_schema_tests PRIVATE hydra_profile_schema)
if(MSVC)
    target_compile_options(profile_schema_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(profile_schema_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ProfileSchemaTests COMMAND profile_schema_tests)
set_tests_properties(ProfileSchemaTests PROPERTIES LABELS "P6-SCHEMA-01")

# P6-MIG-01 deterministic conversion/report/backup plus injected write,
# staged-validation, commit, and post-commit rollback tests.
add_executable(profile_migration_tests tests/test_profile_migration.cpp)
target_link_libraries(profile_migration_tests PRIVATE hydra_profile_migration)
if(MSVC)
    target_compile_options(profile_migration_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(profile_migration_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ProfileMigrationTests COMMAND profile_migration_tests)
set_tests_properties(ProfileMigrationTests PROPERTIES LABELS "P6-MIG-01")

# P6-CATALOG-01 provider/executable identity, malformed metadata, deterministic
# duplicate reconciliation, missing artwork, staleness, and transaction tests.
add_executable(game_catalog_tests tests/test_game_catalog.cpp)
target_link_libraries(game_catalog_tests PRIVATE hydra_game_catalog)
if(MSVC)
    target_compile_options(game_catalog_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(game_catalog_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME GameCatalogTests COMMAND game_catalog_tests)
set_tests_properties(GameCatalogTests PROPERTIES LABELS "P6-CATALOG-01")

# P6-PROV-01 fake-provider contract, revision, bounds, offline/absent,
# transactional failure, typed-launch, and post-launch evidence tests.
add_executable(provider_adapter_tests tests/test_provider_adapter.cpp)
target_link_libraries(provider_adapter_tests PRIVATE hydra_provider_adapter)
if(MSVC)
    target_compile_options(provider_adapter_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(provider_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ProviderAdapterTests COMMAND provider_adapter_tests)
set_tests_properties(ProviderAdapterTests PROPERTIES LABELS "P6-PROV-01")

# P6-PROV-02 virtual metadata-source fixtures plus a Windows-only, read-only
# installed-Steam smoke test. Neither test starts Steam or a game.
add_executable(steam_provider_tests tests/test_steam_provider.cpp)
target_link_libraries(steam_provider_tests PRIVATE hydra_steam_provider)
if(MSVC)
    target_compile_options(steam_provider_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(steam_provider_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SteamProviderTests COMMAND steam_provider_tests)
set_tests_properties(SteamProviderTests PROPERTIES LABELS "P6-PROV-02")
if(WIN32)
    add_test(NAME SteamProviderLiveReadOnlySmoke
             COMMAND steam_provider_tests --live-smoke)
    set_tests_properties(SteamProviderLiveReadOnlySmoke PROPERTIES
        LABELS "P6-PROV-02" RUN_SERIAL TRUE)
endif()

# P6-PROV-03D virtual source fixtures and native read-only PE/path smoke. No
# test starts the selected executable or evaluates a shell command.
add_executable(custom_executable_provider_tests
    tests/test_custom_executable_provider.cpp)
target_link_libraries(custom_executable_provider_tests PRIVATE
    hydra_custom_executable_provider)
if(MSVC)
    target_compile_options(custom_executable_provider_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(custom_executable_provider_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME CustomExecutableProviderTests COMMAND custom_executable_provider_tests)
set_tests_properties(CustomExecutableProviderTests PROPERTIES LABELS "P6-PROV-03D")
if(WIN32)
    add_test(NAME CustomExecutableProviderLiveReadOnlySmoke
             COMMAND custom_executable_provider_tests --live-smoke)
    set_tests_properties(CustomExecutableProviderLiveReadOnlySmoke PROPERTIES
        LABELS "P6-PROV-03D" RUN_SERIAL TRUE)
endif()

# P6-PLAN-01 deterministic provider-aware plan/hash, same-game setup, stale
# requirement/provider, account ambiguity, and exact hardware preflight tests.
add_executable(provider_launch_plan_tests tests/test_provider_launch_plan.cpp)
target_link_libraries(provider_launch_plan_tests PRIVATE hydra_provider_launch_plan)
if(MSVC)
    target_compile_options(provider_launch_plan_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(provider_launch_plan_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ProviderLaunchPlanTests COMMAND provider_launch_plan_tests)
set_tests_properties(ProviderLaunchPlanTests PROPERTIES LABELS "P6-PLAN-01")

# P6-PREFLIGHT-01 normal/expert messages, deterministic mutation ordering,
# explicit approval gates, and typed no-secret mutation preview tests.
add_executable(plan_preflight_tests tests/test_plan_preflight.cpp)
target_link_libraries(plan_preflight_tests PRIVATE hydra_plan_preflight)
if(MSVC)
    target_compile_options(plan_preflight_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(plan_preflight_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME PlanPreflightTests COMMAND plan_preflight_tests)
set_tests_properties(PlanPreflightTests PROPERTIES LABELS "P6-PREFLIGHT-01")

# P6-UI-01 full normal flow over fake providers and immutable local snapshots.
add_executable(launcher_ui_model_tests tests/test_launcher_ui_model.cpp)
target_link_libraries(launcher_ui_model_tests PRIVATE hydra_launcher_ui_model)
if(MSVC)
    target_compile_options(launcher_ui_model_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(launcher_ui_model_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME LauncherUiModelTests COMMAND launcher_ui_model_tests)
set_tests_properties(LauncherUiModelTests PROPERTIES LABELS "P6-UI-01")

# P6-PROFILE-01 automatic/manual convergence, transactional Save, bounded path,
# shared-data-root, and cross-document runtime-selection validation tests.
add_executable(two_player_setup_editor_tests tests/test_two_player_setup_editor.cpp)
target_link_libraries(two_player_setup_editor_tests PRIVATE hydra_two_player_setup_editor)
if(MSVC)
    target_compile_options(two_player_setup_editor_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(two_player_setup_editor_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME TwoPlayerSetupEditorTests COMMAND two_player_setup_editor_tests)
set_tests_properties(TwoPlayerSetupEditorTests PROPERTIES LABELS "P6-PROFILE-01")

# P6-IMPORT-01 privacy-redacted package roundtrip, explicit remap, malformed
# envelope/version, and transactional failure tests.
add_executable(setup_package_tests tests/test_setup_package.cpp)
target_link_libraries(setup_package_tests PRIVATE hydra_setup_package)
if(MSVC)
    target_compile_options(setup_package_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(setup_package_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME SetupPackageTests COMMAND setup_package_tests)
set_tests_properties(SetupPackageTests PROPERTIES LABELS "P6-IMPORT-01")

# P6-REG-01 deterministic cross-packet fixture corpus covering provider/catalog
# churn, Seat/Player moves, same-game setup/import, protection, malformed data,
# provider absence/offline, Unicode, moved paths, and stale/uninstalled metadata.
add_executable(phase6_regression_tests tests/test_phase6_regression_fixtures.cpp)
target_link_libraries(phase6_regression_tests PRIVATE
    hydra_game_catalog hydra_provider_launch_plan hydra_plan_preflight
    hydra_two_player_setup_editor hydra_setup_package)
if(MSVC)
    target_compile_options(phase6_regression_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(phase6_regression_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME Phase6RegressionTests COMMAND phase6_regression_tests)
set_tests_properties(Phase6RegressionTests PROPERTIES LABELS "P6-REG-01")

# P6-CLI-01 stable-schema human/JSON rendering, plan snapshot roundtrip, Unicode,
# malformed-version rejection, and account-reference redaction tests.
add_executable(profile_cli_tests tests/test_profile_cli.cpp)
target_link_libraries(profile_cli_tests PRIVATE hydra_profile_cli)
if(MSVC)
    target_compile_options(profile_cli_tests PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(profile_cli_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME ProfileCliTests COMMAND profile_cli_tests)
set_tests_properties(ProfileCliTests PROPERTIES LABELS "P6-CLI-01")

# P3-HW-01 acceptance tooling is CI-tested without claiming physical acceptance.
# Python parses bounded synthetic evidence; Windows additionally checks that the
# guided/resumable PowerShell 5 runner and its manifest schema load successfully.
if(Python3_Interpreter_FOUND)
    add_test(NAME Phase3HardwareAcceptanceParserTests
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/summarize_phase3_trace.py"
                     --self-test)
endif()
if(WIN32 AND HYDRA_POWERSHELL_EXECUTABLE)
    add_test(NAME Phase3HardwareAcceptanceRunnerSelfTest
             COMMAND "${HYDRA_POWERSHELL_EXECUTABLE}"
                     -NoProfile -ExecutionPolicy Bypass
                     -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_phase3_hardware_acceptance.ps1"
                     -SelfTest)
endif()

# C ABI adapter tests exercise independent process-local contexts and
# GetAsyncKeyState/GetKeyboardState-style semantics without installing hooks.
add_executable(gate_c_adapter_tests
    tests/test_gate_c_adapter.cpp
)
target_link_libraries(gate_c_adapter_tests PRIVATE hydra_gate_c_adapter)
hydra_set_gate_c_output(gate_c_adapter_tests)
if(MSVC)
    target_compile_options(gate_c_adapter_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME GateCAdapterTests COMMAND gate_c_adapter_tests)

add_executable(gate_c_xinput_adapter_tests
    tests/test_gate_c_xinput_adapter.cpp
)
target_link_libraries(gate_c_xinput_adapter_tests PRIVATE
    hydra_gate_c_adapter)
hydra_set_gate_c_output(gate_c_xinput_adapter_tests)
if(MSVC)
    target_compile_options(gate_c_xinput_adapter_tests PRIVATE
        /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gate_c_xinput_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME GateCXInputAdapterTests COMMAND gate_c_xinput_adapter_tests)

add_executable(gate_c_adapter_c_smoke
    tests/test_gate_c_adapter_c.c
)
target_link_libraries(gate_c_adapter_c_smoke PRIVATE hydra_gate_c_adapter)
hydra_set_gate_c_output(gate_c_adapter_c_smoke)
if(MSVC)
    target_compile_options(gate_c_adapter_c_smoke PRIVATE /W4)
endif()
add_test(NAME GateCAdapterCSmoke COMMAND gate_c_adapter_c_smoke)

# P3-API-02 transaction, rollback, polling-semantics, and fixed C ABI tests.
add_executable(gate_c_polling_shim_tests
    tests/test_gate_c_polling_shim.cpp
)
target_link_libraries(gate_c_polling_shim_tests PRIVATE
    hydra_gate_c_adapter hydra_gate_c_shim hydra_gate_c_shim_support
)
hydra_set_gate_c_output(gate_c_polling_shim_tests)
if(WIN32)
    target_link_libraries(gate_c_polling_shim_tests PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(gate_c_polling_shim_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME GateCPollingShimTests COMMAND gate_c_polling_shim_tests)

add_executable(gate_c_cursor_focus_shim_tests
    tests/test_gate_c_cursor_focus_shim.cpp
)
target_link_libraries(gate_c_cursor_focus_shim_tests PRIVATE
    hydra_gate_c_shim_support
)
hydra_set_gate_c_output(gate_c_cursor_focus_shim_tests)
if(MSVC)
    target_compile_options(gate_c_cursor_focus_shim_tests PRIVATE
        /W4 /permissive-)
endif()
add_test(NAME GateCCursorFocusShimTests
         COMMAND gate_c_cursor_focus_shim_tests)

add_executable(gate_c_raw_input_shim_tests
    tests/test_gate_c_raw_input_shim.cpp
)
target_link_libraries(gate_c_raw_input_shim_tests PRIVATE
    hydra_gate_c_shim_support
)
hydra_set_gate_c_output(gate_c_raw_input_shim_tests)
if(MSVC)
    target_compile_options(gate_c_raw_input_shim_tests PRIVATE
        /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gate_c_raw_input_shim_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
add_test(NAME GateCRawInputShimTests
         COMMAND gate_c_raw_input_shim_tests)

add_executable(gate_c_shim_c_smoke
    tests/test_gate_c_shim_c.c
)
target_link_libraries(gate_c_shim_c_smoke PRIVATE hydra_gate_c_shim)
hydra_set_gate_c_output(gate_c_shim_c_smoke)
if(MSVC)
    target_compile_options(gate_c_shim_c_smoke PRIVATE /W4)
endif()
add_test(NAME GateCShimCSmoke COMMAND gate_c_shim_c_smoke)

add_test(NAME GateCTargetSelfTest COMMAND hydra_gate_c_target --self-test)

if(WIN32)
    add_test(
        NAME GateCArchitectureRuntimeSelfTest
        COMMAND gate_c_architecture_tests --runtime-self-test
                ${HYDRA_GATE_C_ARCH_NAME}
                $<TARGET_FILE:hydra_gate_c_target>
                $<TARGET_FILE:hydra_gate_c_adapter>
                $<TARGET_FILE:hydra_gate_c_shim>
    )

    add_test(
        NAME GateCApiProbeSelfTest
        COMMAND hydra_gate_c_api_probe --baseline-self-test
    )
    set_tests_properties(GateCApiProbeSelfTest PROPERTIES TIMEOUT 15)

    add_test(
        NAME GateCPollingShimProbeSelfTest
        COMMAND hydra_gate_c_api_probe --polling-shim-self-test
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCPollingShimProbeSelfTest PROPERTIES TIMEOUT 20)

    add_test(
        NAME GateCCursorFocusShimProbeSelfTest
        COMMAND hydra_gate_c_api_probe --cursor-focus-shim-self-test
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCCursorFocusShimProbeSelfTest PROPERTIES TIMEOUT 25)

    add_test(
        NAME GateCRawInputShimProbeSelfTest
        COMMAND hydra_gate_c_api_probe --raw-input-shim-self-test
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCRawInputShimProbeSelfTest PROPERTIES TIMEOUT 25)

    add_test(
        NAME GateCXInputProbeSelfTest
        COMMAND hydra_gate_c_api_probe --xinput-self-test
    )
    set_tests_properties(GateCXInputProbeSelfTest PROPERTIES TIMEOUT 15)

    add_test(
        NAME RawInputRegistrationSelfTest
        COMMAND hydra_gate_c_raw_input_probe --registration-self-test
                --output
                ${CMAKE_CURRENT_BINARY_DIR}/raw-input-observed-${HYDRA_GATE_C_ARCH_NAME}.json
    )
    set_tests_properties(RawInputRegistrationSelfTest PROPERTIES TIMEOUT 30)

    add_test(
        NAME RawInputProcessTeardownSelfTest
        COMMAND hydra_gate_c_raw_input_probe --process-teardown-self-test
    )
    set_tests_properties(RawInputProcessTeardownSelfTest PROPERTIES TIMEOUT 50)
endif()

if(WIN32)
    # Launches two separate controlled target processes, performs the versioned
    # handshake, sends different Seat states, queries snapshots, and verifies
    # no cross-process state bleed. No device hiding or injection occurs.
    add_test(
        NAME GateCProcessSelfTest
        COMMAND hydra_gate_c_host --self-test --target $<TARGET_FILE:hydra_gate_c_target>
    )
    set_tests_properties(GateCProcessSelfTest PROPERTIES TIMEOUT 45)

    add_test(
        NAME GateCProtocolErrorSelfTest
        COMMAND hydra_gate_c_host --protocol-error-self-test --target $<TARGET_FILE:hydra_gate_c_target>
    )
    set_tests_properties(GateCProtocolErrorSelfTest PROPERTIES TIMEOUT 30)

    add_test(
        NAME GateCApiProbeProcessSelfTest
        COMMAND hydra_gate_c_host --baseline-self-test --target $<TARGET_FILE:hydra_gate_c_api_probe>
    )
    set_tests_properties(GateCApiProbeProcessSelfTest PROPERTIES TIMEOUT 60)

    add_test(
        NAME GateCPollingShimProcessSelfTest
        COMMAND hydra_gate_c_host --polling-shim-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCPollingShimProcessSelfTest PROPERTIES TIMEOUT 75)

    add_test(
        NAME GateCCursorFocusShimProcessSelfTest
        COMMAND hydra_gate_c_host --cursor-focus-shim-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCCursorFocusShimProcessSelfTest PROPERTIES TIMEOUT 75)

    add_test(
        NAME GateCRawInputShimProcessSelfTest
        COMMAND hydra_gate_c_host --raw-input-shim-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCRawInputShimProcessSelfTest PROPERTIES TIMEOUT 75)

    add_test(
        NAME GateCXInputProcessSelfTest
        COMMAND hydra_gate_c_host --xinput-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
    )
    set_tests_properties(GateCXInputProcessSelfTest PROPERTIES TIMEOUT 75)
else()
    add_test(NAME GateCHostPortableSelfTest COMMAND hydra_gate_c_host --self-test)
endif()
