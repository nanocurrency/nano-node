add_executable (nano_node
	daemon.cpp
	daemon.hpp
	entry.cpp)

target_link_libraries (nano_node
	node
	rpc
	secure
	argon2
	Boost::boost
	${PLATFORM_LIBS}
)

if (NANO_STACKTRACE_BACKTRACE)
	target_link_libraries(nano_node backtrace)
endif ()

target_compile_definitions(nano_node
	PRIVATE
		-DTAG_VERSION_STRING=${TAG_VERSION_STRING}
		-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH}
		-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})

set_target_properties (nano_node
	PROPERTIES
		COMPILE_FLAGS
			"-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

if ((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
	install(TARGETS nano_node
			RUNTIME DESTINATION .
	)
endif()
