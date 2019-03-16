add_library (rpc
	rpc.cpp
	rpc.hpp
	rpc_connection.cpp
	rpc_connection.hpp
	rpc_handler.cpp
	rpc_handler.hpp)

target_link_libraries (rpc	
	nano_lib
	${CMAKE_DL_LIBS})

target_compile_definitions(node
	PRIVATE
		-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})
