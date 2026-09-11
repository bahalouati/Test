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

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network)

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
)
target_include_directories(jiradesk_core PUBLIC src)
target_link_libraries(jiradesk_core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(jiradesk
    src/main.cpp
    src/ui/connectiondialog.cpp
    src/ui/connectiondialog.h
    src/ui/issuedetailwidget.cpp
    src/ui/issuedetailwidget.h
    src/ui/issuetablemodel.cpp
    src/ui/issuetablemodel.h
    src/ui/logworkdialog.cpp
    src/ui/logworkdialog.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/timesheetsettingsdialog.cpp
    src/ui/timesheetsettingsdialog.h
    src/ui/timesheetwidget.cpp
    src/ui/timesheetwidget.h
)
target_link_libraries(jiradesk PRIVATE jiradesk_core Qt6::Widgets)

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
