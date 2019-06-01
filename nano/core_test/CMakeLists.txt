add_executable (core_test
	core_test_main.cc
	testutil.hpp
	active_transactions.cpp
	block.cpp
	block_store.cpp
	conflicts.cpp
	difficulty.cpp
	entry.cpp
	gap_cache.cpp
	interface.cpp
	ipc.cpp
	ledger.cpp
	logger.cpp
	network.cpp
	node.cpp
	message.cpp
	message_parser.cpp
	memory_pool.cpp
	processor_service.cpp
	peer_container.cpp
	signing.cpp
	socket.cpp
	timer.cpp
	uint256_union.cpp
	versioning.cpp
	wallet.cpp
	wallets.cpp
	websocket.cpp
	work_pool.cpp)

target_compile_definitions(core_test
		PRIVATE
			-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
			-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
			-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH}
			-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})
target_link_libraries (core_test node secure gtest libminiupnpc-static Boost::boost)
