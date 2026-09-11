find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(tst_jiracore tst_jiracore.cpp)
else()
    add_executable(tst_jiracore tst_jiracore.cpp)
endif()

target_link_libraries(tst_jiracore PRIVATE jiradesk_core Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME jiracore COMMAND tst_jiracore)
