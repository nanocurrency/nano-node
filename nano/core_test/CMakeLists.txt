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
	signing.cpp
	timer.cpp
	uint256_union.cpp
	versioning.cpp
	wallet.cpp
	wallets.cpp
	work_pool.cpp)

target_compile_definitions(core_test
		PRIVATE
			-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
			-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
			-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH}
			-DNANO_MAJOR_MINOR_VERSION="${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}"
			-DNANO_MAJOR_MINOR_RC_VERSION="${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}RC${CPACK_PACKAGE_VERSION_PATCH}")
target_link_libraries (core_test node secure gtest_main gtest libminiupnpc-static Boost::boost)
