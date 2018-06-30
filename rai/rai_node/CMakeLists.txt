add_executable (rai_node
	daemon.cpp
	daemon.hpp
	entry.cpp)

target_link_libraries (rai_node
	node
	secure
	argon2
	Boost::boost
)

set_target_properties (rai_node
	PROPERTIES
		COMPILE_FLAGS
			"-DQT_NO_KEYWORDS -DACTIVE_NETWORK=${ACTIVE_NETWORK} -DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR} -DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR} -DBOOST_ASIO_HAS_STD_ARRAY=1")
# TODO: Do globally
set_target_properties (rai_node PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS}")

if (RAIBLOCKS_GUI AND NOT APPLE)
	install(TARGETS rai_node
			RUNTIME DESTINATION bin
			LIBRARY DESTINATION lib
			ARCHIVE DESTINATION lib
	)
endif()
