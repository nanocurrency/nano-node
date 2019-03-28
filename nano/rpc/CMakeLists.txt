if (NANO_SECURE_RPC OR RAIBLOCKS_SECURE_RPC)
	set (secure_rpc_sources rpc_secure.hpp rpc_secure.cpp rpc_connection_secure.hpp rpc_connection_secure.cpp)
endif ()

add_library (rpc
	${secure_rpc_sources}
	rpc.hpp
	rpc.cpp
	rpc_connection.hpp
	rpc_connection.cpp
	rpc_handler.hpp
	rpc_handler.cpp)

target_link_libraries (rpc
	node
	nano_lib
	${OPENSSL_LIBRARIES}
	Boost::boost)

target_compile_definitions(rpc
	PRIVATE
	-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
	-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
	-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})