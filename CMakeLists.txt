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
