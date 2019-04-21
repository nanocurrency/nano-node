add_executable (slow_test
	entry.cpp
	node.cpp)

target_link_libraries (slow_test node secure gtest libminiupnpc-static Boost::boost)
