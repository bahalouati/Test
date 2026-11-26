cmake_minimum_required(VERSION 3.16)

project(CopilotAgent
    VERSION 0.1.0
    DESCRIPTION "Drives Microsoft 365 Copilot chat as a coding agent (Qt 5 + WebEngine)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Qt 5 only, 5.12 or newer (developed against 5.12.2).
find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets WebEngineCore WebEngineWidgets)
message(STATUS "Building against Qt ${Qt5_VERSION}")

if(WIN32)
    # Keep <windows.h>, if any header pulls it in, from defining min/max macros.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
    # Qt < 5.15.3 headers use std::numeric_limits without including <limits>,
    # which GCC 11+ no longer pulls in transitively.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-include limits)
    endif()
endif()

option(COPILOTAGENT_BUILD_TESTS "Build the unit tests" ON)

# Everything that does not need a window lives in a static library, so the
# tests link exactly the code the app runs.
add_library(agentcore STATIC
    src/agentaction.h
    src/agentprotocol.h      src/agentprotocol.cpp
    src/workspace.h          src/workspace.cpp
    src/linediff.h           src/linediff.cpp
    src/commandrunner.h      src/commandrunner.cpp
    src/agentcontroller.h    src/agentcontroller.cpp
)
target_include_directories(agentcore PUBLIC src)
target_link_libraries(agentcore PUBLIC Qt5::Core)

# The WebEngine side: the JS bridge that types into Copilot and reads replies.
# In Qt 5, QWebEnginePage and QWebEngineProfile live in WebEngineWidgets.
add_library(agentweb STATIC
    src/copilotbridge.h      src/copilotbridge.cpp
    src/bridge.qrc
)
target_include_directories(agentweb PUBLIC src)
target_link_libraries(agentweb PUBLIC Qt5::Core Qt5::WebEngineCore Qt5::WebEngineWidgets)

add_executable(copilot-agent WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.h         src/mainwindow.cpp
    src/approvaldialog.h     src/approvaldialog.cpp
    src/settingsdialog.h     src/settingsdialog.cpp
    src/webpage.h            src/webpage.cpp
)
target_link_libraries(copilot-agent PRIVATE
    agentcore agentweb Qt5::Widgets Qt5::WebEngineWidgets)

install(TARGETS copilot-agent
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)

if(COPILOTAGENT_BUILD_TESTS)
    enable_testing()
    find_package(Qt5 REQUIRED COMPONENTS Test)

    foreach(name tst_agentprotocol tst_workspace tst_linediff tst_agentcontroller)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE agentcore Qt5::Test)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()

    # Drives the real JS bridge against a local mock of the Copilot page.
    # Needs a Chromium that can start, so it is opt-in for headless CI.
    add_executable(tst_bridge tests/tst_bridge.cpp tests/mock.qrc)
    target_link_libraries(tst_bridge PRIVATE agentweb Qt5::Test Qt5::Widgets)
    add_test(NAME tst_bridge COMMAND tst_bridge)
    if(WIN32)
        set(_bridge_env "QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu")
    else()
        set(_bridge_env "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu")
    endif()
    set_tests_properties(tst_bridge PROPERTIES ENVIRONMENT "${_bridge_env}" LABELS webengine)
endif()
