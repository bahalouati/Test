cmake_minimum_required(VERSION 3.16)

project(CopilotAgent
    VERSION 0.1.0
    DESCRIPTION "Drives Microsoft 365 Copilot chat in Qt WebEngine as a coding agent"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Builds against Qt 5.12+ or Qt 6.2+. "Auto" prefers Qt6 when both are
# installed; -DCOPILOTAGENT_QT_VERSION=5 pins Qt 5 explicitly.
set(COPILOTAGENT_QT_VERSION "Auto" CACHE STRING "Qt major version to build against: Auto, 5 or 6")
set_property(CACHE COPILOTAGENT_QT_VERSION PROPERTY STRINGS Auto 5 6)

set(_qt_components Core Gui Widgets WebEngineCore WebEngineWidgets)
if(COPILOTAGENT_QT_VERSION STREQUAL "Auto")
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${_qt_components})
else()
    find_package(QT NAMES Qt${COPILOTAGENT_QT_VERSION} REQUIRED COMPONENTS ${_qt_components})
endif()
set(QtX Qt${QT_VERSION_MAJOR})
find_package(${QtX} REQUIRED COMPONENTS ${_qt_components})

if(QT_VERSION_MAJOR EQUAL 5 AND Qt5_VERSION VERSION_LESS 5.12)
    message(FATAL_ERROR "Qt 5.12 or newer is required; found ${Qt5_VERSION}")
endif()
if(QT_VERSION_MAJOR EQUAL 6 AND Qt6_VERSION VERSION_LESS 6.2)
    message(FATAL_ERROR "Qt 6.2 or newer is required (WebEngine returned in 6.2); found ${Qt6_VERSION}")
endif()
message(STATUS "Building against Qt ${${QtX}_VERSION}")

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
    # Qt < 5.15.3 headers use std::numeric_limits without including <limits>,
    # which GCC 11+ no longer pulls in transitively.
    if(QT_VERSION_MAJOR EQUAL 5 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
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
target_link_libraries(agentcore PUBLIC ${QtX}::Core)

# The WebEngine side: the JS bridge that types into Copilot and reads replies.
# In Qt 5, QWebEnginePage and QWebEngineProfile live in WebEngineWidgets.
add_library(agentweb STATIC
    src/copilotbridge.h      src/copilotbridge.cpp
    src/bridge.qrc
)
target_include_directories(agentweb PUBLIC src)
target_link_libraries(agentweb PUBLIC ${QtX}::Core ${QtX}::WebEngineCore ${QtX}::WebEngineWidgets)

add_executable(copilot-agent WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.h         src/mainwindow.cpp
    src/approvaldialog.h     src/approvaldialog.cpp
    src/settingsdialog.h     src/settingsdialog.cpp
    src/webpage.h            src/webpage.cpp
)
target_link_libraries(copilot-agent PRIVATE
    agentcore agentweb ${QtX}::Widgets ${QtX}::WebEngineWidgets)

install(TARGETS copilot-agent
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)

if(COPILOTAGENT_BUILD_TESTS)
    enable_testing()
    find_package(${QtX} REQUIRED COMPONENTS Test)

    foreach(name tst_agentprotocol tst_workspace tst_linediff tst_agentcontroller)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE agentcore ${QtX}::Test)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()

    # Drives the real JS bridge against a local mock of the Copilot page.
    # Needs a Chromium that can start, so it is opt-in for headless CI.
    add_executable(tst_bridge tests/tst_bridge.cpp tests/mock.qrc)
    target_link_libraries(tst_bridge PRIVATE agentweb ${QtX}::Test ${QtX}::Widgets)
    add_test(NAME tst_bridge COMMAND tst_bridge)
    set_tests_properties(tst_bridge PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu"
        LABELS webengine)
endif()
