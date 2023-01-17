add_executable(load_test entry.cpp)

target_link_libraries(load_test boost_process node secure test_common gtest)
