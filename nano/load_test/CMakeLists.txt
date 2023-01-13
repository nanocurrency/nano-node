add_executable(load_test entry.cpp)

target_link_libraries(load_test node secure test_common gtest)
