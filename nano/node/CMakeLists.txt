if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	# No opencl
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set (platform_sources plat/windows/openclapi.cpp)
	set (psapi_lib Psapi)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set (platform_sources plat/posix/openclapi.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	set (platform_sources plat/posix/openclapi.cpp)
else ()
	error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif ()

add_library (node
	${platform_sources}
	active_transactions.hpp
	active_transactions.cpp
	blockprocessor.cpp
	blockprocessor.hpp
	blockprocessor.cpp
	bootstrap.hpp
	bootstrap.cpp
	cli.hpp
	cli.cpp
	common.hpp
	common.cpp
	confirmation_height_processor.hpp
	confirmation_height_processor.cpp
	daemonconfig.hpp
	daemonconfig.cpp
	diagnosticsconfig.hpp
	diagnosticsconfig.cpp
	election.hpp
	election.cpp
	gap_cache.hpp
	gap_cache.cpp
	ipc.hpp
	ipc.cpp
	ipcconfig.hpp
	ipcconfig.cpp
	json_handler.hpp
	json_handler.cpp
	json_payment_observer.hpp	
	json_payment_observer.cpp
	lmdb.hpp
	lmdb.cpp
	lmdb_txn_tracker.hpp
	lmdb_txn_tracker.cpp
	logging.hpp
	logging.cpp
	network.hpp
	network.cpp
	nodeconfig.hpp
	nodeconfig.cpp
	node_observers.hpp
	node_observers.cpp
	node_rpc_config.hpp
	node_rpc_config.cpp
	node.hpp
	node.cpp
	online_reps.hpp
	online_reps.cpp
	openclconfig.hpp
	openclconfig.cpp
	openclwork.hpp
	openclwork.cpp
	payment_observer_processor.hpp
	payment_observer_processor.cpp
	portmapping.hpp
	portmapping.cpp
	repcrawler.hpp
	repcrawler.cpp
	testing.hpp
	testing.cpp
	transport/tcp.hpp
	transport/tcp.cpp
	transport/transport.hpp
	transport/transport.cpp
	transport/udp.hpp
	transport/udp.cpp
	signatures.hpp
	signatures.cpp
	socket.hpp
	socket.cpp
	vote_processor.hpp
	vote_processor.cpp
	voting.hpp
	voting.cpp
	wallet.hpp
	wallet.cpp
	websocket.hpp
	websocket.cpp
	websocketconfig.hpp
	websocketconfig.cpp
	write_database_queue.hpp
	write_database_queue.cpp
	xorshift.hpp)

target_link_libraries (node
	rpc
	secure
	nano_lib
	libminiupnpc-static
	argon2
	lmdb
	Boost::filesystem
	Boost::log
	Boost::log_setup
	Boost::program_options
	Boost::system
	Boost::thread
	Boost::boost
	${CMAKE_DL_LIBS}
	${psapi_lib}
	)

target_compile_definitions(node
	PRIVATE
		-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})
