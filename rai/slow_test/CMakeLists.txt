add_executable (slow_test
	node.cpp)

target_link_libraries (slow_test node secure gtest_main gtest libminiupnpc-static Boost::boost)
target_include_directories (slow_test PRIVATE ${CMAKE_SOURCE_DIR}/gtest/include)
