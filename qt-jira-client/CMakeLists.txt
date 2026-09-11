cmake_minimum_required(VERSION 3.16)

project(JiraDesk
    VERSION 1.0.0
    DESCRIPTION "Qt desktop client for Jira over the REST API v2"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Builds against Qt 6.2+ or Qt 5.15. "Auto" prefers Qt6 when both are
# installed; -DJIRADESK_QT_VERSION=5 pins the older one explicitly.
set(JIRADESK_QT_VERSION "Auto" CACHE STRING "Qt major version to build against: Auto, 5 or 6")
set_property(CACHE JIRADESK_QT_VERSION PROPERTY STRINGS Auto 5 6)

if(JIRADESK_QT_VERSION STREQUAL "Auto")
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)
else()
    find_package(QT NAMES Qt${JIRADESK_QT_VERSION} REQUIRED COMPONENTS Core Gui Widgets Network)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)

if(QT_VERSION_MAJOR EQUAL 5 AND Qt5_VERSION VERSION_LESS 5.15)
    # 5.15 is the floor: QNetworkRequest::setTransferTimeout arrived there, and
    # a request without a timeout can hang a window forever.
    message(FATAL_ERROR "Qt 5.15 or newer is required; found ${Qt5_VERSION}")
endif()

message(STATUS "Building against Qt ${Qt${QT_VERSION_MAJOR}_VERSION}")

option(JIRADESK_BUILD_TESTS "Build the unit tests" ON)

# The parsing, URL building and credential handling live in a library so the
# tests can exercise them without pulling in a window.
add_library(jiradesk_core STATIC
    src/core/credentials.cpp
    src/core/credentials.h
    src/core/jiraclient.cpp
    src/core/jiraclient.h
    src/core/jiratypes.cpp
    src/core/jiratypes.h
    src/core/timesheet.cpp
    src/core/timesheet.h
    src/core/timesheetloader.cpp
    src/core/timesheetloader.h
    src/core/timesheetexport.cpp
    src/core/timesheetexport.h
    src/core/tasktracker.cpp
    src/core/tasktracker.h
    src/core/xlsxwriter.cpp
    src/core/xlsxwriter.h
)
target_include_directories(jiradesk_core PUBLIC src)
target_link_libraries(jiradesk_core PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

# The .ui files are Qt Designer forms; AUTOUIC turns each into a ui_*.h that the
# matching .cpp includes. Listing them here keeps them visible in IDEs too.
# qt_add_executable is Qt6-only; Qt5 uses the plain command with AUTOUIC and
# AUTOMOC doing the same work.
if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(jiradesk)
else()
    add_executable(jiradesk)
endif()

target_sources(jiradesk PRIVATE
    src/main.cpp
    src/ui/connectiondialog.cpp
    src/ui/connectiondialog.h
    src/ui/connectiondialog.ui
    src/ui/issuedetailwidget.cpp
    src/ui/issuedetailwidget.h
    src/ui/issuedetailwidget.ui
    src/ui/issuetablemodel.cpp
    src/ui/issuetablemodel.h
    src/ui/logworkdialog.cpp
    src/ui/logworkdialog.h
    src/ui/logworkdialog.ui
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.ui
    src/ui/sprintwidget.cpp
    src/ui/sprintwidget.h
    src/ui/sprintwidget.ui
    src/ui/timesheetsettingsdialog.cpp
    src/ui/timesheetsettingsdialog.h
    src/ui/timesheetsettingsdialog.ui
    src/ui/timesheetwidget.cpp
    src/ui/timesheetwidget.h
    src/ui/timesheetwidget.ui
)
target_link_libraries(jiradesk PRIVATE jiradesk_core Qt${QT_VERSION_MAJOR}::Widgets)

set_target_properties(jiradesk PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(JIRADESK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS jiradesk
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
