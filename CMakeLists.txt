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
    set(WIN32_LIBS User32.lib Cfgmgr32.lib Dxgi.lib Setupapi.lib Hid.lib Xinput.lib Comctl32.lib)
endif()

# Find Qt6 components if available
find_package(Qt6 COMPONENTS Core Widgets QUIET)

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

set(HYDRA_HARDWARE_HEADERS
    include/hydra/hardware_detector.hpp
    include/hydra/hardware_identity.hpp
    include/hydra/hid_usage.hpp
    include/hydra/raw_input_utils.hpp
)

set(HYDRA_HEADERS
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
if(MSVC)
    target_compile_options(hydra_hardware PRIVATE /W4 /permissive-)
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

# Core Executable target (Windows GUI subsystem)
add_executable(HydraSeat WIN32 src/main.cpp ${HYDRA_SOURCES} ${HYDRA_HEADERS})
target_link_libraries(HydraSeat PRIVATE hydra_hardware)

if(WIN32)
    target_link_libraries(HydraSeat PRIVATE ${WIN32_LIBS})
endif()

if(Qt6_FOUND)
    target_link_libraries(HydraSeat PRIVATE Qt6::Core Qt6::Widgets)
    target_compile_definitions(HydraSeat PRIVATE HYDRA_HAS_QT=1)
endif()

# Reset Tool Target
add_executable(reset_input src/reset_input.cpp)
if(WIN32)
    target_link_libraries(reset_input PRIVATE User32.lib)
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
target_link_libraries(hydra_gate_c_host PRIVATE hydra_gate_c_core hydra_hardware)
hydra_set_gate_c_output(hydra_gate_c_host)
if(WIN32)
    target_link_libraries(hydra_gate_c_host PRIVATE ${WIN32_LIBS})
endif()
if(MSVC)
    target_compile_options(hydra_gate_c_host PRIVATE /W4 /permissive-)
endif()

# Test Suite Executable Target
enable_testing()
add_executable(hydra_tests tests/test_hydra.cpp ${HYDRA_SOURCES} ${HYDRA_HEADERS})
target_link_libraries(hydra_tests PRIVATE hydra_hardware)
if(WIN32)
    target_link_libraries(hydra_tests PRIVATE ${WIN32_LIBS})
endif()
if(Qt6_FOUND)
    target_link_libraries(hydra_tests PRIVATE Qt6::Core Qt6::Widgets)
endif()
add_test(NAME EngineUnitTests COMMAND hydra_tests)

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
else()
    add_test(NAME GateCHostPortableSelfTest COMMAND hydra_gate_c_host --self-test)
endif()
