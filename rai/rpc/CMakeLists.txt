if (RAIBLOCKS_SECURE_RPC)
	set (secure_rpc_sources rpc_secure.cpp rpc_secure.hpp)
endif ()

add_library (rpc
	${secure_rpc_sources}
	rpc.hpp
	rpc.cpp
	rpc_handler.hpp
	rpc_handler.cpp
	rpc_handler_account.cpp
	rpc_handler_block.cpp
	rpc_handler_misc.cpp
	rpc_handler_payment.cpp	
	rpc_handler_wallet.cpp
	rpc_handler_work.cpp
	)

target_link_libraries (rpc
	secure
	rai_lib
	libminiupnpc-static
	)

target_compile_definitions(rpc
	PRIVATE
		-DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR})
