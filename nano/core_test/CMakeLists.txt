if (NANO_ROCKSDB)
  set (rocksdb_test rocksdb_test.cpp)
  set (rocksdb_libs ${ROCKSDB_LIBRARIES} ${ZLIB_LIBRARIES})
endif ()

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
	${rocksdb_test}
	signing.cpp
	socket.cpp
	toml.cpp
	timer.cpp
	uint256_union.cpp
	utility.cpp
	versioning.cpp
	wallet.cpp
	wallets.cpp
	websocket.cpp
	work_pool.cpp)

target_compile_definitions(core_test
		PRIVATE
			-DTAG_VERSION_STRING=${TAG_VERSION_STRING}
			-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH}
			-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})
target_link_libraries (core_test node secure gtest libminiupnpc-static Boost::boost ${rocksdb_libs})
