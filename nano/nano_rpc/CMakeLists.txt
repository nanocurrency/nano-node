add_executable (nano_rpc
	entry.cpp)

target_link_libraries (nano_rpc
	rpc
	secure
	Boost::filesystem
	Boost::log
	Boost::log_setup
	Boost::program_options
	Boost::system
	Boost::thread
	Boost::boost)

target_compile_definitions(nano_rpc
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
	PRIVATE
		-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})

if ((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
	install(TARGETS nano_rpc
			RUNTIME DESTINATION .
	)
endif()
