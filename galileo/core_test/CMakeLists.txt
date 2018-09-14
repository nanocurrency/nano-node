add_executable (core_test
	testutil.hpp
	block.cpp
	block_store.cpp
	interface.cpp
	conflicts.cpp
	daemon.cpp
	entry.cpp
	gap_cache.cpp
	ledger.cpp
	network.cpp
	node.cpp
	message.cpp
	message_parser.cpp
	processor_service.cpp
	peer_container.cpp
	rpc.cpp
	uint256_union.cpp
	versioning.cpp
	wallet.cpp
	wallets.cpp
	work_pool.cpp)

target_compile_definitions(core_test
		PRIVATE
			-DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
			-DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR})
target_link_libraries (core_test node secure gtest_main gtest libminiupnpc-static Boost::boost)
