add_executable (load_test
	entry.cpp)

target_link_libraries (load_test node secure Boost::boost Boost::coroutine)

target_compile_definitions(load_test
	PRIVATE
		-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})
