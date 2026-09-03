cmake_minimum_required(VERSION 3.20)
project(HydraSeat VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

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
    add_compile_definitions(_UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set(WIN32_LIBS User32.lib Cfgmgr32.lib Dxgi.lib Setupapi.lib Hid.lib Xinput.lib Comctl32.lib Comdlg32.lib)
endif()

find_package(Threads REQUIRED)

# Source files
set(HYDRA_HARDWARE_SOURCES
    src/hardware_detector.cpp
    src/raw_input_utils.cpp
)

set(HYDRA_SOURCES
    src/input_isolation.cpp
    src/input_observation.cpp
    src/gui_win32.cpp
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
    include/hydra/workspace_manager.hpp
    include/hydra/seat_assignment_projection.hpp
    include/hydra/gui_win32.hpp
    ${HYDRA_HARDWARE_HEADERS}
)

add_library(hydra_hardware STATIC
    ${HYDRA_HARDWARE_SOURCES}
    ${HYDRA_HARDWARE_HEADERS}
)
if(WIN32)
    target_link_libraries(hydra_hardware PRIVATE User32.lib Cfgmgr32.lib Setupapi.lib Xinput.lib)
endif()

# Canonical Raw Input router ownership. Production activation bridges and the
# existing GUI/lab/Gate-C consumers all reuse this implementation instead of
# compiling another copy of input_router.cpp into each executable.
add_library(hydra_input_router STATIC
    src/input_router.cpp
    include/hydra/input_router.hpp
)
target_link_libraries(hydra_input_router PRIVATE hydra_hardware)
if(WIN32)
    target_link_libraries(hydra_input_router PRIVATE User32.lib)
endif()
hydra_enable_strict_warnings(hydra_input_router)

# Portable Seat configuration ownership and projection. The Win32 setup surface
# supplies only visible assignment intent; persistence and hidden profile state
# stay outside the GUI layer.
add_library(hydra_workspace STATIC
    src/workspace_manager.cpp
    src/seat_assignment_projection.cpp
    include/hydra/workspace_manager.hpp
    include/hydra/seat_assignment_projection.hpp
)
hydra_enable_strict_warnings(hydra_workspace)

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
hydra_enable_strict_warnings(hydra_runtime)

# P4-IPC-01 portable fixed-width host protocol codec. Transport/authentication
# lives in a separate Windows-aware component so codec tests remain portable.
add_library(hydra_host_protocol STATIC
    src/host_protocol.cpp
    include/hydra/host_protocol.hpp
)
target_link_libraries(hydra_host_protocol PUBLIC hydra_runtime)
hydra_enable_strict_warnings(hydra_host_protocol)

add_library(hydra_host_transport STATIC
    src/host_transport.cpp
    include/hydra/host_transport.hpp
)
target_link_libraries(hydra_host_transport PUBLIC hydra_host_protocol)
if(WIN32)
    target_link_libraries(hydra_host_transport PRIVATE Advapi32.lib Bcrypt.lib)
endif()
hydra_enable_strict_warnings(hydra_host_transport)

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
hydra_enable_strict_warnings(hydra_process)

# P5-LAUNCH-01 immutable exactly-two-Seat launch plan and Seat-local activation
# transaction. Production resource backends are injected behind a typed factory;
# the coordinator owns ordering, verification, reverse rollback, and isolation.
add_library(hydra_two_seat_launch STATIC
    src/two_seat_launch.cpp
    include/hydra/two_seat_launch.hpp
)
target_link_libraries(hydra_two_seat_launch PUBLIC hydra_process hydra_runtime)
hydra_enable_strict_warnings(hydra_two_seat_launch)

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
hydra_enable_strict_warnings(hydra_window_tracking)

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
hydra_enable_strict_warnings(hydra_display_topology)

# P4-DIS-02 stable Seat display groups and reversible coordinate transforms.
add_library(hydra_seat_display_layout STATIC
    src/seat_display_layout.cpp
    include/hydra/seat_display_layout.hpp
)
target_link_libraries(hydra_seat_display_layout PUBLIC hydra_display_topology hydra_runtime)
hydra_enable_strict_warnings(hydra_seat_display_layout)

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
hydra_enable_strict_warnings(hydra_window_placement)

# P4-POL-01 serialized desired-vs-observed Seat runtime policy coordinator.
add_library(hydra_seat_runtime_policy STATIC
    src/seat_runtime_policy.cpp
    include/hydra/seat_runtime_policy.hpp
)
target_link_libraries(hydra_seat_runtime_policy PUBLIC hydra_window_placement)
hydra_enable_strict_warnings(hydra_seat_runtime_policy)

# P4-DIS-03 deterministic display hot-plug degradation and recovery planning.
add_library(hydra_display_recovery STATIC
    src/display_recovery.cpp
    include/hydra/display_recovery.hpp
)
target_link_libraries(hydra_display_recovery PUBLIC hydra_seat_display_layout hydra_seat_runtime_policy)
hydra_enable_strict_warnings(hydra_display_recovery)

hydra_enable_strict_warnings(hydra_hardware)

# P4-CTRL-01 Management Seat placement and global-control permission model.
add_library(hydra_management_seat STATIC
    src/management_seat.cpp
    include/hydra/management_seat.hpp
)
target_link_libraries(hydra_management_seat PUBLIC hydra_seat_display_layout)
if(WIN32)
    target_link_libraries(hydra_management_seat PRIVATE User32.lib)
endif()
hydra_enable_strict_warnings(hydra_management_seat)

# P4-CTRL-01 authoritative host/config-backed control-surface presentation model.
add_library(hydra_control_surface STATIC
    src/control_surface_model.cpp
    include/hydra/control_surface_model.hpp
)
target_link_libraries(hydra_control_surface PUBLIC hydra_management_seat hydra_runtime)
hydra_enable_strict_warnings(hydra_control_surface)

# P4-CTRL-02 verified Stop/Reconfigure/Save+Start control-surface state model.
add_library(hydra_session_control STATIC
    src/session_control_transition.cpp
    include/hydra/session_control_transition.hpp
)
target_link_libraries(hydra_session_control PUBLIC hydra_management_seat hydra_host_transport)
hydra_enable_strict_warnings(hydra_session_control)

# P3-CTRL-01 bounded, process-local normalized XInput state. This component
# contains no physical polling and performs no ordinary XInput API hooking.
add_library(hydra_virtual_xinput STATIC
    src/virtual_xinput_state.cpp
    include/hydra/virtual_xinput_state.hpp
)
set_target_properties(hydra_virtual_xinput PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
hydra_enable_strict_warnings(hydra_virtual_xinput)

# P3-CTRL-02 bounded, process-local DirectInput enumeration/visibility policy.
# The core is portable and owns no DirectInput COM object or physical device.
add_library(hydra_directinput_policy STATIC
    src/directinput_policy.cpp
    include/hydra/directinput_policy.hpp
)
set_target_properties(hydra_directinput_policy PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
hydra_enable_strict_warnings(hydra_directinput_policy)

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
hydra_enable_strict_warnings(hydra_controller_runtime)

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
hydra_enable_strict_warnings(hydra_hidhide_probe)

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
hydra_enable_strict_warnings(hydra_hidhide_session)

# P3-MET-01 fixed-capacity, process-local metrics recorder and deterministic
# off-path report generator. The recorder performs no I/O on the input path.
add_library(hydra_input_metrics STATIC
    src/input_metrics.cpp
    include/hydra/input_metrics.hpp
)
set_target_properties(hydra_input_metrics PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
hydra_enable_strict_warnings(hydra_input_metrics)

# P5-MET-01 bounded privacy-safe integrated two-Seat evidence report. This
# aggregates existing input metrics plus Seat capability/timing outcomes and
# keeps physical-validation eligibility separate from controlled/synthetic pass.
add_library(hydra_session_metrics STATIC
    src/session_metrics.cpp
    include/hydra/session_metrics.hpp
)
target_link_libraries(hydra_session_metrics PUBLIC hydra_input_metrics)
hydra_enable_strict_warnings(hydra_session_metrics)

# P6-SCHEMA-01 bounded versioned persistence/runtime schema family. This keeps
# Seat hardware, Player identity, Game metadata, two-player recipes, and
# temporary runtime bindings separate and contains no credential/script/runtime-handle fields.
add_library(hydra_profile_schema STATIC
    src/profile_schema.cpp
    include/hydra/profile_schema.hpp
)
hydra_enable_strict_warnings(hydra_profile_schema)

# P6-MIG-01 read-only legacy parser plus staged, byte-preserving backup and
# directory-level transactional commit/rollback for the separated v1 stores.
add_library(hydra_profile_migration STATIC
    src/profile_migration.cpp
    include/hydra/profile_migration.hpp
)
target_link_libraries(hydra_profile_migration PUBLIC hydra_profile_schema)
hydra_enable_strict_warnings(hydra_profile_migration)

# P6-CATALOG-01 provider-neutral, read-only candidate reconciliation into bounded
# stable GameRecords plus catalog-only icon/architecture/staleness metadata.
add_library(hydra_game_catalog STATIC
    src/game_catalog.cpp
    include/hydra/game_catalog.hpp
)
target_link_libraries(hydra_game_catalog PUBLIC hydra_profile_schema)
hydra_enable_strict_warnings(hydra_game_catalog)

# P6-PROV-01 bounded lawful launcher-provider boundary. Provider-specific I/O
# stays behind this contract; the core validates discovery, account references,
# typed launch requests, revision freshness, and process evidence transactionally.
add_library(hydra_provider_adapter STATIC
    src/provider_adapter.cpp
    include/hydra/provider_adapter.hpp
)
target_link_libraries(hydra_provider_adapter PUBLIC hydra_game_catalog)
hydra_enable_strict_warnings(hydra_provider_adapter)

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
hydra_enable_strict_warnings(hydra_custom_executable_provider)
hydra_enable_strict_warnings(hydra_steam_provider)

# P6-PLAN-01 pure immutable provider-aware plan compiler. It resolves bounded
# Seat/Player/Game/setup/provider snapshots and performs exact hardware/capability
# preflight without launching a process or mutating provider/system state.
add_library(hydra_provider_launch_plan STATIC
    src/provider_launch_plan.cpp
    include/hydra/provider_launch_plan.hpp
)
target_link_libraries(hydra_provider_launch_plan PUBLIC
    hydra_provider_adapter hydra_profile_schema hydra_two_seat_launch)
hydra_enable_strict_warnings(hydra_provider_launch_plan)

# P6-PREFLIGHT-01 deterministic normal/expert requirement, risk, and typed
# mutation preview. The preview surface cannot carry arbitrary mutation payloads.
add_library(hydra_plan_preflight STATIC
    src/plan_preflight.cpp
    include/hydra/plan_preflight.hpp
)
target_link_libraries(hydra_plan_preflight PUBLIC hydra_provider_launch_plan)
hydra_enable_strict_warnings(hydra_plan_preflight)

# P6-PROFILE-01 typed two-player setup generation/editor model. Automatic
# generation is pure/read-only, while manual Save is transactional and keeps the
# previous valid setup on validation failure.
add_library(hydra_two_player_setup_editor STATIC
    src/two_player_setup_editor.cpp
    include/hydra/two_player_setup_editor.hpp
)
target_link_libraries(hydra_two_player_setup_editor PUBLIC hydra_profile_schema)
hydra_enable_strict_warnings(hydra_two_player_setup_editor)

# P7-I18N-01 stable UI-string catalog. Protocol/schema/diagnostic identifiers stay
# canonical English while presentation strings have en-US/ko-KR/zh-CN entries.
add_library(hydra_ui_localization STATIC
    src/ui_localization.cpp
    src/ui_accessibility.cpp
    src/launcher_layout.cpp
    include/hydra/ui_localization.hpp
    include/hydra/ui_accessibility.hpp
    include/hydra/launcher_layout.hpp
)
hydra_enable_strict_warnings(hydra_ui_localization)

# P6-UI-01 Qt-independent product-flow model. UI state is exercised with fake
# providers and immutable snapshots; the model performs no launch or mutation.
add_library(hydra_launcher_ui_model STATIC
    src/launcher_ui_model.cpp
    include/hydra/launcher_ui_model.hpp
)
target_link_libraries(hydra_launcher_ui_model PUBLIC
    hydra_plan_preflight hydra_two_player_setup_editor hydra_game_catalog)
hydra_enable_strict_warnings(hydra_launcher_ui_model)

# V1 hands-on durable launcher user state. This owns only Player profile and
# last-selection persistence semantics; the Win32 launcher consumes the API.
add_library(hydra_launcher_user_state STATIC
    src/launcher_user_state.cpp
    include/hydra/launcher_user_state.hpp
)
target_link_libraries(hydra_launcher_user_state PUBLIC hydra_profile_schema)
hydra_enable_strict_warnings(hydra_launcher_user_state)

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
hydra_enable_strict_warnings(hydra_seat_launcher)

# P6-IMPORT-01 versioned portable two-player setup package with provenance,
# path-variable redaction, strict decode, explicit local remap, and revalidation.
add_library(hydra_setup_package STATIC
    src/setup_package.cpp
    include/hydra/setup_package.hpp
)
target_link_libraries(hydra_setup_package PUBLIC hydra_two_player_setup_editor)
hydra_enable_strict_warnings(hydra_setup_package)

# P6-CLI-01 deterministic expert renderer plus redacted stable plan snapshots.
add_library(hydra_profile_cli STATIC
    src/profile_cli.cpp
    include/hydra/profile_cli.hpp
)
target_link_libraries(hydra_profile_cli PUBLIC hydra_profile_schema hydra_provider_launch_plan)
hydra_enable_strict_warnings(hydra_profile_cli)

add_executable(hydraseat_profilectl src/profilectl_main.cpp)
target_link_libraries(hydraseat_profilectl PRIVATE hydra_profile_cli)
hydra_enable_strict_warnings(hydraseat_profilectl)

# P9-SDK-01 public data-only compatibility result boundary. It canonicalizes a
# bounded privacy-safe JSON schema and can derive it from Phase 5 session metrics.
add_library(hydra_compatibility_result STATIC
    src/compatibility_result.cpp
    src/compatibility_aggregation.cpp
    include/hydra/compatibility_result.hpp
    include/hydra/compatibility_aggregation.hpp
)
target_link_libraries(hydra_compatibility_result PUBLIC hydra_session_metrics)
hydra_enable_strict_warnings(hydra_compatibility_result)

# P6-PLAN-01/P7-LAUNCH-01 trusted local runtime-requirement authority. It resolves
# exact current catalog/provider/local-evidence revisions into the immutable
# GameRuntimeRequirement snapshot consumed by the launcher and never treats
# community popularity/imported results as runtime authority.
add_library(hydra_game_runtime_requirement_resolver STATIC
    src/game_runtime_requirement_resolver.cpp
    include/hydra/game_runtime_requirement_resolver.hpp
)
target_link_libraries(hydra_game_runtime_requirement_resolver PUBLIC
    hydra_compatibility_result
    hydra_game_catalog
    hydra_provider_launch_plan
)
target_link_libraries(hydra_game_runtime_requirement_resolver PRIVATE
    hydra_steam_provider
    hydra_custom_executable_provider
    hydra_community_submission
)
if(WIN32)
    target_link_libraries(hydra_game_runtime_requirement_resolver PRIVATE Bcrypt.lib)
endif()
hydra_enable_strict_warnings(hydra_game_runtime_requirement_resolver)

# P8-TRUST-01 pure optional artifact/data trust evaluator. It separates data-only
# hashes/provenance/license policy from executable architecture/signature/recovery policy.
add_library(hydra_artifact_trust STATIC
    src/artifact_trust.cpp
    include/hydra/artifact_trust.hpp
)
hydra_enable_strict_warnings(hydra_artifact_trust)

# P9-PKG-01 data-only community package manifest/lifecycle. Package entries are
# compatibility results, TwoPlayerSetup data, or catalog snapshots only.
add_library(hydra_community_package STATIC
    src/community_package.cpp
    src/community_setup.cpp
    src/compatibility_catalog.cpp
    include/hydra/community_package.hpp
    include/hydra/community_setup.hpp
    include/hydra/compatibility_catalog.hpp
)
target_link_libraries(hydra_community_package PUBLIC
    hydra_artifact_trust hydra_compatibility_result hydra_setup_package
    hydra_provider_launch_plan)
hydra_enable_strict_warnings(hydra_community_package)

# P8-DATA-01 pure optional compatibility/setup catalog cache lifecycle. Network/file
# code supplies observations; offline/disabled/tampered refreshes preserve local state.
add_library(hydra_catalog_cache STATIC
    src/catalog_cache.cpp
    include/hydra/catalog_cache.hpp
)
target_link_libraries(hydra_catalog_cache PUBLIC hydra_artifact_trust)
hydra_enable_strict_warnings(hydra_catalog_cache)

# P9-TEST-01 contributor-facing conformance CLI. It validates/canonicalizes the
# public compatibility result or portable setup package without networking.
add_executable(hydraseat_community_validate src/community_validate_main.cpp)
target_link_libraries(hydraseat_community_validate PRIVATE
    hydra_compatibility_result hydra_setup_package)
hydra_enable_strict_warnings(hydraseat_community_validate)

# P8-WATCH-01 versioned lease/rollback protocol and idempotent rollback registry.
# The portable core contains no arbitrary command execution or persistent-state I/O.
add_library(hydra_watchdog_core STATIC
    src/watchdog_protocol.cpp
    src/rollback_registry.cpp
    include/hydra/watchdog_protocol.hpp
    include/hydra/rollback_registry.hpp
)
hydra_enable_strict_warnings(hydra_watchdog_core)

# P3-D-02 durable recovery record + narrow RestoreSnapshotState executor. The
# watchdog/reset path restores only a numeric resource ID from the bounded
# HydraSeat recovery root and refuses stale external-state overwrite.
add_library(hydra_hidhide_session_recovery STATIC
    src/hidhide_session_recovery.cpp
    include/hydra/hidhide_session_recovery.hpp
)
target_link_libraries(hydra_hidhide_session_recovery PUBLIC
    hydra_hidhide_session hydra_watchdog_core)
hydra_enable_strict_warnings(hydra_hidhide_session_recovery)

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
hydra_enable_strict_warnings(hydra_audio_inventory)

# P5-AUD-02 exact-owned audio session observation and typed route transaction.
# The native Windows implementation is observation-only until a documented,
# reversible routing backend is selected; no private audio-policy COM API is used.
add_library(hydra_audio_routing STATIC
    src/audio_routing.cpp
    src/audio_session_native.cpp
    include/hydra/audio_routing.hpp
)
target_link_libraries(hydra_audio_routing PUBLIC hydra_audio_inventory)
hydra_enable_strict_warnings(hydra_audio_routing)

# P10-RC-01 acceptance evidence orchestration. The campaign core never turns
# synthetic/controlled evidence into physical validation and binds every child
# record to one exact RC artifact/scenario identity.
add_library(hydra_acceptance_campaign STATIC
    src/acceptance_campaign.cpp
    include/hydra/acceptance_campaign.hpp
)
hydra_enable_strict_warnings(hydra_acceptance_campaign)

add_executable(hydraseat_acceptance_campaignctl
    src/acceptance_campaignctl_main.cpp)
target_link_libraries(hydraseat_acceptance_campaignctl PRIVATE
    hydra_acceptance_campaign)
hydra_enable_strict_warnings(hydraseat_acceptance_campaignctl)

# Read-only installed-RC acceptance probe. It verifies exact owned artifacts,
# observes exact process identities/resources, and inventories current physical
# endpoints without becoming a mutation or release-validation authority.
add_library(hydra_acceptance_probe STATIC
    src/acceptance_probe.cpp
    include/hydra/acceptance_probe.hpp
)
target_link_libraries(hydra_acceptance_probe PUBLIC
    hydra_audio_inventory
    hydra_controller_runtime
    hydra_display_topology
)
if(WIN32)
    target_link_libraries(hydra_acceptance_probe PRIVATE
        Bcrypt.lib Wintrust.lib Psapi.lib)
endif()
hydra_enable_strict_warnings(hydra_acceptance_probe)

add_executable(hydraseat_acceptance_probe
    src/acceptance_probe_main.cpp)
target_link_libraries(hydraseat_acceptance_probe PRIVATE hydra_acceptance_probe)
hydra_enable_strict_warnings(hydraseat_acceptance_probe)

# Typed compatibility lifecycle adapter. It owns the implementation once and is
# consumed by the production launch runtime; recipe compilation remains in the
# existing provider-plan/materialization authority.
add_library(hydra_production_compatibility_activation STATIC
    src/production_compatibility_activation.cpp
    include/hydra/production_compatibility_activation.hpp
)
target_link_libraries(hydra_production_compatibility_activation PUBLIC
    hydra_provider_launch_plan
    hydra_two_seat_launch
)
hydra_enable_strict_warnings(hydra_production_compatibility_activation)

# P5-LAUNCH-01/P7-LAUNCH-01 host-owned immutable provider-plan registry and
# production Seat activation factory. It reuses the existing process/window/
# display/controller/audio transaction boundaries and fails closed when a required
# production input bridge or other mutation capability is unavailable.
add_library(hydra_production_launch_runtime STATIC
    src/production_launch_runtime.cpp
    include/hydra/production_launch_runtime.hpp
)
target_link_libraries(hydra_production_launch_runtime PUBLIC
    hydra_two_seat_launch
    hydra_provider_launch_plan
    hydra_window_placement
    hydra_controller_runtime
    hydra_audio_routing
)
target_link_libraries(hydra_production_launch_runtime PRIVATE
    hydra_production_compatibility_activation)
hydra_enable_strict_warnings(hydra_production_launch_runtime)

# P7-LAUNCH-01 narrow UI/Seat-client -> authoritative Host provider-plan
# transaction. The installer resnapshots the Host and never launches a target
# itself; rollback removes only the exact idle Seat plan epoch it installed.
add_library(hydra_production_launch_installer STATIC
    src/production_launch_installer.cpp
    include/hydra/production_launch_installer.hpp
)
target_link_libraries(hydra_production_launch_installer PUBLIC
    hydra_seat_launcher
    hydra_production_launch_runtime
)
hydra_enable_strict_warnings(hydra_production_launch_installer)

# V1 end-user setup bootstrapper. The native UI does not reimplement installer
# mutation authority; it invokes the signed transactional PowerShell contract.
add_library(hydra_installer_bootstrap STATIC
    src/installer_bootstrap.cpp
    include/hydra/installer_bootstrap.hpp
)
hydra_enable_strict_warnings(hydra_installer_bootstrap)
if(WIN32)
    target_link_libraries(hydra_installer_bootstrap PRIVATE
        Wintrust.lib Crypt32.lib Shell32.lib Advapi32.lib)
endif()

# HydraSeat v1 ships one x64 host/setup package. Win32 builds remain target-
# process compatibility evidence and therefore do not produce a second setup EXE.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(HydraSeatSetup WIN32 src/installer_bootstrap_main.cpp)
    target_link_libraries(HydraSeatSetup PRIVATE hydra_installer_bootstrap User32.lib)
    hydra_enable_strict_warnings(HydraSeatSetup)
endif()

# Host protocol v4 serializes and validates production provider-plan registry
# requests/results, including host-side fingerprint revalidation.
target_link_libraries(hydra_host_protocol PUBLIC hydra_production_launch_runtime)

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
hydra_enable_strict_warnings(hydra_crash_journal)

# P8-DIAG-01 privacy-redacted support bundle. It accepts only already-bounded
# public/aggregate evidence and binds export approval to the exact preview bytes.
add_library(hydra_support_bundle STATIC
    src/support_bundle.cpp
    include/hydra/support_bundle.hpp
)
target_link_libraries(hydra_support_bundle PUBLIC
    hydra_compatibility_result hydra_crash_journal hydra_session_metrics)
hydra_enable_strict_warnings(hydra_support_bundle)

# P8-BOOT-01 user-approved startup policy and transactional fixed-target registration.
add_library(hydra_startup_policy STATIC
    src/startup_policy.cpp
    include/hydra/startup_policy.hpp
)
hydra_enable_strict_warnings(hydra_startup_policy)

# P8-PRIV-01 narrow typed elevated broker core. The unelevated caller can name
# only compiled resources/operations; broker-owned trust plus capture/verify/rollback
# prevent this boundary from becoming arbitrary administrator execution.
add_library(hydra_privilege_broker STATIC
    src/privilege_broker.cpp
    include/hydra/privilege_broker.hpp
)
target_link_libraries(hydra_privilege_broker PUBLIC hydra_artifact_trust)
hydra_enable_strict_warnings(hydra_privilege_broker)

# P8-INST-01 fixed owned-component installer transaction core. Native packaging
# resolves Program Files/ProgramData/uninstall locations; the core handles only
# trusted component enums, startup choice, verification and rollback.
add_library(hydra_installer_transaction STATIC
    src/installer_transaction.cpp
    include/hydra/installer_transaction.hpp
)
target_link_libraries(hydra_installer_transaction PUBLIC
    hydra_artifact_trust hydra_startup_policy)
hydra_enable_strict_warnings(hydra_installer_transaction)

# P8-UPD-01 user-approved executable update/rollback contract layered over the
# fixed trusted installer transaction. Compatibility/setup data refresh remains P8-DATA.
add_library(hydra_update_transaction STATIC
    src/update_transaction.cpp
    include/hydra/update_transaction.hpp
)
target_link_libraries(hydra_update_transaction PUBLIC hydra_installer_transaction)
hydra_enable_strict_warnings(hydra_update_transaction)

# P9-RPC-01 explicit opt-in community submission boundary. Local evidence is
# canonicalized first; transport invocation requires approval of the exact JSON preview.
add_library(hydra_community_submission STATIC
    src/community_submission.cpp
    src/compatibility_share_model.cpp
    src/compatibility_local_store.cpp
    include/hydra/community_submission.hpp
    include/hydra/compatibility_share_model.hpp
    include/hydra/compatibility_local_store.hpp
)
target_link_libraries(hydra_community_submission PUBLIC hydra_compatibility_result)
hydra_enable_strict_warnings(hydra_community_submission)

# V1 first-use Play authority closure. These modules deliberately keep evidence
# collection, evidence persistence, and requirement publication as separate
# authorities so ControlledProcess observations can never silently become
# Physical normal-Play authority.
add_library(hydra_local_compatibility_evidence STATIC
    src/local_compatibility_evidence.cpp
    include/hydra/local_compatibility_evidence.hpp
)
target_link_libraries(hydra_local_compatibility_evidence PUBLIC
    hydra_community_submission)
hydra_enable_strict_warnings(hydra_local_compatibility_evidence)

add_library(hydra_runtime_requirement_authority STATIC
    src/runtime_requirement_authority.cpp
    include/hydra/runtime_requirement_authority.hpp
)
target_link_libraries(hydra_runtime_requirement_authority PUBLIC
    hydra_game_runtime_requirement_resolver)
hydra_enable_strict_warnings(hydra_runtime_requirement_authority)

add_library(hydra_local_compatibility_runner STATIC
    src/local_compatibility_runner.cpp
    include/hydra/local_compatibility_runner.hpp
)
target_link_libraries(hydra_local_compatibility_runner PUBLIC
    hydra_process hydra_window_tracking hydra_session_metrics)
hydra_enable_strict_warnings(hydra_local_compatibility_runner)

# P8-RESET-01 bounded runtime reset registration and verified emergency-reset
# execution. The crash journal remains evidence-only; reset authority is the
# journal-correlated exact watchdog manifest plus exact runtime-owner identity.
add_library(hydra_reset_actions STATIC
    src/reset_actions.cpp
    include/hydra/reset_actions.hpp
)
target_link_libraries(hydra_reset_actions PUBLIC
    hydra_crash_journal hydra_watchdog_core)
hydra_enable_strict_warnings(hydra_reset_actions)

# P3-REC-01 Gate C recovery contract. The trusted rollback manifest contains
# only exact controlled-process termination identities; process-local shim and
# adapter state disappears with the owned target process.
add_library(hydra_gate_c_recovery STATIC
    src/gate_c_recovery.cpp
    include/hydra/gate_c_recovery.hpp
)
target_link_libraries(hydra_gate_c_recovery PUBLIC
    hydra_crash_journal hydra_watchdog_core)
hydra_enable_strict_warnings(hydra_gate_c_recovery)

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

# Production Physical-input authority selection. The per-user store persists only
# a path to an already accepted P3-HW manifest; every load revalidates the typed
# evidence. Real-game Gate-C profiles remain release-owned and fail closed while
# P3-E-02 has no reviewed profile.
add_library(hydra_production_input_authority STATIC
    src/production_input_authority.cpp
    include/hydra/production_input_authority.hpp
)
target_link_libraries(hydra_production_input_authority PUBLIC hydra_hidhide_session)
hydra_enable_strict_warnings(hydra_production_input_authority)

# Production Recovery/Input bridge composition. The implementation reuses the
# existing exact-process recovery, Gate-C, HidHide, and Raw Input authorities;
# hydra_host reaches it through this single canonical production owner.
add_library(hydra_production_activation_bridges STATIC
    src/production_activation_bridges.cpp
    include/hydra/production_activation_bridges.hpp
)
target_link_libraries(hydra_production_activation_bridges PUBLIC
    hydra_production_launch_runtime
)
target_link_libraries(hydra_production_activation_bridges PRIVATE
    hydra_production_input_authority
    hydra_input_router
    hydra_input_metrics
    hydra_hidhide_session
    hydra_hidhide_session_recovery
    hydra_watchdog_core
    hydra_crash_journal
    hydra_reset_actions
    hydra_gate_c_recovery
    hydra_gate_c_core
    Threads::Threads
)
if(WIN32)
    target_link_libraries(hydra_production_activation_bridges PRIVATE User32.lib)
endif()
hydra_enable_strict_warnings(hydra_production_activation_bridges)
# HostControlServer is the production composition root for the same bridge; keep
# the dependency explicit so GUI/CLI consumers of host_transport cannot rely on a
# final executable accidentally supplying these symbols.
target_link_libraries(hydra_host_transport PRIVATE hydra_production_activation_bridges)

# P3-RAW-01 fixed-width, versioned, bounded trace/parser contract. This
# library is portable and contains no Raw Input virtualization.
add_library(hydra_raw_input_probe_trace STATIC
    src/raw_input_probe_trace.cpp
    include/hydra/raw_input_probe_trace.hpp
)
hydra_enable_strict_warnings(hydra_raw_input_probe_trace)
hydra_enable_strict_warnings(hydra_gate_c_core)

# P3-RAW-02 bounded process-local registration, synthetic-handle, packet, and
# queue model. Win32 API interposition remains in the startup-loaded shim.
add_library(hydra_virtual_raw_input STATIC
    src/virtual_raw_input.cpp
    include/hydra/virtual_raw_input.hpp
)
set_target_properties(hydra_virtual_raw_input PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
hydra_enable_strict_warnings(hydra_virtual_raw_input)

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
hydra_enable_strict_warnings(hydra_gate_c_adapter)

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
hydra_enable_strict_warnings(hydra_gate_c_shim_support)

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
hydra_enable_strict_warnings(hydra_gate_c_shim)

# Production Gate-C receiver loaded only by the exact-process production
# activation runtime. Keep the injected DLL in its own production source so the
# Host bridge and receiver have one unambiguous owner each.
if(WIN32)
    add_library(hydra_production_gate_c_bridge SHARED
        src/production_gate_c_bridge.cpp
    )
    target_link_libraries(hydra_production_gate_c_bridge PRIVATE
        hydra_gate_c_core hydra_gate_c_adapter hydra_gate_c_shim User32.lib
    )
    set_target_properties(hydra_production_gate_c_bridge PROPERTIES
        OUTPUT_NAME hydra_production_gate_c_bridge
    )
    hydra_set_gate_c_output(hydra_production_gate_c_bridge)
    hydra_enable_strict_warnings(hydra_production_gate_c_bridge)
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
    hydra_enable_strict_warnings(hydra_gate_c_external_bridge)

    add_executable(hydra_gate_c_external_harness
        src/gate_c_external_harness.cpp
        include/hydra/gate_c_external_profile.hpp
    )
    target_link_libraries(hydra_gate_c_external_harness PRIVATE
        hydra_gate_c_core User32.lib
    )
    hydra_set_gate_c_output(hydra_gate_c_external_harness)
    hydra_enable_strict_warnings(hydra_gate_c_external_harness)
    add_test(NAME GateCExternalHarnessHelp
        COMMAND hydra_gate_c_external_harness --help)
endif()

# Core Executable target (Windows GUI subsystem)
add_executable(HydraSeat WIN32 src/main.cpp ${HYDRA_SOURCES} ${HYDRA_HEADERS})
target_link_libraries(HydraSeat PRIVATE
    hydra_hardware hydra_input_router hydra_workspace hydra_host_transport hydra_control_surface hydra_session_control
    hydra_launcher_ui_model hydra_launcher_user_state hydra_steam_provider hydra_custom_executable_provider
    hydra_ui_localization hydra_community_submission
    hydra_game_runtime_requirement_resolver hydra_local_compatibility_evidence
    hydra_runtime_requirement_authority hydra_local_compatibility_runner
    hydra_production_input_authority hydra_production_launch_installer)

if(WIN32)
    target_link_libraries(HydraSeat PRIVATE ${WIN32_LIBS})
endif()

# P4-RUN-01 headless per-user runtime authority. It deliberately has no Qt or
# GUI dependency; P4-IPC-01 will add the long-lived local control transport.
add_executable(hydra_host
    src/host_main.cpp
)
target_link_libraries(hydra_host PRIVATE
    hydra_workspace hydra_runtime hydra_host_transport
    hydra_game_runtime_requirement_resolver hydra_production_activation_bridges)
hydra_enable_strict_warnings(hydra_host)
if(WIN32)
    # The normal GUI bootstraps the per-user runtime authority on first run.
    # Keep the Host and its architecture-matched production receiver present
    # whenever HydraSeat itself is built.
    add_dependencies(hydra_host hydra_production_gate_c_bridge)
    add_dependencies(HydraSeat hydra_host)
endif()

add_executable(hydra_hostctl src/hostctl_main.cpp)
target_link_libraries(hydra_hostctl PRIVATE hydra_host_transport)
hydra_enable_strict_warnings(hydra_hostctl)

# P7-SHELL-01 minimal per-Seat Windows launcher. It can issue only Seat-local
# game lifecycle commands through hydra_seat_launcher and remains hidden until
# its authoritative display group resolves against a fresh topology snapshot.
add_executable(hydra_seat_ui WIN32 src/seat_ui_main.cpp)
target_link_libraries(hydra_seat_ui PRIVATE
    hydra_seat_launcher hydra_seat_display_layout hydra_ui_localization)
if(WIN32)
    target_link_libraries(hydra_seat_ui PRIVATE User32.lib Shell32.lib)
endif()
hydra_enable_strict_warnings(hydra_seat_ui)

# P8-RESET-01 independently launchable emergency reset tool. It depends only on
# recovery/watchdog core libraries and never on Qt or the main control UI.
add_executable(hydra_reset src/reset_main.cpp)
target_link_libraries(hydra_reset PRIVATE
    hydra_reset_actions hydra_hidhide_session_recovery)
hydra_enable_strict_warnings(hydra_reset)
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
hydra_enable_strict_warnings(hydra_detect)

# Phase 3 capability planner diagnostics (analysis only; activates no backend).
add_executable(hydra_plan
    src/isolation_plan_cli.cpp
    src/input_isolation.cpp
    include/hydra/input_isolation.hpp
)
target_link_libraries(hydra_plan PRIVATE hydra_workspace)
hydra_enable_strict_warnings(hydra_plan)

# P3-D-01 bounded read-only diagnostics CLI. It never requests elevation and
# returns an observational report even when HidHide is absent or unverified.
add_executable(hydra_hidhide_probe_cli
    src/hidhide_probe_cli.cpp
)
set_target_properties(hydra_hidhide_probe_cli PROPERTIES
    OUTPUT_NAME hydra_hidhide_probe
)
target_link_libraries(hydra_hidhide_probe_cli PRIVATE hydra_hidhide_probe)
hydra_enable_strict_warnings(hydra_hidhide_probe_cli)

# P3-D-02 guarded lab exposes only read-only status and synthetic self-test
# while physical acceptance is deferred. No native cloak command is available.
add_executable(hydra_hidhide_session_lab
    src/hidhide_session_lab.cpp
)
target_link_libraries(hydra_hidhide_session_lab PRIVATE
    hydra_hidhide_probe hydra_hidhide_session)
hydra_enable_strict_warnings(hydra_hidhide_session_lab)

# P5-AUD-01 read-only endpoint diagnostic used for eventual physical evidence.
# It cannot change default devices, volume, or per-process routing.
add_executable(hydra_audio_diag src/audio_diag_main.cpp)
target_link_libraries(hydra_audio_diag PRIVATE hydra_audio_routing)
hydra_enable_strict_warnings(hydra_audio_diag)

# P8-WATCH-01 independent rollback-only watchdog executable. The host-side
# launcher is intentionally deferred to the owning runtime/recovery packet.
add_executable(hydra_watchdog
    src/watchdog_main.cpp
)
target_link_libraries(hydra_watchdog PRIVATE
    hydra_watchdog_core hydra_hidhide_session_recovery)
hydra_enable_strict_warnings(hydra_watchdog)
if(WIN32)
    add_dependencies(hydra_host hydra_watchdog)
endif()
if(MINGW)
    target_link_options(hydra_watchdog PRIVATE -municode)
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
hydra_enable_strict_warnings(hydra_input_metrics_cli)

# Phase 3 Gate A/B observation and explicit-routing harness.
# The interactive windows are Windows-only, while --self-test is portable.
add_executable(hydra_input_lab
    src/input_lab.cpp
    src/input_observation.cpp
    src/input_isolation.cpp
    include/hydra/input_router.hpp
    include/hydra/input_observation.hpp
    include/hydra/input_isolation.hpp
)
target_link_libraries(hydra_input_lab PRIVATE
    hydra_hardware hydra_input_router hydra_workspace)
if(WIN32)
    target_link_libraries(hydra_input_lab PRIVATE ${WIN32_LIBS})
endif()
hydra_enable_strict_warnings(hydra_input_lab)

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
hydra_enable_strict_warnings(hydra_gate_c_target)

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
hydra_enable_strict_warnings(hydra_gate_c_raw_input_probe)
hydra_enable_strict_warnings(hydra_gate_c_api_probe)

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
hydra_enable_strict_warnings(hydra_directinput_probe)

# P5-CTRL-01 read-only native controller inventory diagnostic. It never sends
# vibration and explicitly labels XInput slots as runtime-only identities.
add_executable(hydra_controller_diag src/controller_diag_main.cpp)
target_link_libraries(hydra_controller_diag PRIVATE hydra_controller_runtime)
hydra_enable_strict_warnings(hydra_controller_diag)

# Phase 3 Gate C host: launches only the controlled target executable and
# routes Seat-owned Raw Input observations over the versioned named-pipe protocol.
add_executable(hydra_gate_c_host
    src/gate_c_host.cpp
    src/input_observation.cpp
    src/input_isolation.cpp
    include/hydra/input_router.hpp
    include/hydra/input_observation.hpp
    include/hydra/input_isolation.hpp
)
target_link_libraries(hydra_gate_c_host PRIVATE
    hydra_gate_c_core hydra_gate_c_recovery hydra_reset_actions
    hydra_hardware hydra_input_router hydra_workspace hydra_input_metrics)
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
hydra_enable_strict_warnings(hydra_gate_c_host)

# Test Suite Executable Target
enable_testing()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(WIN32)
    find_program(HYDRA_POWERSHELL_EXECUTABLE NAMES powershell.exe powershell)
endif()
add_executable(hydra_tests tests/test_hydra.cpp ${HYDRA_SOURCES} ${HYDRA_HEADERS})
target_link_libraries(hydra_tests PRIVATE
    hydra_hardware hydra_input_router hydra_workspace hydra_host_transport hydra_control_surface hydra_session_control
    hydra_launcher_ui_model hydra_launcher_user_state hydra_steam_provider hydra_custom_executable_provider
    hydra_ui_localization hydra_community_submission hydra_game_runtime_requirement_resolver
    hydra_local_compatibility_evidence hydra_local_compatibility_runner hydra_runtime_requirement_authority
    hydra_production_input_authority)
if(WIN32)
    target_link_libraries(hydra_tests PRIVATE ${WIN32_LIBS})
endif()
add_test(NAME EngineUnitTests COMMAND hydra_tests)

# P4-RUN-01 pure runtime transition/rollback/concurrency tests and controlled
# headless process self-test. No hardware or persistent system state is touched.
add_executable(runtime_host_tests tests/test_runtime_host.cpp)
target_link_libraries(runtime_host_tests PRIVATE hydra_runtime Threads::Threads)
hydra_enable_strict_warnings(runtime_host_tests)
add_test(NAME RuntimeHostTests COMMAND runtime_host_tests)

# P4-SEAT-01 authoritative per-Seat game lifecycle model. These tests use
# deterministic owned-instance fakes and prove independent start/stop/restart,
# failure isolation, duplicate/concurrent command rejection, and the v1 limit.
add_executable(seat_game_lifecycle_tests tests/test_seat_game_lifecycle.cpp)
target_link_libraries(seat_game_lifecycle_tests PRIVATE hydra_runtime Threads::Threads)
hydra_enable_strict_warnings(seat_game_lifecycle_tests)
add_test(NAME SeatGameLifecycleTests COMMAND seat_game_lifecycle_tests)

# P5-LAUNCH-01 immutable plan compiler, all-resource activation failure-index
# rollback, natural-exit Seat-local cleanup, and RuntimeHost two-Seat isolation.
add_executable(two_seat_launch_tests tests/test_two_seat_launch.cpp)
target_link_libraries(two_seat_launch_tests PRIVATE hydra_two_seat_launch hydra_runtime)
hydra_enable_strict_warnings(two_seat_launch_tests)
add_test(NAME TwoSeatLaunchTests COMMAND two_seat_launch_tests)
set_tests_properties(TwoSeatLaunchTests PROPERTIES LABELS "P5-LAUNCH-01;P4-SEAT-01")

add_test(NAME RuntimeHostProcessSelfTest COMMAND hydra_host --self-test)

add_executable(host_protocol_tests tests/test_host_protocol.cpp)
target_link_libraries(host_protocol_tests PRIVATE hydra_host_protocol)
hydra_enable_strict_warnings(host_protocol_tests)
add_test(NAME HostProtocolTests COMMAND host_protocol_tests)
set_tests_properties(HostProtocolTests PROPERTIES LABELS "P4-IPC-01;P7-SHELL-01")

add_executable(host_ipc_process_tests tests/test_host_process.cpp)
target_link_libraries(host_ipc_process_tests PRIVATE hydra_host_transport)
add_dependencies(host_ipc_process_tests hydra_host hydra_hostctl)
hydra_enable_strict_warnings(host_ipc_process_tests)
add_test(NAME HostIpcProcessTests COMMAND host_ipc_process_tests)
set_tests_properties(HostIpcProcessTests PROPERTIES
    LABELS "P4-IPC-01;P4-SEAT-01;P7-SHELL-01")

add_executable(seat_launcher_model_tests tests/test_seat_launcher_model.cpp)
target_link_libraries(seat_launcher_model_tests PRIVATE hydra_seat_launcher)
hydra_enable_strict_warnings(seat_launcher_model_tests)
add_test(NAME SeatLauncherModelTests COMMAND seat_launcher_model_tests)
set_tests_properties(SeatLauncherModelTests PROPERTIES LABELS "P7-SHELL-01")

add_executable(seat_launch_flow_tests tests/test_seat_launch_flow.cpp)
target_link_libraries(seat_launch_flow_tests PRIVATE hydra_seat_launcher)
hydra_enable_strict_warnings(seat_launch_flow_tests)
add_test(NAME SeatLaunchFlowTests COMMAND seat_launch_flow_tests)
set_tests_properties(SeatLaunchFlowTests PROPERTIES LABELS "P7-LAUNCH-01")

add_executable(seat_notification_model_tests
    tests/test_seat_notification_model.cpp)
target_link_libraries(seat_notification_model_tests PRIVATE hydra_seat_launcher)
hydra_enable_strict_warnings(seat_notification_model_tests)
add_test(NAME SeatNotificationModelTests COMMAND seat_notification_model_tests)
set_tests_properties(SeatNotificationModelTests PROPERTIES LABELS "P7-NOTIFY-01")

add_executable(seat_hotkey_model_tests tests/test_seat_hotkey_model.cpp)
target_link_libraries(seat_hotkey_model_tests PRIVATE hydra_seat_launcher)
hydra_enable_strict_warnings(seat_hotkey_model_tests)
add_test(NAME SeatHotkeyModelTests COMMAND seat_hotkey_model_tests)
set_tests_properties(SeatHotkeyModelTests PROPERTIES LABELS "P7-HOTKEY-01")

add_executable(seat_navigation_model_tests tests/test_seat_navigation_model.cpp)
target_link_libraries(seat_navigation_model_tests PRIVATE hydra_seat_launcher)
hydra_enable_strict_warnings(seat_navigation_model_tests)
add_test(NAME SeatNavigationModelTests COMMAND seat_navigation_model_tests)
set_tests_properties(SeatNavigationModelTests PROPERTIES LABELS "P7-CURSOR-01")

add_executable(ui_localization_tests tests/test_ui_localization.cpp)
target_link_libraries(ui_localization_tests PRIVATE hydra_ui_localization)
hydra_enable_strict_warnings(ui_localization_tests)
add_test(NAME UiLocalizationTests COMMAND ui_localization_tests)
set_tests_properties(UiLocalizationTests PROPERTIES LABELS "P7-I18N-01")

add_executable(ui_accessibility_tests tests/test_ui_accessibility.cpp)
target_link_libraries(ui_accessibility_tests PRIVATE hydra_ui_localization)
hydra_enable_strict_warnings(ui_accessibility_tests)
add_test(NAME UiAccessibilityTests COMMAND ui_accessibility_tests)
set_tests_properties(UiAccessibilityTests PROPERTIES LABELS "P7-A11Y-01")

if(WIN32)
    add_executable(seat_launch_flow_process_tests
        tests/test_seat_launch_flow_process.cpp)
    target_link_libraries(seat_launch_flow_process_tests PRIVATE
        hydra_seat_launcher hydra_process hydra_runtime)
    hydra_enable_strict_warnings(seat_launch_flow_process_tests)
    add_test(NAME SeatLaunchFlowProcessTests COMMAND seat_launch_flow_process_tests)
    set_tests_properties(SeatLaunchFlowProcessTests PROPERTIES
        LABELS "P7-LAUNCH-01")
endif()

if(WIN32)
    add_executable(seat_ui_process_tests tests/test_seat_ui_process.cpp)
    target_link_libraries(seat_ui_process_tests PRIVATE hydra_host_transport)
    add_dependencies(seat_ui_process_tests hydra_host hydra_seat_ui)
    hydra_enable_strict_warnings(seat_ui_process_tests)
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
hydra_enable_strict_warnings(hydra_process_tree_child)

if(WIN32)
    # P5-MVP-01 complete controlled two-Seat production-path harness. It uses
    # real owned Job Object process trees while keeping display/input/audio/controller
    # evidence explicitly synthetic until physical validation is performed.
    add_executable(hydra_two_seat_mvp_lab src/two_seat_mvp_lab.cpp)
    target_link_libraries(hydra_two_seat_mvp_lab PRIVATE
        hydra_two_seat_launch hydra_session_metrics hydra_runtime hydra_process
        Threads::Threads)
    add_dependencies(hydra_two_seat_mvp_lab hydra_process_tree_child)
    hydra_enable_strict_warnings(hydra_two_seat_mvp_lab)
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
    hydra_enable_strict_warnings(seat_game_process_lifecycle_tests)
    add_test(NAME SeatGameProcessLifecycleTests
             COMMAND seat_game_process_lifecycle_tests)
    set_tests_properties(SeatGameProcessLifecycleTests PROPERTIES
        TIMEOUT 45
        LABELS "P4-SEAT-01;P4-REC-01"
        RESOURCE_LOCK "HydraSeatHostPipe")
endif()

add_executable(process_group_tests tests/test_process_group.cpp)
target_link_libraries(process_group_tests PRIVATE hydra_process)
add_dependencies(process_group_tests hydra_process_tree_child)
hydra_enable_strict_warnings(process_group_tests)
add_test(NAME ProcessGroupTests COMMAND process_group_tests)

add_executable(hydra_window_test_app tests/window_test_app.cpp)
hydra_enable_strict_warnings(hydra_window_test_app)

add_executable(window_tracker_tests tests/test_window_tracker.cpp)
target_link_libraries(window_tracker_tests PRIVATE hydra_window_tracking)
add_dependencies(window_tracker_tests hydra_window_test_app)
if(WIN32)
    target_link_libraries(window_tracker_tests PRIVATE User32.lib)
endif()
hydra_enable_strict_warnings(window_tracker_tests)
add_test(NAME WindowTrackerTests COMMAND window_tracker_tests)

add_executable(hydra_display_diag src/display_diag_main.cpp)
target_link_libraries(hydra_display_diag PRIVATE hydra_display_topology)
hydra_enable_strict_warnings(hydra_display_diag)

add_executable(display_topology_tests tests/test_display_topology.cpp)
target_link_libraries(display_topology_tests PRIVATE hydra_display_topology)
hydra_enable_strict_warnings(display_topology_tests)
add_test(NAME DisplayTopologyTests COMMAND display_topology_tests)

add_executable(seat_display_layout_tests tests/test_seat_display_layout.cpp)
target_link_libraries(seat_display_layout_tests PRIVATE hydra_seat_display_layout)
hydra_enable_strict_warnings(seat_display_layout_tests)
add_test(NAME SeatDisplayLayoutTests COMMAND seat_display_layout_tests)

add_executable(window_policy_tests tests/test_window_policy.cpp)
target_link_libraries(window_policy_tests PRIVATE hydra_window_placement)
hydra_enable_strict_warnings(window_policy_tests)
add_test(NAME WindowPolicyTests COMMAND window_policy_tests)

add_executable(seat_runtime_policy_tests tests/test_seat_runtime_policy.cpp)
target_link_libraries(seat_runtime_policy_tests PRIVATE hydra_seat_runtime_policy)
hydra_enable_strict_warnings(seat_runtime_policy_tests)
add_test(NAME SeatRuntimePolicyTests COMMAND seat_runtime_policy_tests)

add_executable(display_recovery_tests tests/test_display_recovery.cpp)
target_link_libraries(display_recovery_tests PRIVATE hydra_display_recovery)
hydra_enable_strict_warnings(display_recovery_tests)
add_test(NAME DisplayRecoveryTests COMMAND display_recovery_tests)

add_executable(management_seat_tests tests/test_management_seat.cpp)
target_link_libraries(management_seat_tests PRIVATE hydra_management_seat)
hydra_enable_strict_warnings(management_seat_tests)
add_test(NAME ManagementSeatTests COMMAND management_seat_tests)

add_executable(session_control_transition_tests tests/test_session_control_transition.cpp)
target_link_libraries(session_control_transition_tests PRIVATE hydra_session_control)
hydra_enable_strict_warnings(session_control_transition_tests)
add_test(NAME SessionControlTransitionTests COMMAND session_control_transition_tests)

add_executable(control_surface_model_tests tests/test_control_surface_model.cpp)
target_link_libraries(control_surface_model_tests PRIVATE hydra_control_surface)
hydra_enable_strict_warnings(control_surface_model_tests)
add_test(NAME ControlSurfaceModelTests COMMAND control_surface_model_tests)
set_tests_properties(ControlSurfaceModelTests PROPERTIES
    LABELS "P4-CTRL-01;P4-REC-01;P4-SEAT-01")

# Pure Phase 1 identity checks: deterministic and independent of attached hardware.
add_executable(hardware_identity_tests
    tests/test_hardware_identity.cpp
    include/hydra/hardware_identity.hpp
)
hydra_enable_strict_warnings(hardware_identity_tests)
add_test(NAME HardwareIdentityTests COMMAND hardware_identity_tests)

# Pure Phase 3 capability-planner tests. No process injection or driver access.
add_executable(isolation_planner_tests
    tests/test_isolation_planner.cpp
    src/input_isolation.cpp
    include/hydra/input_isolation.hpp
)
target_link_libraries(isolation_planner_tests PRIVATE hydra_workspace)
hydra_enable_strict_warnings(isolation_planner_tests)
add_test(NAME IsolationPlannerTests COMMAND isolation_planner_tests)
add_test(NAME IsolationPlanObservation COMMAND hydra_plan observation-harness)

add_executable(seat_assignment_projection_tests
    tests/test_seat_assignment_projection.cpp)
target_link_libraries(seat_assignment_projection_tests PRIVATE hydra_workspace)
hydra_enable_strict_warnings(seat_assignment_projection_tests)
add_test(NAME SeatAssignmentProjectionTests COMMAND seat_assignment_projection_tests)

# P3-D-01 platform-independent policy/failure tests plus a bounded CLI test.
add_executable(hidhide_probe_tests
    tests/test_hidhide_probe.cpp
)
target_link_libraries(hidhide_probe_tests PRIVATE hydra_hidhide_probe)
hydra_enable_strict_warnings(hidhide_probe_tests)
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
hydra_enable_strict_warnings(hidhide_session_backend_tests)
add_test(NAME HidHideSessionBackendTests COMMAND hidhide_session_backend_tests)
set_tests_properties(HidHideSessionBackendTests PROPERTIES LABELS "P3-D-02")
add_test(NAME HidHideSessionLabSelfTest COMMAND hydra_hidhide_session_lab --self-test)
set_tests_properties(HidHideSessionLabSelfTest PROPERTIES LABELS "P3-D-02")

add_executable(hidhide_session_recovery_tests
    tests/test_hidhide_session_recovery.cpp
)
target_link_libraries(hidhide_session_recovery_tests PRIVATE
    hydra_hidhide_session_recovery)
hydra_enable_strict_warnings(hidhide_session_recovery_tests)
add_test(NAME HidHideSessionRecoveryTests COMMAND hidhide_session_recovery_tests)
set_tests_properties(HidHideSessionRecoveryTests PROPERTIES LABELS "P3-D-02;P8-WATCH-01")

# P5-AUD-01 deterministic fake inventory/notification/Seat-assignment tests.
add_executable(audio_endpoint_inventory_tests
    tests/test_audio_endpoint_inventory.cpp
)
target_link_libraries(audio_endpoint_inventory_tests PRIVATE hydra_audio_inventory)
hydra_enable_strict_warnings(audio_endpoint_inventory_tests)
add_test(NAME AudioEndpointInventoryTests COMMAND audio_endpoint_inventory_tests)
set_tests_properties(AudioEndpointInventoryTests PROPERTIES LABELS "P5-AUD-01")
add_test(NAME AudioEndpointDiagHelp COMMAND hydra_audio_diag --help)
set_tests_properties(AudioEndpointDiagHelp PROPERTIES LABELS "P5-AUD-01")

# P5-AUD-02 exact process/session ownership, late-session observation, typed
# mutable backend, apply verification, rollback, and unsupported production path.
add_executable(audio_routing_tests tests/test_audio_routing.cpp)
target_link_libraries(audio_routing_tests PRIVATE hydra_audio_routing)
hydra_enable_strict_warnings(audio_routing_tests)
add_test(NAME AudioRoutingTests COMMAND audio_routing_tests)
set_tests_properties(AudioRoutingTests PROPERTIES LABELS "P5-AUD-02")

# P8-WATCH-01 portable protocol/registry tests and Windows process/fault tests.
add_executable(watchdog_protocol_tests
    tests/test_watchdog_protocol.cpp
)
target_link_libraries(watchdog_protocol_tests PRIVATE hydra_watchdog_core)
hydra_enable_strict_warnings(watchdog_protocol_tests)
add_test(NAME WatchdogProtocolTests COMMAND watchdog_protocol_tests)
if(WIN32)
    add_executable(watchdog_process_tests
        tests/test_watchdog_process.cpp
    )
    target_link_libraries(watchdog_process_tests PRIVATE hydra_watchdog_core)
    add_dependencies(watchdog_process_tests hydra_watchdog)
    hydra_enable_strict_warnings(watchdog_process_tests)
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
hydra_enable_strict_warnings(crash_journal_tests)
add_test(NAME CrashJournalTests COMMAND crash_journal_tests)

# P8-RESET-01 codec/state/rollback/idempotency tests. Windows process tests run
# the actual hydra_reset executable against exact owned process identities.
add_executable(reset_actions_tests
    tests/test_reset_actions.cpp
)
target_link_libraries(reset_actions_tests PRIVATE hydra_reset_actions)
hydra_enable_strict_warnings(reset_actions_tests)
add_test(NAME ResetActionsTests COMMAND reset_actions_tests)
if(WIN32)
    add_executable(reset_process_tests tests/test_reset_process.cpp)
    target_link_libraries(reset_process_tests PRIVATE hydra_reset_actions)
    add_dependencies(reset_process_tests hydra_reset)
    hydra_enable_strict_warnings(reset_process_tests)
    add_test(NAME ResetProcessTests COMMAND reset_process_tests)
    set_tests_properties(ResetProcessTests PROPERTIES TIMEOUT 90)
endif()

# P3-REC-01 portable trusted-plan and durable recovery-journal lifecycle tests.
add_executable(gate_c_recovery_tests
    tests/test_gate_c_recovery.cpp
)
target_link_libraries(gate_c_recovery_tests PRIVATE hydra_gate_c_recovery)
hydra_enable_strict_warnings(gate_c_recovery_tests)
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
    hydra_enable_strict_warnings(gate_c_watchdog_recovery_tests)
    add_test(NAME GateCWatchdogRecoveryTests
             COMMAND gate_c_watchdog_recovery_tests)
    set_tests_properties(GateCWatchdogRecoveryTests PROPERTIES TIMEOUT 180)
endif()

# Pure Gate A/B observation and explicit Seat-routing tests.
add_executable(input_observation_tests
    tests/test_input_observation.cpp
    src/input_observation.cpp
    src/input_isolation.cpp
    include/hydra/input_observation.hpp
    include/hydra/input_isolation.hpp
    include/hydra/input_router.hpp
)
target_link_libraries(input_observation_tests PRIVATE hydra_workspace)
hydra_enable_strict_warnings(input_observation_tests)
add_test(NAME InputObservationTests COMMAND input_observation_tests)
add_test(NAME InputLabSelfTest COMMAND hydra_input_lab --self-test)

# Pure Gate C wire-protocol and process-local virtual-state tests.
add_executable(gate_c_protocol_tests
    tests/test_gate_c_protocol.cpp
)
target_link_libraries(gate_c_protocol_tests PRIVATE hydra_gate_c_core)
hydra_enable_strict_warnings(gate_c_protocol_tests)
add_test(NAME GateCProtocolTests COMMAND gate_c_protocol_tests)

# P3-ARCH-01 process-architecture detection, PE validation, and the bounded
# architecture-specific artifact manifest/selector.
add_executable(gate_c_architecture_tests
    tests/test_gate_c_architecture.cpp
)
target_link_libraries(gate_c_architecture_tests PRIVATE hydra_gate_c_core)
hydra_set_gate_c_output(gate_c_architecture_tests)
hydra_enable_strict_warnings(gate_c_architecture_tests)
add_test(NAME GateCArchitectureTests COMMAND gate_c_architecture_tests)

# P3-API-01 fixed-width, versioned OS/adapter comparison snapshot tests.
add_executable(gate_c_probe_snapshot_tests
    tests/test_gate_c_probe_snapshot.cpp
)
target_link_libraries(gate_c_probe_snapshot_tests PRIVATE hydra_gate_c_core)
hydra_enable_strict_warnings(gate_c_probe_snapshot_tests)
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
hydra_enable_strict_warnings(raw_input_probe_trace_tests)
add_test(NAME RawInputProbeTraceTests COMMAND raw_input_probe_trace_tests)
add_test(NAME RawInputProbeSelfTest
         COMMAND hydra_gate_c_raw_input_probe --self-test)

# P3-RAW-02 portable registration/token/queue/data/buffer contract tests.
add_executable(virtual_raw_input_tests
    tests/test_virtual_raw_input.cpp
)
target_link_libraries(virtual_raw_input_tests PRIVATE hydra_virtual_raw_input)
hydra_enable_strict_warnings(virtual_raw_input_tests)
add_test(NAME VirtualRawInputTests COMMAND virtual_raw_input_tests)

# P3-CTRL-01 normalized XInput logical-slot, generation, metadata, battery,
# and vibration-route component tests.
add_executable(virtual_xinput_state_tests
    tests/test_virtual_xinput_state.cpp
)
target_link_libraries(virtual_xinput_state_tests PRIVATE hydra_virtual_xinput)
hydra_enable_strict_warnings(virtual_xinput_state_tests)
add_test(NAME VirtualXInputStateTests COMMAND virtual_xinput_state_tests)

# P3-CTRL-02 stable instance-ID allowlist/order policy and controlled probe tests.
add_executable(directinput_policy_tests
    tests/test_directinput_policy.cpp
)
target_link_libraries(directinput_policy_tests PRIVATE hydra_directinput_policy)
hydra_enable_strict_warnings(directinput_policy_tests)
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
hydra_enable_strict_warnings(controller_runtime_tests)
add_test(NAME ControllerRuntimeTests COMMAND controller_runtime_tests)
set_tests_properties(ControllerRuntimeTests PROPERTIES LABELS "P5-CTRL-01")
add_test(NAME ControllerDiagHelp COMMAND hydra_controller_diag --help)
set_tests_properties(ControllerDiagHelp PROPERTIES LABELS "P5-CTRL-01")

# P3-MET-01 recorder/report contract, deterministic fixture, and CLI checks.
add_executable(input_metrics_tests
    tests/test_input_metrics.cpp
)
target_link_libraries(input_metrics_tests PRIVATE hydra_input_metrics)
hydra_enable_strict_warnings(input_metrics_tests)
add_test(NAME InputMetricsTests COMMAND input_metrics_tests)
add_test(NAME InputMetricsCliSelfTest
         COMMAND hydra_input_metrics_cli --self-test)
add_test(NAME InputMetricsFixtureReport
         COMMAND hydra_input_metrics_cli --fixture-report)

# P5-MET-01 integrated evidence origin/verdict/privacy/zero-cross report tests.
add_executable(session_metrics_tests tests/test_session_metrics.cpp)
target_link_libraries(session_metrics_tests PRIVATE hydra_session_metrics)
hydra_enable_strict_warnings(session_metrics_tests)
add_test(NAME SessionMetricsTests COMMAND session_metrics_tests)
set_tests_properties(SessionMetricsTests PROPERTIES LABELS "P5-MET-01")

# P6-SCHEMA-01 strict version/bounds/Unicode/cross-reference persistence tests.
add_executable(profile_schema_tests tests/test_profile_schema.cpp)
target_link_libraries(profile_schema_tests PRIVATE hydra_profile_schema)
hydra_enable_strict_warnings(profile_schema_tests)
add_test(NAME ProfileSchemaTests COMMAND profile_schema_tests)
set_tests_properties(ProfileSchemaTests PROPERTIES LABELS "P6-SCHEMA-01")

add_executable(strict_json_tests tests/test_strict_json.cpp)
hydra_enable_strict_warnings(strict_json_tests)
add_test(NAME StrictJsonTests COMMAND strict_json_tests)

# P6-MIG-01 deterministic conversion/report/backup plus injected write,
# staged-validation, commit, and post-commit rollback tests.
add_executable(profile_migration_tests tests/test_profile_migration.cpp)
target_link_libraries(profile_migration_tests PRIVATE hydra_profile_migration)
hydra_enable_strict_warnings(profile_migration_tests)
add_test(NAME ProfileMigrationTests COMMAND profile_migration_tests)
set_tests_properties(ProfileMigrationTests PROPERTIES LABELS "P6-MIG-01")

# P6-CATALOG-01 provider/executable identity, malformed metadata, deterministic
# duplicate reconciliation, missing artwork, staleness, and transaction tests.
add_executable(game_catalog_tests tests/test_game_catalog.cpp)
target_link_libraries(game_catalog_tests PRIVATE hydra_game_catalog)
hydra_enable_strict_warnings(game_catalog_tests)
add_test(NAME GameCatalogTests COMMAND game_catalog_tests)
set_tests_properties(GameCatalogTests PROPERTIES LABELS "P6-CATALOG-01")

# P6-PROV-01 fake-provider contract, revision, bounds, offline/absent,
# transactional failure, typed-launch, and post-launch evidence tests.
add_executable(provider_adapter_tests tests/test_provider_adapter.cpp)
target_link_libraries(provider_adapter_tests PRIVATE hydra_provider_adapter)
hydra_enable_strict_warnings(provider_adapter_tests)
add_test(NAME ProviderAdapterTests COMMAND provider_adapter_tests)
set_tests_properties(ProviderAdapterTests PROPERTIES LABELS "P6-PROV-01")

# P6-PROV-02 virtual metadata-source fixtures plus a Windows-only, read-only
# installed-Steam smoke test. Neither test starts Steam or a game.
add_executable(steam_provider_tests tests/test_steam_provider.cpp)
target_link_libraries(steam_provider_tests PRIVATE hydra_steam_provider)
hydra_enable_strict_warnings(steam_provider_tests)
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
hydra_enable_strict_warnings(custom_executable_provider_tests)
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
hydra_enable_strict_warnings(provider_launch_plan_tests)
add_test(NAME ProviderLaunchPlanTests COMMAND provider_launch_plan_tests)
set_tests_properties(ProviderLaunchPlanTests PROPERTIES LABELS "P6-PLAN-01")

# P6/P7 production requirement resolver: exact local evidence/provider/catalog
# matching, transactional store behavior, freshness, protected approval, and
# community-evidence rejection.
add_executable(game_runtime_requirement_resolver_tests
    tests/test_game_runtime_requirement_resolver.cpp)
target_link_libraries(game_runtime_requirement_resolver_tests PRIVATE
    hydra_game_runtime_requirement_resolver)
hydra_enable_strict_warnings(game_runtime_requirement_resolver_tests)
add_test(NAME GameRuntimeRequirementResolverTests
         COMMAND game_runtime_requirement_resolver_tests)
set_tests_properties(GameRuntimeRequirementResolverTests PROPERTIES
    LABELS "P6-PLAN-01;P7-LAUNCH-01")

add_executable(local_compatibility_evidence_tests
    tests/test_local_compatibility_evidence.cpp)
target_link_libraries(local_compatibility_evidence_tests PRIVATE
    hydra_local_compatibility_evidence)
hydra_enable_strict_warnings(local_compatibility_evidence_tests)
add_test(NAME LocalCompatibilityEvidenceTests
         COMMAND local_compatibility_evidence_tests)
set_tests_properties(LocalCompatibilityEvidenceTests PROPERTIES
    LABELS "V1-PLAY-AUTHORITY")

add_executable(runtime_requirement_authority_tests
    tests/test_runtime_requirement_authority.cpp)
target_link_libraries(runtime_requirement_authority_tests PRIVATE
    hydra_runtime_requirement_authority)
hydra_enable_strict_warnings(runtime_requirement_authority_tests)
add_test(NAME RuntimeRequirementAuthorityTests
         COMMAND runtime_requirement_authority_tests)
set_tests_properties(RuntimeRequirementAuthorityTests PROPERTIES
    LABELS "V1-PLAY-AUTHORITY")

add_executable(local_compatibility_runner_tests
    tests/test_local_compatibility_runner.cpp)
target_link_libraries(local_compatibility_runner_tests PRIVATE
    hydra_local_compatibility_runner)
hydra_enable_strict_warnings(local_compatibility_runner_tests)
add_test(NAME LocalCompatibilityRunnerTests
         COMMAND local_compatibility_runner_tests)
set_tests_properties(LocalCompatibilityRunnerTests PROPERTIES
    LABELS "V1-PLAY-AUTHORITY")

add_executable(production_input_authority_tests
    tests/test_production_input_authority.cpp)
target_link_libraries(production_input_authority_tests PRIVATE
    hydra_production_input_authority)
hydra_enable_strict_warnings(production_input_authority_tests)
add_test(NAME ProductionInputAuthorityTests
         COMMAND production_input_authority_tests)
set_tests_properties(ProductionInputAuthorityTests PROPERTIES
    LABELS "V1-PLAY-AUTHORITY")

# P5/P7 production launch boundary: immutable Host registry, exact
# profile/session/Seat generation binding, resource rollback at every activation
# index, real controlled Job Object ownership, and IPC installer permissions.
add_executable(production_launch_runtime_tests
    tests/test_production_launch_runtime.cpp)
target_link_libraries(production_launch_runtime_tests PRIVATE
    hydra_production_launch_installer
    hydra_runtime
)
hydra_enable_strict_warnings(production_launch_runtime_tests)
if(WIN32)
    add_dependencies(production_launch_runtime_tests hydra_process_tree_child)
    add_test(NAME ProductionLaunchRuntimeTests
             COMMAND production_launch_runtime_tests
                     $<TARGET_FILE:hydra_process_tree_child>)
else()
    add_test(NAME ProductionLaunchRuntimeTests
             COMMAND production_launch_runtime_tests)
endif()
set_tests_properties(ProductionLaunchRuntimeTests PROPERTIES
    LABELS "P5-LAUNCH-01;P7-LAUNCH-01")
if(WIN32)
    set_tests_properties(ProductionLaunchRuntimeTests PROPERTIES
        RESOURCE_LOCK "HydraSeatHostPipe")
endif()

add_executable(production_compatibility_activation_tests
    tests/test_production_compatibility_activation.cpp)
target_link_libraries(production_compatibility_activation_tests PRIVATE
    hydra_production_compatibility_activation)
hydra_enable_strict_warnings(production_compatibility_activation_tests)
add_test(NAME ProductionCompatibilityActivationTests
         COMMAND production_compatibility_activation_tests)
set_tests_properties(ProductionCompatibilityActivationTests PROPERTIES
    LABELS "P5-LAUNCH-01;P7-LAUNCH-01")

add_executable(production_activation_bridges_tests
    tests/test_production_activation_bridges.cpp)
target_link_libraries(production_activation_bridges_tests PRIVATE
    hydra_production_activation_bridges)
hydra_enable_strict_warnings(production_activation_bridges_tests)
add_test(NAME ProductionActivationBridgesTests
         COMMAND production_activation_bridges_tests)
set_tests_properties(ProductionActivationBridgesTests PROPERTIES
    LABELS "P3-REC-01;P5-LAUNCH-01;P7-LAUNCH-01")

add_executable(production_host_protocol_tests
    tests/test_production_host_protocol.cpp)
target_link_libraries(production_host_protocol_tests PRIVATE
    hydra_host_protocol
    hydra_production_launch_runtime)
hydra_enable_strict_warnings(production_host_protocol_tests)
add_test(NAME ProductionHostProtocolTests
         COMMAND production_host_protocol_tests)
set_tests_properties(ProductionHostProtocolTests PROPERTIES
    LABELS "P4-IPC-01;P7-LAUNCH-01")

# P10-RC-01 acceptance infrastructure remains fail-closed even while the packet
# itself is BLOCKED on upstream/manual gates. These tests prove exact RC/scenario
# binding and that synthetic evidence cannot satisfy physical stages.
add_executable(acceptance_campaign_tests
    tests/test_acceptance_campaign.cpp)
target_link_libraries(acceptance_campaign_tests PRIVATE
    hydra_acceptance_campaign)
hydra_enable_strict_warnings(acceptance_campaign_tests)
add_test(NAME AcceptanceCampaignTests COMMAND acceptance_campaign_tests)
set_tests_properties(AcceptanceCampaignTests PROPERTIES LABELS "P10-RC-01")

add_executable(acceptance_probe_process_tests
    tests/test_acceptance_probe_process.cpp)
target_link_libraries(acceptance_probe_process_tests PRIVATE
    hydra_acceptance_probe)
hydra_enable_strict_warnings(acceptance_probe_process_tests)
if(WIN32)
    add_dependencies(acceptance_probe_process_tests hydra_process_tree_child)
    add_test(NAME AcceptanceProbeProcessTests
             COMMAND acceptance_probe_process_tests
                     $<TARGET_FILE:hydra_process_tree_child>)
else()
    add_test(NAME AcceptanceProbeProcessTests
             COMMAND acceptance_probe_process_tests)
endif()
set_tests_properties(AcceptanceProbeProcessTests PROPERTIES LABELS "P10-RC-01")

if(Python3_Interpreter_FOUND)
    add_test(NAME V1AcceptanceCampaignValidatorTests
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/tools/validate_v1_acceptance_campaign.py"
                     --self-test)
    set_tests_properties(V1AcceptanceCampaignValidatorTests PROPERTIES
        LABELS "P10-RC-01")
endif()

# P6-PREFLIGHT-01 normal/expert messages, deterministic mutation ordering,
# explicit approval gates, and typed no-secret mutation preview tests.
add_executable(plan_preflight_tests tests/test_plan_preflight.cpp)
target_link_libraries(plan_preflight_tests PRIVATE hydra_plan_preflight)
hydra_enable_strict_warnings(plan_preflight_tests)
add_test(NAME PlanPreflightTests COMMAND plan_preflight_tests)
set_tests_properties(PlanPreflightTests PROPERTIES LABELS "P6-PREFLIGHT-01")

# P6-UI-01 full normal flow over fake providers and immutable local snapshots.
add_executable(launcher_ui_model_tests tests/test_launcher_ui_model.cpp)
target_link_libraries(launcher_ui_model_tests PRIVATE hydra_launcher_ui_model)
hydra_enable_strict_warnings(launcher_ui_model_tests)
add_test(NAME LauncherUiModelTests COMMAND launcher_ui_model_tests)
set_tests_properties(LauncherUiModelTests PROPERTIES LABELS "P6-UI-01")

add_executable(launcher_user_state_tests tests/test_launcher_user_state.cpp)
target_link_libraries(launcher_user_state_tests PRIVATE hydra_launcher_user_state)
hydra_enable_strict_warnings(launcher_user_state_tests)
add_test(NAME LauncherUserStateTests COMMAND launcher_user_state_tests)
set_tests_properties(LauncherUserStateTests PROPERTIES LABELS "V1-HANDS-ON")

add_executable(installer_bootstrap_tests tests/test_installer_bootstrap.cpp)
target_link_libraries(installer_bootstrap_tests PRIVATE hydra_installer_bootstrap)
hydra_enable_strict_warnings(installer_bootstrap_tests)
add_test(NAME InstallerBootstrapTests COMMAND installer_bootstrap_tests)
set_tests_properties(InstallerBootstrapTests PROPERTIES LABELS "P8-INST-01;V1-HANDS-ON")

add_executable(v1_user_journey_tests tests/test_v1_user_journey.cpp)
target_link_libraries(v1_user_journey_tests PRIVATE
    hydra_launcher_ui_model hydra_launcher_user_state)
hydra_enable_strict_warnings(v1_user_journey_tests)
add_test(NAME V1UserJourneyTests COMMAND v1_user_journey_tests)
set_tests_properties(V1UserJourneyTests PROPERTIES LABELS "V1-HANDS-ON")

if(Python3_Interpreter_FOUND)
    add_test(NAME V1HandsOnReadinessValidatorTests
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/tools/validate_v1_hands_on_readiness.py"
                     --self-test)
    set_tests_properties(V1HandsOnReadinessValidatorTests PROPERTIES
        LABELS "V1-HANDS-ON")
endif()

# P6-PROFILE-01 automatic/manual convergence, transactional Save, bounded path,
# shared-data-root, and cross-document runtime-selection validation tests.
add_executable(two_player_setup_editor_tests tests/test_two_player_setup_editor.cpp)
target_link_libraries(two_player_setup_editor_tests PRIVATE hydra_two_player_setup_editor)
hydra_enable_strict_warnings(two_player_setup_editor_tests)
add_test(NAME TwoPlayerSetupEditorTests COMMAND two_player_setup_editor_tests)
set_tests_properties(TwoPlayerSetupEditorTests PROPERTIES LABELS "P6-PROFILE-01")

# P6-IMPORT-01 privacy-redacted package roundtrip, explicit remap, malformed
# envelope/version, and transactional failure tests.
add_executable(setup_package_tests tests/test_setup_package.cpp)
target_link_libraries(setup_package_tests PRIVATE hydra_setup_package)
hydra_enable_strict_warnings(setup_package_tests)
add_test(NAME SetupPackageTests COMMAND setup_package_tests)
set_tests_properties(SetupPackageTests PROPERTIES LABELS "P6-IMPORT-01")

# P6-REG-01 deterministic cross-packet fixture corpus covering provider/catalog
# churn, Seat/Player moves, same-game setup/import, protection, malformed data,
# provider absence/offline, Unicode, moved paths, and stale/uninstalled metadata.
add_executable(phase6_regression_tests tests/test_phase6_regression_fixtures.cpp)
target_link_libraries(phase6_regression_tests PRIVATE
    hydra_game_catalog hydra_provider_launch_plan hydra_plan_preflight
    hydra_two_player_setup_editor hydra_setup_package)
hydra_enable_strict_warnings(phase6_regression_tests)
add_test(NAME Phase6RegressionTests COMMAND phase6_regression_tests)
set_tests_properties(Phase6RegressionTests PROPERTIES LABELS "P6-REG-01")

# P6-CLI-01 stable-schema human/JSON rendering, plan snapshot roundtrip, Unicode,
# malformed-version rejection, and account-reference redaction tests.
add_executable(profile_cli_tests tests/test_profile_cli.cpp)
target_link_libraries(profile_cli_tests PRIVATE hydra_profile_cli)
hydra_enable_strict_warnings(profile_cli_tests)
add_test(NAME ProfileCliTests COMMAND profile_cli_tests)
set_tests_properties(ProfileCliTests PROPERTIES LABELS "P6-CLI-01")

# P9-SDK-01 public compatibility-result schema/canonicalization, privacy,
# malformed/future/bounds, and Phase 5 session-metrics adapter fixtures.
add_executable(compatibility_result_tests tests/test_compatibility_result.cpp)
target_link_libraries(compatibility_result_tests PRIVATE hydra_compatibility_result)
hydra_enable_strict_warnings(compatibility_result_tests)
add_test(NAME CompatibilityResultTests COMMAND compatibility_result_tests)
set_tests_properties(CompatibilityResultTests PROPERTIES LABELS "P9-SDK-01")

add_executable(compatibility_aggregation_tests tests/test_compatibility_aggregation.cpp)
target_link_libraries(compatibility_aggregation_tests PRIVATE hydra_compatibility_result)
hydra_enable_strict_warnings(compatibility_aggregation_tests)
add_test(NAME CompatibilityAggregationTests COMMAND compatibility_aggregation_tests)
set_tests_properties(CompatibilityAggregationTests PROPERTIES LABELS "P9-CAP-01")

add_executable(artifact_trust_tests tests/test_artifact_trust.cpp)
target_link_libraries(artifact_trust_tests PRIVATE hydra_artifact_trust)
hydra_enable_strict_warnings(artifact_trust_tests)
add_test(NAME ArtifactTrustTests COMMAND artifact_trust_tests)
set_tests_properties(ArtifactTrustTests PROPERTIES LABELS "P8-TRUST-01")

add_executable(community_package_tests tests/test_community_package.cpp)
target_link_libraries(community_package_tests PRIVATE hydra_community_package)
hydra_enable_strict_warnings(community_package_tests)
add_test(NAME CommunityPackageTests COMMAND community_package_tests)
set_tests_properties(CommunityPackageTests PROPERTIES LABELS "P9-PKG-01")

add_executable(community_setup_tests tests/test_community_setup.cpp)
target_link_libraries(community_setup_tests PRIVATE hydra_community_package)
hydra_enable_strict_warnings(community_setup_tests)
add_test(NAME CommunitySetupTests COMMAND community_setup_tests)
set_tests_properties(CommunitySetupTests PROPERTIES LABELS "P9-PROFILE-01")

add_executable(compatibility_catalog_tests tests/test_compatibility_catalog.cpp)
target_link_libraries(compatibility_catalog_tests PRIVATE hydra_community_package)
hydra_enable_strict_warnings(compatibility_catalog_tests)
add_test(NAME CompatibilityCatalogTests COMMAND compatibility_catalog_tests)
set_tests_properties(CompatibilityCatalogTests PROPERTIES LABELS "P9-REG-01")

add_executable(catalog_cache_tests tests/test_catalog_cache.cpp)
target_link_libraries(catalog_cache_tests PRIVATE hydra_catalog_cache)
hydra_enable_strict_warnings(catalog_cache_tests)
add_test(NAME CatalogCacheTests COMMAND catalog_cache_tests)
set_tests_properties(CatalogCacheTests PROPERTIES LABELS "P8-DATA-01")

add_executable(support_bundle_tests tests/test_support_bundle.cpp)
target_link_libraries(support_bundle_tests PRIVATE hydra_support_bundle)
hydra_enable_strict_warnings(support_bundle_tests)
add_test(NAME SupportBundleTests COMMAND support_bundle_tests)
set_tests_properties(SupportBundleTests PROPERTIES LABELS "P8-DIAG-01")

add_executable(privilege_broker_tests tests/test_privilege_broker.cpp)
target_link_libraries(privilege_broker_tests PRIVATE hydra_privilege_broker)
hydra_enable_strict_warnings(privilege_broker_tests)
add_test(NAME PrivilegeBrokerTests COMMAND privilege_broker_tests)
set_tests_properties(PrivilegeBrokerTests PROPERTIES LABELS "P8-PRIV-01")

add_executable(startup_policy_tests tests/test_startup_policy.cpp)
target_link_libraries(startup_policy_tests PRIVATE hydra_startup_policy)
hydra_enable_strict_warnings(startup_policy_tests)
add_test(NAME StartupPolicyTests COMMAND startup_policy_tests)
set_tests_properties(StartupPolicyTests PROPERTIES LABELS "P8-BOOT-01")

add_executable(installer_transaction_tests tests/test_installer_transaction.cpp)
target_link_libraries(installer_transaction_tests PRIVATE hydra_installer_transaction)
hydra_enable_strict_warnings(installer_transaction_tests)
add_test(NAME InstallerTransactionTests COMMAND installer_transaction_tests)
set_tests_properties(InstallerTransactionTests PROPERTIES LABELS "P8-INST-01")

add_executable(update_transaction_tests tests/test_update_transaction.cpp)
target_link_libraries(update_transaction_tests PRIVATE hydra_update_transaction)
hydra_enable_strict_warnings(update_transaction_tests)
add_test(NAME UpdateTransactionTests COMMAND update_transaction_tests)
set_tests_properties(UpdateTransactionTests PROPERTIES LABELS "P8-UPD-01")

if(Python3_Interpreter_FOUND)
    add_test(NAME ReleaseSigningManifestTests
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_release_signing_manifest.py"
                     --self-test)
    set_tests_properties(ReleaseSigningManifestTests PROPERTIES LABELS "P8-SIGN-01")
    add_test(NAME ReleaseInstallerContractTests
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_release_installer_contract.py"
                     --self-test)
    set_tests_properties(ReleaseInstallerContractTests PROPERTIES
        LABELS "P8-SIGN-01;P8-INST-01")
endif()
if(WIN32 AND HYDRA_POWERSHELL_EXECUTABLE)
    add_test(NAME ReleasePowerShellSyntaxTests
             COMMAND "${HYDRA_POWERSHELL_EXECUTABLE}"
                     -NoProfile -ExecutionPolicy Bypass
                     -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_release_powershell_syntax.ps1"
                     -RepositoryRoot "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(ReleasePowerShellSyntaxTests PROPERTIES
        LABELS "P8-SIGN-01;P8-INST-01")
endif()

add_executable(community_submission_tests tests/test_community_submission.cpp)
target_link_libraries(community_submission_tests PRIVATE hydra_community_submission)
hydra_enable_strict_warnings(community_submission_tests)
add_test(NAME CommunitySubmissionTests COMMAND community_submission_tests)
set_tests_properties(CommunitySubmissionTests PROPERTIES LABELS "P9-RPC-01")

add_executable(compatibility_share_model_tests tests/test_compatibility_share_model.cpp)
target_link_libraries(compatibility_share_model_tests PRIVATE hydra_community_submission)
hydra_enable_strict_warnings(compatibility_share_model_tests)
add_test(NAME CompatibilityShareModelTests COMMAND compatibility_share_model_tests)
set_tests_properties(CompatibilityShareModelTests PROPERTIES LABELS "P9-DIAG-01")

add_executable(compatibility_local_store_tests tests/test_compatibility_local_store.cpp)
target_link_libraries(compatibility_local_store_tests PRIVATE hydra_community_submission)
hydra_enable_strict_warnings(compatibility_local_store_tests)
add_test(NAME CompatibilityLocalStoreTests COMMAND compatibility_local_store_tests)
set_tests_properties(CompatibilityLocalStoreTests PROPERTIES LABELS "P10-PRIV-01")

# P3-HW-01 acceptance tooling is CI-tested without claiming physical acceptance.
# Python parses bounded synthetic evidence; Windows additionally checks that the
# guided/resumable PowerShell 5 runner and its manifest schema load successfully.
if(Python3_Interpreter_FOUND)
    add_test(NAME Phase3HardwareAcceptanceParserTests
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/summarize_phase3_trace.py"
                     --self-test)
    set_tests_properties(Phase3HardwareAcceptanceParserTests PROPERTIES
        LABELS "P3-HW-01")
endif()
if(WIN32 AND HYDRA_POWERSHELL_EXECUTABLE)
    add_test(NAME Phase3HardwareAcceptanceRunnerSelfTest
             COMMAND "${HYDRA_POWERSHELL_EXECUTABLE}"
                     -NoProfile -ExecutionPolicy Bypass
                     -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_phase3_hardware_acceptance.ps1"
                     -SelfTest)
    set_tests_properties(Phase3HardwareAcceptanceRunnerSelfTest PROPERTIES
        LABELS "P3-HW-01")
endif()

# C ABI adapter tests exercise independent process-local contexts and
# GetAsyncKeyState/GetKeyboardState-style semantics without installing hooks.
add_executable(gate_c_adapter_tests
    tests/test_gate_c_adapter.cpp
)
target_link_libraries(gate_c_adapter_tests PRIVATE hydra_gate_c_adapter)
hydra_set_gate_c_output(gate_c_adapter_tests)
hydra_enable_strict_warnings(gate_c_adapter_tests)
add_test(NAME GateCAdapterTests COMMAND gate_c_adapter_tests)

add_executable(gate_c_xinput_adapter_tests
    tests/test_gate_c_xinput_adapter.cpp
)
target_link_libraries(gate_c_xinput_adapter_tests PRIVATE
    hydra_gate_c_adapter)
hydra_set_gate_c_output(gate_c_xinput_adapter_tests)
hydra_enable_strict_warnings(gate_c_xinput_adapter_tests)
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
hydra_enable_strict_warnings(gate_c_polling_shim_tests)
add_test(NAME GateCPollingShimTests COMMAND gate_c_polling_shim_tests)

add_executable(gate_c_cursor_focus_shim_tests
    tests/test_gate_c_cursor_focus_shim.cpp
)
target_link_libraries(gate_c_cursor_focus_shim_tests PRIVATE
    hydra_gate_c_shim_support
)
hydra_set_gate_c_output(gate_c_cursor_focus_shim_tests)
hydra_enable_strict_warnings(gate_c_cursor_focus_shim_tests)
add_test(NAME GateCCursorFocusShimTests
         COMMAND gate_c_cursor_focus_shim_tests)

add_executable(gate_c_raw_input_shim_tests
    tests/test_gate_c_raw_input_shim.cpp
)
target_link_libraries(gate_c_raw_input_shim_tests PRIVATE
    hydra_gate_c_shim_support
)
hydra_set_gate_c_output(gate_c_raw_input_shim_tests)
hydra_enable_strict_warnings(gate_c_raw_input_shim_tests)
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
    set_tests_properties(GateCProcessSelfTest PROPERTIES
        TIMEOUT 45 LABELS "P3-PROC-01")

    add_test(
        NAME GateCProtocolErrorSelfTest
        COMMAND hydra_gate_c_host --protocol-error-self-test --target $<TARGET_FILE:hydra_gate_c_target>
    )
    set_tests_properties(GateCProtocolErrorSelfTest PROPERTIES
        TIMEOUT 30 LABELS "P3-IPC-01")

    add_test(
        NAME GateCApiProbeProcessSelfTest
        COMMAND hydra_gate_c_host --baseline-self-test --target $<TARGET_FILE:hydra_gate_c_api_probe>
    )
    set_tests_properties(GateCApiProbeProcessSelfTest PROPERTIES
        TIMEOUT 60 LABELS "P3-API-01")

    add_test(
        NAME GateCPollingShimProcessSelfTest
        COMMAND hydra_gate_c_host --polling-shim-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCPollingShimProcessSelfTest PROPERTIES
        TIMEOUT 75 LABELS "P3-API-02")

    add_test(
        NAME GateCCursorFocusShimProcessSelfTest
        COMMAND hydra_gate_c_host --cursor-focus-shim-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCCursorFocusShimProcessSelfTest PROPERTIES
        TIMEOUT 75 LABELS "P3-API-03")

    add_test(
        NAME GateCRawInputShimProcessSelfTest
        COMMAND hydra_gate_c_host --raw-input-shim-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
                --shim $<TARGET_FILE:hydra_gate_c_shim>
    )
    set_tests_properties(GateCRawInputShimProcessSelfTest PROPERTIES
        TIMEOUT 75 LABELS "P3-RAW-02")

    add_test(
        NAME GateCXInputProcessSelfTest
        COMMAND hydra_gate_c_host --xinput-self-test
                --target $<TARGET_FILE:hydra_gate_c_api_probe>
    )
    set_tests_properties(GateCXInputProcessSelfTest PROPERTIES
        TIMEOUT 75 LABELS "P3-CTRL-01")
else()
    add_test(NAME GateCHostPortableSelfTest COMMAND hydra_gate_c_host --self-test)
    set_tests_properties(GateCHostPortableSelfTest PROPERTIES LABELS "P3-PROC-01")
endif()
