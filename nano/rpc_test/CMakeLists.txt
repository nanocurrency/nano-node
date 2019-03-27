add_executable (rpc_test
	entry.cpp
	rpc.cpp)

target_link_libraries(rpc_test gtest gtest_main rpc)

target_compile_definitions(rpc_test
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
	PRIVATE
		-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})