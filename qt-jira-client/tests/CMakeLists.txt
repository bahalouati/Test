find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

qt_add_executable(tst_jiracore tst_jiracore.cpp)
target_link_libraries(tst_jiracore PRIVATE jiradesk_core Qt6::Test)
add_test(NAME jiracore COMMAND tst_jiracore)
