#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/wallet.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/rpc_test/common.hpp>
#include <nano/rpc_test/rpc_context.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/confirmation_height.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <functional>

using namespace nano::test;

TEST (rpc, receivable)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	auto response = wait_response (system, rpc_ctx, request);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	nano::block_hash hash{ blocks_node.begin ()->second.get<std::string> ("") };
	ASSERT_EQ (block1->hash (), hash);
}

TEST (rpc, receivable_sorting)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("sorting", "true"); // Sorting test
	auto response = wait_response (system, rpc_ctx, request);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	nano::block_hash hash{ blocks_node.begin ()->first };
	ASSERT_EQ (block1->hash (), hash);
	std::string amount{ blocks_node.begin ()->second.get<std::string> ("") };
	ASSERT_EQ ("1", amount);
}

TEST (rpc, receivable_threshold_sufficient)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("threshold", "1"); // Threshold test
	auto response = wait_response (system, rpc_ctx, request);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	std::unordered_map<nano::block_hash, nano::uint128_union> blocks;
	for (auto i (blocks_node.begin ()), j (blocks_node.end ()); i != j; ++i)
	{
		nano::block_hash hash;
		hash.decode_hex (i->first);
		nano::uint128_union amount;
		amount.decode_dec (i->second.get<std::string> (""));
		blocks[hash] = amount;
		auto source = i->second.get_optional<std::string> ("source");
		ASSERT_FALSE (source.has_value ());
		auto min_version = i->second.get_optional<uint8_t> ("min_version");
		ASSERT_FALSE (min_version.has_value ());
	}
	ASSERT_EQ (blocks[block1->hash ()], 1);
}

TEST (rpc, receivable_threshold_insufficient)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("threshold", "2"); // Chains are set up with 1 raw transfers therefore all blocks are less than 2 raw.
	auto response = wait_response (system, rpc_ctx, request, 10s);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (0, blocks_node.size ());
}

TEST (rpc, receivable_source_min_version)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("source", "true");
	request.put ("min_version", "true");
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
	ASSERT_EQ (amounts[block1->hash ()], 1);
	ASSERT_EQ (sources[block1->hash ()], nano::dev::genesis_key.pub);
}

TEST (rpc, receivable_unconfirmed)
{
	nano::test::system system;
	nano::node_config config;
	config.backlog_scan->enable = false;
	auto node = add_ipc_enabled_node (system, config);
	auto chain = nano::test::setup_chain (system, *node, 1, nano::dev::genesis_key, false);
	auto block1 = chain[0];

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "false");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
	nano::test::confirm (node->ledger, block1);
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
}

/*TEST (rpc, amounts)
{
	auto block2 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 200));
	auto block3 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 300));
	auto block4 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 400));
	rpc_ctx.io_scope->renew ();

	ASSERT_TIMELY_EQ (10s, node->ledger.account_receivable (node->store.tx_begin_read (), key1.pub), 1000);
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
}*/

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
	ASSERT_TIMELY_EQ (5s, node->ledger.account_receivable (node->ledger.tx_begin_read (), key1.pub, true), 1600);

	// check confirmation height is as expected, there is no perfect clarity yet when confirmation height updates after a block get confirmed
	nano::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (node->store.confirmation_height.get (node->store.tx_begin_read (), nano::dev::genesis_key.pub, confirmation_height_info));
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
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, block));
	}
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "search_receivable");
	request.put ("wallet", wallet);
	auto response (wait_response (system, rpc_ctx, request));
	ASSERT_TIMELY_EQ (10s, node->balance (nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount);
}

TEST (rpc, accounts_pending_deprecated)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	boost::property_tree::ptree child;
	boost::property_tree::ptree accounts;
	child.put ("", nano::dev::genesis_key.pub.to_account ());
	accounts.push_back (std::make_pair ("", child));
	request.add_child ("accounts", accounts);
	request.put ("action", "accounts_pending");
	auto response (wait_response (system, rpc_ctx, request));
	ASSERT_EQ ("1", response.get<std::string> ("deprecated"));
}

TEST (rpc, accounts_receivable_blocks)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	auto response = wait_response (system, rpc_ctx, request);
	for (auto & blocks : response.get_child ("blocks"))
	{
		std::string account_text{ blocks.first };
		ASSERT_EQ (block1->destination ().to_account (), account_text);
		nano::block_hash hash1{ blocks.second.begin ()->second.get<std::string> ("") };
		ASSERT_EQ (block1->hash (), hash1);
	}
}

TEST (rpc, accounts_receivable_sorting)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	request.put ("sorting", "true"); // Sorting test
	auto response = wait_response (system, rpc_ctx, request);
	for (auto & blocks : response.get_child ("blocks"))
	{
		std::string account_text{ blocks.first };
		ASSERT_EQ (block1->destination ().to_account (), account_text);
		nano::block_hash hash1{ blocks.second.begin ()->first };
		ASSERT_EQ (block1->hash (), hash1);
		std::string amount{ blocks.second.begin ()->second.get<std::string> ("") };
		ASSERT_EQ ("1", amount);
	}
}

namespace
{
// Sends four state sends from genesis to the destination: three tied at amount 100 and one of 200; hashes exclude work, so every test node builds identical sends
void setup_tied_receivables (nano::node & node, nano::account const & destination, std::vector<nano::block_hash> & sends)
{
	nano::block_builder builder;
	auto latest = nano::dev::genesis->hash ();
	auto balance = nano::dev::constants.genesis_amount;
	for (auto const amount : { 100, 100, 100, 200 })
	{
		balance = balance - amount;
		auto send = builder.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (destination)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*node.work_generate_blocking (latest))
					.build ();
		ASSERT_EQ (nano::block_status::progress, node.process (send));
		sends.push_back (send->hash ());
		latest = send->hash ();
	}
}

// Reads a blocks subtree into (hash, amount) pairs preserving response order
std::vector<std::pair<std::string, std::string>> blocks_of (boost::property_tree::ptree const & blocks)
{
	std::vector<std::pair<std::string, std::string>> result;
	for (auto const & entry : blocks)
	{
		result.emplace_back (entry.first, entry.second.get<std::string> (""));
	}
	return result;
}
}

/*
 * With sorting the response must contain the top-count receivables by amount, hash descending on ties,
 * identically with and without the extended receivable index
 */
TEST (rpc, receivable_sorting_tie_parity)
{
	nano::keypair key1;

	// Runs the sorted queries on a fresh node and returns the responses along with the send hashes
	auto run = [&key1] (bool extended_index, std::vector<nano::block_hash> & sends, std::vector<std::vector<std::pair<std::string, std::string>>> & results) {
		nano::test::system system;
		nano::node_config config = system.default_config ();
		config.extended_ledger_index = extended_index;
		// Disable the backlog scan so no elections start; blocks with active elections are filtered from unconfirmed receivable responses
		config.backlog_scan->enable = false;
		auto node = add_ipc_enabled_node (system, config);
		ASSERT_EQ (extended_index, node->ledger.flags.account_receivable_by_amount_index);
		ASSERT_NO_FATAL_FAILURE (setup_tied_receivables (*node, key1.pub, sends));

		auto const rpc_ctx = add_rpc (system, node);
		for (auto const offset : { 0, 1 })
		{
			boost::property_tree::ptree request;
			request.put ("action", "receivable");
			request.put ("account", key1.pub.to_account ());
			// The sends deliberately stay unconfirmed, so the queries must accept unconfirmed receivables
			request.put ("include_only_confirmed", "false");
			request.put ("sorting", "true");
			request.put ("count", 2);
			request.put ("offset", offset);
			auto response = wait_response (system, rpc_ctx, request);
			results.push_back (blocks_of (response.get_child ("blocks")));
		}
	};

	std::vector<nano::block_hash> sends_legacy, sends_indexed;
	std::vector<std::vector<std::pair<std::string, std::string>>> legacy, indexed;
	ASSERT_NO_FATAL_FAILURE (run (false, sends_legacy, legacy));
	ASSERT_NO_FATAL_FAILURE (run (true, sends_indexed, indexed));
	ASSERT_EQ (sends_legacy, sends_indexed);
	ASSERT_EQ (legacy, indexed);

	// Tied amounts are ordered by hash descending
	std::vector<std::string> tied{ sends_legacy[0].to_string (), sends_legacy[1].to_string (), sends_legacy[2].to_string () };
	std::sort (tied.begin (), tied.end (), std::greater<> ());

	// count = 2: the 200 send first, then the highest tied hash
	std::vector<std::pair<std::string, std::string>> expected0{ { sends_legacy[3].to_string (), "200" }, { tied[0], "100" } };
	ASSERT_EQ (expected0, legacy[0]);
	// offset = 1 skips the 200 send
	std::vector<std::pair<std::string, std::string>> expected1{ { tied[0], "100" }, { tied[1], "100" } };
	ASSERT_EQ (expected1, legacy[1]);
}

/*
 * With sorting each account's response must contain the top-count receivables by amount rather than the first-count in hash order,
 * identically with and without the extended receivable index
 */
TEST (rpc, accounts_receivable_sorting_top_parity)
{
	nano::keypair key1;

	// Runs the sorted query on a fresh node and returns the response along with the send hashes
	auto run = [&key1] (bool extended_index, std::vector<nano::block_hash> & sends, std::vector<std::pair<std::string, std::string>> & result) {
		nano::test::system system;
		nano::node_config config = system.default_config ();
		config.extended_ledger_index = extended_index;
		// Disable the backlog scan so no elections start; blocks with active elections are filtered from unconfirmed receivable responses
		config.backlog_scan->enable = false;
		auto node = add_ipc_enabled_node (system, config);
		ASSERT_EQ (extended_index, node->ledger.flags.account_receivable_by_amount_index);
		ASSERT_NO_FATAL_FAILURE (setup_tied_receivables (*node, key1.pub, sends));

		auto const rpc_ctx = add_rpc (system, node);
		boost::property_tree::ptree request;
		request.put ("action", "accounts_receivable");
		boost::property_tree::ptree entry;
		boost::property_tree::ptree accounts_l;
		entry.put ("", key1.pub.to_account ());
		accounts_l.push_back (std::make_pair ("", entry));
		request.add_child ("accounts", accounts_l);
		// The sends deliberately stay unconfirmed, so the query must accept unconfirmed receivables
		request.put ("include_only_confirmed", "false");
		request.put ("sorting", "true");
		request.put ("count", 2);
		auto response = wait_response (system, rpc_ctx, request);
		result = blocks_of (response.get_child ("blocks").get_child (key1.pub.to_account ()));
	};

	std::vector<nano::block_hash> sends_legacy, sends_indexed;
	std::vector<std::pair<std::string, std::string>> legacy, indexed;
	ASSERT_NO_FATAL_FAILURE (run (false, sends_legacy, legacy));
	ASSERT_NO_FATAL_FAILURE (run (true, sends_indexed, indexed));
	ASSERT_EQ (sends_legacy, sends_indexed);
	ASSERT_EQ (legacy, indexed);

	// The top two by amount: the 200 send, then the highest tied 100 hash
	std::vector<std::string> tied{ sends_legacy[0].to_string (), sends_legacy[1].to_string (), sends_legacy[2].to_string () };
	std::sort (tied.begin (), tied.end (), std::greater<> ());
	std::vector<std::pair<std::string, std::string>> expected{ { sends_legacy[3].to_string (), "200" }, { tied[0], "100" } };
	ASSERT_EQ (expected, legacy);
}

TEST (rpc, accounts_receivable_threshold)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	request.put ("threshold", "1"); // Threshold test
	auto response = wait_response (system, rpc_ctx, request);
	std::unordered_map<nano::block_hash, nano::uint128_union> blocks;
	for (auto & pending : response.get_child ("blocks"))
	{
		std::string account_text{ pending.first };
		ASSERT_EQ (block1->destination ().to_account (), account_text);
		for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
		{
			nano::block_hash hash;
			hash.decode_hex (i->first);
			nano::uint128_union amount;
			amount.decode_dec (i->second.get<std::string> (""));
			blocks[hash] = amount;
			auto source = i->second.get_optional<std::string> ("source");
			ASSERT_FALSE (source.has_value ());
		}
	}
	ASSERT_EQ (blocks[block1->hash ()], 1);
}

TEST (rpc, accounts_receivable_source)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = nano::test::setup_chain (system, *node, 1, nano::dev::genesis_key);
	auto block1 = chain[0];

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	request.put ("source", "true");
	{
		auto response (wait_response (system, rpc_ctx, request));
		std::unordered_map<nano::block_hash, nano::uint128_union> amounts;
		std::unordered_map<nano::block_hash, nano::account> sources;
		for (auto & pending : response.get_child ("blocks"))
		{
			std::string account_text (pending.first);
			ASSERT_EQ (block1->destination ().to_account (), account_text);
			for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
			{
				nano::block_hash hash;
				hash.decode_hex (i->first);
				amounts[hash].decode_dec (i->second.get<std::string> ("amount"));
				sources[hash].decode_account (i->second.get<std::string> ("source"));
			}
		}
		ASSERT_EQ (amounts[block1->hash ()], 1);
		ASSERT_EQ (sources[block1->hash ()], nano::dev::genesis_key.pub);
	}
}

TEST (rpc, accounts_receivable_confirmed)
{
	nano::test::system system;
	nano::node_config config;
	config.backlog_scan->enable = false;
	auto node = add_ipc_enabled_node (system, config);
	auto chain = nano::test::setup_chain (system, *node, 1, nano::dev::genesis_key, false);
	auto block1 = chain[0];

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);

	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "false");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
	nano::test::confirm (node->ledger, block1);
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
}
