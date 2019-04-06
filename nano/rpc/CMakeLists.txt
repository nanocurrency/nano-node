if (NANO_SECURE_RPC OR RAIBLOCKS_SECURE_RPC)
	set (secure_rpc_sources rpcsecure.hpp rpcsecure.cpp rpcconnectionsecure.hpp rpcconnectionsecure.cpp)
endif ()

add_library (rpc
	${secure_rpc_sources}
	rpc.hpp
	rpc.cpp
	rpcconnection.hpp
	rpcconnection.cpp
	rpchandler.hpp
	rpchandler.cpp
	rpcrequestprocessor.hpp
	rpcrequestprocessor.cpp)

target_link_libraries (rpc
	nano_lib
	${OPENSSL_LIBRARIES}
	Boost::boost)
