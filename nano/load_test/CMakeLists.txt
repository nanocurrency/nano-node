add_executable (load_test
	entry.cpp)

target_link_libraries (load_test node secure test_common gtest Boost::boost)

target_compile_definitions(load_test
	PRIVATE
		-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})
