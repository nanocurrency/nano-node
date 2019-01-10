if (NOT WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=shadow")
endif ()

add_executable (slow_test
	node.cpp)

target_link_libraries (slow_test node secure gtest_main gtest libminiupnpc-static Boost::boost)
