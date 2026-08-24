cmake_minimum_required(VERSION 3.20)
project(HydraSeat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    src/gate_c_protocol.cpp
    src/gate_c_transport.cpp
    src/virtual_input_state.cpp
    include/hydra/gate_c_protocol.hpp
    include/hydra/gate_c_transport.hpp
    include/hydra/virtual_input_state.hpp
)
if(MSVC)
    target_compile_options(hydra_gate_c_core PRIVATE /W4 /permissive-)
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
target_link_libraries(hydra_gate_c_target PRIVATE hydra_gate_c_core)
if(WIN32)
    target_link_libraries(hydra_gate_c_target PRIVATE User32.lib)
endif()
if(MSVC)
    target_compile_options(hydra_gate_c_target PRIVATE /W4 /permissive-)
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
add_test(NAME GateCTargetSelfTest COMMAND hydra_gate_c_target --self-test)

if(WIN32)
    # Launches two separate controlled target processes, performs the versioned
    # handshake, sends different Seat states, queries snapshots, and verifies
    # no cross-process state bleed. No device hiding or injection occurs.
    add_test(
        NAME GateCProcessSelfTest
        COMMAND hydra_gate_c_host --self-test --target $<TARGET_FILE:hydra_gate_c_target>
    )
    set_tests_properties(GateCProcessSelfTest PROPERTIES TIMEOUT 45)
else()
    add_test(NAME GateCHostPortableSelfTest COMMAND hydra_gate_c_host --self-test)
endif()
