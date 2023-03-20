#include <nano/rpc_test/common.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace nano::test;

TEST (rpc, receivable)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	nano::keypair key1;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto block1 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 100));
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", key1.pub.to_account ());
	request.put ("count", "100");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (1, blocks_node.size ());
		nano::block_hash hash (blocks_node.begin ()->second.get<std::string> (""));
		ASSERT_EQ (block1->hash (), hash);
	}
	request.put ("sorting", "true"); // Sorting test
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (1, blocks_node.size ());
		nano::block_hash hash (blocks_node.begin ()->first);
		ASSERT_EQ (block1->hash (), hash);
		std::string amount (blocks_node.begin ()->second.get<std::string> (""));
		ASSERT_EQ ("100", amount);
	}
	request.put ("threshold", "100"); // Threshold test
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (1, blocks_node.size ());
		std::unordered_map<nano::block_hash, nano::uint128_union> blocks;
		for (auto i (blocks_node.begin ()), j (blocks_node.end ()); i != j; ++i)
		{
			nano::block_hash hash;
			hash.decode_hex (i->first);
			nano::uint128_union amount;
			amount.decode_dec (i->second.get<std::string> (""));
			blocks[hash] = amount;
			boost::optional<std::string> source (i->second.get_optional<std::string> ("source"));
			ASSERT_FALSE (source.is_initialized ());
			boost::optional<uint8_t> min_version (i->second.get_optional<uint8_t> ("min_version"));
			ASSERT_FALSE (min_version.is_initialized ());
		}
		ASSERT_EQ (blocks[block1->hash ()], 100);
	}
	request.put ("threshold", "101");
	{
		auto response (wait_response (system, rpc_ctx, request, 10s));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (0, blocks_node.size ());
	}
	request.put ("threshold", "0");
	request.put ("source", "true");
	request.put ("min_version", "true");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (1, blocks_node.size ());
		std::unordered_map<nano::block_hash, nano::uint128_union> amounts;
		std::unordered_map<nano::block_hash, nano::account> sources;
		for (auto i (blocks_node.begin ()), j (blocks_node.end ()); i != j; ++i)
		{
			nano::block_hash hash;
			hash.decode_hex (i->first);
			amounts[hash].decode_dec (i->second.get<std::string> ("amount"));
			sources[hash].decode_account (i->second.get<std::string> ("source"));
			ASSERT_EQ (i->second.get<uint8_t> ("min_version"), 0);
		}
		ASSERT_EQ (amounts[block1->hash ()], 100);
		ASSERT_EQ (sources[block1->hash ()], nano::dev::genesis_key.pub);
	}

	request.put ("account", key1.pub.to_account ());
	request.put ("source", "false");
	request.put ("min_version", "false");

	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
	rpc_ctx.io_scope->reset ();
	reset_confirmation_height (system.nodes.front ()->store, block1->account ());
	rpc_ctx.io_scope->renew ();
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "false");
	rpc_ctx.io_scope->renew ();
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
	request.put ("include_only_confirmed", "true");

	// Sorting with a smaller count than total should give absolute sorted amounts
	rpc_ctx.io_scope->reset ();
	node->store.confirmation_height.put (node->store.tx_begin_write (), nano::dev::genesis_key.pub, { 2, block1->hash () });
	auto block2 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 200));
	auto block3 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 300));
	auto block4 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 400));
	rpc_ctx.io_scope->renew ();

	ASSERT_TIMELY (10s, node->ledger.account_receivable (node->store.tx_begin_read (), key1.pub) == 1000);
	ASSERT_TIMELY (5s, !node->active.active (*block4));
	ASSERT_TIMELY (5s, node->block_confirmed (block4->hash ()));

	request.put ("count", "2");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (2, blocks_node.size ());
		nano::block_hash hash (blocks_node.begin ()->first);
		nano::block_hash hash1 ((++blocks_node.begin ())->first);
		ASSERT_EQ (block4->hash (), hash);
		ASSERT_EQ (block3->hash (), hash1);
	}
}

/**
 * This test case tests the receivable RPC command when used with offsets and sorting.
 */
TEST (rpc, receivable_offset_and_sorting)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	nano::keypair key1;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	auto block1 = system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 200);
	auto block2 = system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 100);
	auto block3 = system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 400);
	auto block4 = system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 300);
	auto block5 = system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 300);
	auto block6 = system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 300);

	// check that all blocks got confirmed
	ASSERT_TIMELY (5s, node->ledger.account_receivable (node->store.tx_begin_read (), key1.pub, true) == 1600);

	// check confirmation height is as expected, there is no perfect clarity yet when confirmation height updates after a block get confirmed
	nano::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (node->store.confirmation_height.get (node->store.tx_begin_read (), nano::dev::genesis->account (), confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 7);
	ASSERT_EQ (confirmation_height_info.frontier, block6->hash ());

	// returns true if hash is found in node
	// if match_first is set then the function looks for key (first item)
	// if match_first is not set then the function looks for value (second item)
	auto hash_exists = [] (boost::property_tree::ptree & node, bool match_first, nano::block_hash hash) {
		std::stringstream ss;
		boost::property_tree::json_parser::write_json (ss, node);
		for (auto itr = node.begin (); itr != node.end (); ++itr)
		{
			std::string possible_match = match_first ? itr->first : itr->second.get<std::string> ("");
			if (possible_match == hash.to_string ())
			{
				return true;
			}
		}
		return false;
	};

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", key1.pub.to_account ());

	request.put ("offset", "0");
	request.put ("sorting", "false");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (6, blocks_node.size ());

		// check that all 6 blocks are listed, the order does not matter
		ASSERT_TRUE (hash_exists (blocks_node, false, block1->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block2->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block3->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block4->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block5->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block6->hash ()));
	}

	request.put ("offset", "4");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		// since we haven't asked for sorted, we can't be sure which 2 blocks will be returned
		ASSERT_EQ (2, blocks_node.size ());
	}

	request.put ("count", "2");
	request.put ("offset", "2");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		// since we haven't asked for sorted, we can't be sure which 2 blocks will be returned
		ASSERT_EQ (2, blocks_node.size ());
	}

	// Sort by amount from here onwards, this is a sticky setting that applies for the rest of the test case
	request.put ("sorting", "true");

	request.put ("count", "5");
	request.put ("offset", "0");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (5, blocks_node.size ());

		// the first block should be block3 with amount 400
		auto itr = blocks_node.begin ();
		ASSERT_EQ (block3->hash (), nano::block_hash{ itr->first });
		ASSERT_EQ ("400", itr->second.get<std::string> (""));

		// the next 3 block will be of amount 300 but in unspecified order
		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		// the last one will be block1 with amount 200
		++itr;
		ASSERT_EQ (block1->hash (), nano::block_hash{ itr->first });
		ASSERT_EQ ("200", itr->second.get<std::string> (""));

		// check that the blocks returned with 300 amounts have the right hashes
		ASSERT_TRUE (hash_exists (blocks_node, true, block4->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, true, block5->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, true, block6->hash ()));
	}

	request.put ("count", "3");
	request.put ("offset", "3");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (3, blocks_node.size ());

		auto itr = blocks_node.begin ();
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ (block1->hash (), nano::block_hash{ itr->first });
		ASSERT_EQ ("200", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ (block2->hash (), nano::block_hash{ itr->first });
		ASSERT_EQ ("100", itr->second.get<std::string> (""));
	}

	request.put ("source", "true");
	request.put ("min_version", "true");
	request.put ("count", "3");
	request.put ("offset", "2");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (3, blocks_node.size ());

		auto itr = blocks_node.begin ();
		ASSERT_EQ ("300", itr->second.get<std::string> ("amount"));

		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> ("amount"));

		++itr;
		ASSERT_EQ (block1->hash (), nano::block_hash{ itr->first });
		ASSERT_EQ ("200", itr->second.get<std::string> ("amount"));
	}
}

TEST (rpc, receivable_burn)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto block1 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, nano::dev::constants.burn_account, 100));
	auto const rpc_ctx = add_rpc (system, node);
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", nano::dev::constants.burn_account.to_account ());
	request.put ("count", "100");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (1, blocks_node.size ());
		nano::block_hash hash (blocks_node.begin ()->second.get<std::string> (""));
		ASSERT_EQ (block1->hash (), hash);
	}
}

TEST (rpc, search_receivable)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto wallet (node->wallets.items.begin ()->first.to_string ());
	auto latest (node->latest (nano::dev::genesis_key.pub));
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (latest)
				 .destination (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - node->config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node->work_generate_blocking (latest))
				 .build ();
	{
		auto transaction (node->store.tx_begin_write ());
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *block).code);
	}
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "search_receivable");
	request.put ("wallet", wallet);
	auto response (wait_response (system, rpc_ctx, request));
	ASSERT_TIMELY (10s, node->balance (nano::dev::genesis_key.pub) == nano::dev::constants.genesis_amount);
}
