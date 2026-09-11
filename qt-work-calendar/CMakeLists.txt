cmake_minimum_required(VERSION 3.19)

project(WorkCalendar
    VERSION 1.0.0
    DESCRIPTION "Daily work log and calendar"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt code generators. AUTOUIC turns the .ui files into ui_*.h headers,
# AUTOMOC handles Q_OBJECT, AUTORCC handles the .qrc resource file.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Sql Network)

# Everything except main() lives in this library so the tests can link it.
add_library(workcalendar_core STATIC
    src/core/Enums.h                    src/core/Enums.cpp
    src/core/Duration.h                 src/core/Duration.cpp
    src/core/WorkEntry.h                src/core/WorkEntry.cpp
    src/core/DayMeta.h                  src/core/DayMeta.cpp
    src/core/DaySummary.h               src/core/DaySummary.cpp
    src/core/Settings.h                 src/core/Settings.cpp

    src/data/Database.h                 src/data/Database.cpp
    src/data/WorkEntryRepository.h      src/data/WorkEntryRepository.cpp
    src/data/DayMetaRepository.h        src/data/DayMetaRepository.cpp
    src/data/Statistics.h               src/data/Statistics.cpp

    src/model/WorkEntryTableModel.h     src/model/WorkEntryTableModel.cpp
    src/model/EntryFilterProxyModel.h   src/model/EntryFilterProxyModel.cpp
    src/model/BreakdownTableModel.h     src/model/BreakdownTableModel.cpp

    src/widgets/MonthGridWidget.h       src/widgets/MonthGridWidget.cpp
    src/widgets/HoursBarChart.h         src/widgets/HoursBarChart.cpp

    src/export/XlsxWriter.h             src/export/XlsxWriter.cpp
    src/export/ExcelReport.h            src/export/ExcelReport.cpp
    src/export/CsvIo.h                  src/export/CsvIo.cpp
    src/export/TextReport.h             src/export/TextReport.cpp

    src/jira/JiraClient.h               src/jira/JiraClient.cpp

    # The windows and dialogs live in the library too, so the tests can build a
    # real MainWindow and drive it. Only main() stays in the executable.
    src/ui/MainWindow.h       src/ui/MainWindow.cpp       src/ui/MainWindow.ui
    src/ui/EntryDialog.h      src/ui/EntryDialog.cpp      src/ui/EntryDialog.ui
    src/ui/DayMetaDialog.h    src/ui/DayMetaDialog.cpp    src/ui/DayMetaDialog.ui
    src/ui/SettingsDialog.h   src/ui/SettingsDialog.cpp   src/ui/SettingsDialog.ui
    src/ui/ColumnsDialog.h    src/ui/ColumnsDialog.cpp    src/ui/ColumnsDialog.ui
    src/ui/JiraImportDialog.h src/ui/JiraImportDialog.cpp src/ui/JiraImportDialog.ui
    src/ui/AboutDialog.h      src/ui/AboutDialog.cpp      src/ui/AboutDialog.ui
)

target_include_directories(workcalendar_core PUBLIC src)
target_link_libraries(workcalendar_core PUBLIC Qt6::Widgets Qt6::Sql Qt6::Network)

add_executable(workcalendar
    src/main.cpp
    resources/resources.qrc
)
target_link_libraries(workcalendar PRIVATE workcalendar_core)
set_target_properties(workcalendar PROPERTIES WIN32_EXECUTABLE ON)

install(TARGETS workcalendar RUNTIME DESTINATION bin)

# ---------------------------------------------------------------- tests
option(WORKCALENDAR_BUILD_TESTS "Build the unit tests" ON)

if(WORKCALENDAR_BUILD_TESTS)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    enable_testing()

    foreach(test_name tst_duration tst_repository tst_statistics tst_csv tst_xlsx tst_mainwindow)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE workcalendar_core Qt6::Test)
        add_test(NAME ${test_name} COMMAND ${test_name})

        # The window test needs a QApplication; "offscreen" lets it run on a
        # build machine with no display, and harms nothing on a desktop.
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
