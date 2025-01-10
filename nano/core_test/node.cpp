#include <celerix/lib/blocks.hpp>
#include <celerix/lib/config.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/work_version.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/confirming_set.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/inactive_node.hpp>
#include <celerix/node/local_vote_history.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/portmapping.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/manual.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/transport/fake.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/node/transport/tcp_listener.hpp>
#include <celerix/node/vote_generator.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/test_common/chains.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <numeric>

using namespace std::chrono_literals;

TEST (node, null_account)
{
	auto const & null_account = celerix::account::null ();
	ASSERT_EQ (null_account, nullptr);
	ASSERT_FALSE (null_account != nullptr);

	celerix::account default_account{};
	ASSERT_FALSE (default_account == nullptr);
	ASSERT_NE (default_account, nullptr);
}

TEST (node, stop)
{
	celerix::test::system system (1);
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.begin ());
	system.stop_node (*system.nodes[0]);
	ASSERT_TRUE (true);
}

TEST (node, work_generate)
{
	celerix::test::system system (1);
	auto & node (*system.nodes[0]);
	celerix::block_hash root{ 1 };
	celerix::work_version version{ celerix::work_version::work_1 };
	{
		auto difficulty = celerix::difficulty::from_multiplier (1.5, node.network_params.work.base);
		auto work = node.work_generate_blocking (version, root, difficulty);
		ASSERT_TRUE (work.has_value ());
		ASSERT_GE (celerix::dev::network_params.work.difficulty (version, root, work.value ()), difficulty);
	}
	{
		auto difficulty = celerix::difficulty::from_multiplier (0.5, node.network_params.work.base);
		std::optional<uint64_t> work;
		do
		{
			work = node.work_generate_blocking (version, root, difficulty);
		} while (celerix::dev::network_params.work.difficulty (version, root, work.value ()) >= node.network_params.work.base);
		ASSERT_TRUE (work.has_value ());
		ASSERT_GE (celerix::dev::network_params.work.difficulty (version, root, work.value ()), difficulty);
		ASSERT_FALSE (celerix::dev::network_params.work.difficulty (version, root, work.value ()) >= node.network_params.work.base);
	}
}

TEST (node, block_store_path_failure)
{
	celerix::test::system system;
	auto path (celerix::unique_path ());
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto node (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), path, pool));
	system.register_node (node);
	ASSERT_TRUE (node->wallets.items.empty ());
}

#if defined(__clang__) && defined(__linux__) && CI
// Disable test due to instability with clang and actions
TEST (node_DeathTest, DISABLED_readonly_block_store_not_exist)
#else
TEST (node_DeathTest, readonly_block_store_not_exist)
#endif
{
	// This is a read-only node with no ledger file
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		celerix::inactive_node node (celerix::unique_path (), celerix::inactive_node_flag_defaults ());
		ASSERT_TRUE (node.node->init_error ());
	}
	else
	{
		ASSERT_EXIT (celerix::inactive_node node (celerix::unique_path (), celerix::inactive_node_flag_defaults ()), ::testing::ExitedWithCode (1), "");
	}
}

TEST (node, password_fanout)
{
	celerix::test::system system;
	celerix::node_config config;
	config.peering_port = system.get_available_port ();
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	config.password_fanout = 10;
	auto & node = *system.add_node (config);
	auto wallet (node.wallets.create (100));
	ASSERT_EQ (10, wallet->store.password.values.size ());
}

TEST (node, balance)
{
	celerix::test::system system (1);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto transaction = system.nodes[0]->ledger.tx_begin_write ();
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max (), system.nodes[0]->ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
}

TEST (node, send_unkeyed)
{
	celerix::test::system system (1);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->store.password.value_set (celerix::keypair ().prv);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
}

TEST (node, send_self)
{
	celerix::test::system system (1);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (celerix::dev::genesis_key.pub));
}

TEST (node, send_single)
{
	celerix::test::system system (2);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (celerix::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
}

TEST (node, send_single_observing_peer)
{
	celerix::test::system system (3);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (celerix::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<celerix::node> const & node_a) { return !node_a->balance (key2.pub).is_zero (); }));
}

TEST (node, send_out_of_order)
{
	celerix::test::system system (2);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key2;
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - 2 * node1.config.receive_minimum.number ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .previous (send2->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - 3 * node1.config.receive_minimum.number ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	node1.process_active (send3);
	node1.process_active (send2);
	node1.process_active (send1);
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<celerix::node> const & node_a) { return node_a->balance (celerix::dev::genesis_key.pub) == celerix::dev::constants.genesis_amount - node1.config.receive_minimum.number () * 3; }));
}

TEST (node, quick_confirm)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key;
	celerix::block_hash previous (node1.latest (celerix::dev::genesis_key.pub));
	auto genesis_start_balance (node1.balance (celerix::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto send = celerix::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (node1.online_reps.delta () + 1)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, !node1.balance (key.pub).is_zero ());
	ASSERT_EQ (node1.balance (celerix::dev::genesis_key.pub), node1.online_reps.delta () + 1);
	ASSERT_EQ (node1.balance (key.pub), genesis_start_balance - (node1.online_reps.delta () + 1));
}

TEST (node, node_receive_quorum)
{
	celerix::test::system system (1);
	auto & node1 = *system.nodes[0];
	celerix::keypair key;
	celerix::block_hash previous (node1.latest (celerix::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	auto send = celerix::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, node1.block_or_pruned_exists (send->hash ()));
	ASSERT_TIMELY (10s, node1.active.election (celerix::qualified_root (previous, previous)) != nullptr);
	auto election (node1.active.election (celerix::qualified_root (previous, previous)));
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());

	celerix::test::system system2;
	system2.add_node ();

	system2.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_TRUE (node1.balance (key.pub).is_zero ());
	node1.network.tcp_channels.start_tcp (system2.nodes[0]->network.endpoint ());
	while (node1.balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, auto_bootstrap)
{
	celerix::test::system system;
	celerix::node_config config (system.get_available_port ());
	config.backlog_scan.enable = false;
	celerix::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto send1 (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_TIMELY_EQ (10s, node0->balance (key2.pub), node0->config.receive_minimum.number ());
	auto node1 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, celerix::test::establish_tcp (system, *node1, node0->network.endpoint ()));
	ASSERT_TIMELY_EQ (10s, node1->balance (key2.pub), node0->config.receive_minimum.number ());
	ASSERT_TRUE (node1->block_or_pruned_exists (send1->hash ()));
	// Wait block receive
	ASSERT_TIMELY_EQ (5s, node1->ledger.block_count (), 3);
	// Confirmation for all blocks
	ASSERT_TIMELY_EQ (5s, node1->ledger.cemented_count (), 3);
}

TEST (node, auto_bootstrap_reverse)
{
	celerix::test::system system;
	celerix::node_config config (system.get_available_port ());
	config.backlog_scan.enable = false;
	celerix::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node1 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, celerix::test::establish_tcp (system, *node0, node1->network.endpoint ()));
	ASSERT_TIMELY_EQ (10s, node1->balance (key2.pub), node0->config.receive_minimum.number ());
}

TEST (node, merge_peers)
{
	celerix::test::system system (1);
	std::array<celerix::endpoint, 8> endpoints;
	endpoints.fill (celerix::endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ()));
	endpoints[0] = celerix::endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	system.nodes[0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes[0]->network.size ());
}

TEST (node, search_receivable)
{
	celerix::test::system system (1);
	auto node (system.nodes[0]);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, search_receivable_same)
{
	celerix::test::system system (1);
	auto node (system.nodes[0]);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY_EQ (10s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_multiple)
{
	celerix::test::system system (1);
	auto node (system.nodes[0]);
	celerix::keypair key2;
	celerix::keypair key3;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key3.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key3.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !node->balance (key3.pub).is_zero ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (key3.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY_EQ (10s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_confirmed)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto node = system.add_node (node_config);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	auto send1 (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_TIMELY (5s, celerix::test::confirmed (*node, { send1 }));

	auto send2 (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_TIMELY (5s, celerix::test::confirmed (*node, { send2 }));

	{
		auto transaction (node->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, celerix::dev::genesis_key.pub);
	}

	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (5s, !node->vote_router.active (send1->hash ()));
	ASSERT_TIMELY (5s, !node->vote_router.active (send2->hash ()));
	ASSERT_TIMELY_EQ (5s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_pruned)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto node1 = system.add_node (node_config);
	celerix::node_flags node_flags;
	node_flags.enable_pruning = true;
	celerix::node_config config (system.get_available_port ());
	config.enable_voting = false; // Remove after allowing pruned voting
	auto node2 = system.add_node (config, node_flags);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto send1 (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	auto send2 (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);

	// Confirmation
	ASSERT_TIMELY (10s, node1->active.empty () && node2->active.empty ());
	ASSERT_TIMELY (5s, node1->ledger.confirmed.block_exists_or_pruned (node1->ledger.tx_begin_read (), send2->hash ()));
	ASSERT_TIMELY_EQ (5s, node2->ledger.cemented_count (), 3);
	system.wallet (0)->store.erase (node1->wallets.tx_begin_write (), celerix::dev::genesis_key.pub);

	// Pruning
	{
		auto transaction = node2->ledger.tx_begin_write ();
		ASSERT_EQ (1, node2->ledger.pruning_action (transaction, send1->hash (), 1));
	}
	ASSERT_EQ (1, node2->ledger.pruned_count ());
	ASSERT_TRUE (node2->block_or_pruned_exists (send1->hash ())); // true for pruned

	// Receive pruned block
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (1)->search_receivable (system.wallet (1)->wallets.tx_begin_read ()));
	ASSERT_TIMELY_EQ (10s, node2->balance (key2.pub), 2 * node2->config.receive_minimum.number ());
}

TEST (node, unlock_search)
{
	celerix::test::system system (1);
	auto node (system.nodes[0]);
	celerix::keypair key2;
	celerix::uint128_t balance (node->balance (celerix::dev::genesis_key.pub));
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.rekey (transaction, "");
	}
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node->balance (celerix::dev::genesis_key.pub) != balance);
	ASSERT_TIMELY (10s, node->active.empty ());
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		celerix::lock_guard<std::recursive_mutex> lock{ system.wallet (0)->store.mutex };
		system.wallet (0)->store.password.value_set (celerix::keypair ().prv);
	}
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		ASSERT_FALSE (system.wallet (0)->enter_password (transaction, ""));
	}
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, working)
{
	auto path (celerix::working_path ());
	ASSERT_FALSE (path.empty ());
}

TEST (node, confirm_locked)
{
	celerix::test::system system (1);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto transaction (system.wallet (0)->wallets.tx_begin_read ());
	system.wallet (0)->enter_password (transaction, "1");
	auto block = celerix::send_block_builder ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (celerix::keypair ().prv, 0)
				 .work (0)
				 .build ();
	system.nodes[0]->network.flood_block (block, celerix::transport::traffic_type::test);
}

TEST (node_config, random_rep)
{
	auto path (celerix::unique_path ());
	celerix::node_config config1 (100);
	auto rep (config1.random_representative ());
	ASSERT_NE (config1.preconfigured_representatives.end (), std::find (config1.preconfigured_representatives.begin (), config1.preconfigured_representatives.end (), rep));
}

TEST (node, expire)
{
	std::weak_ptr<celerix::node> node0;
	{
		celerix::test::system system (1);
		node0 = system.nodes[0];
		auto & node1 (*system.nodes[0]);
		system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	}
	ASSERT_TRUE (node0.expired ());
}

// This test is racy, there is no guarantee that the election won't be confirmed until all forks are fully processed
TEST (node, fork_publish)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key1;
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	celerix::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	node1.process_active (send1);
	node1.process_active (send2);
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	auto election (node1.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	// Wait until the genesis rep activated & makes vote
	ASSERT_TIMELY_EQ (1s, election->votes ().size (), 2);
	auto votes1 (election->votes ());
	auto existing1 (votes1.find (celerix::dev::genesis_key.pub));
	ASSERT_NE (votes1.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	auto winner (*election->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, winner.first);
}

// In test case there used to be a race condition, it was worked around in:.
// https://github.com/celerixcurrency/celerix-node/pull/4091
// The election and the processing of block send2 happen in parallel.
// Usually the election happens first and the send2 block is added to the election.
// However, if the send2 block is processed before the election is started then
// there is a race somewhere and the election might not notice the send2 block.
// The test case can be made to pass by ensuring the election is started before the send2 is processed.
// However, is this a problem with the test case or this is a problem with the node handling of forks?
TEST (node, fork_publish_inactive)
{
	celerix::test::system system (1);
	auto & node = *system.nodes[0];
	celerix::keypair key1;
	celerix::keypair key2;

	celerix::send_block_builder builder;

	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();

	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (send1->block_work ())
				 .build ();

	node.process_active (send1);
	ASSERT_TIMELY (5s, node.block (send1->hash ()));

	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send1->qualified_root ()));

	ASSERT_EQ (celerix::block_status::fork, node.process_local (send2).value ());

	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);

	auto find_block = [&election] (celerix::block_hash hash_a) -> bool {
		auto blocks = election->blocks ();
		return blocks.end () != blocks.find (hash_a);
	};
	ASSERT_TRUE (find_block (send1->hash ()));
	ASSERT_TRUE (find_block (send2->hash ()));

	ASSERT_EQ (election->winner ()->hash (), send1->hash ());
	ASSERT_NE (election->winner ()->hash (), send2->hash ());
}

TEST (node, fork_keep)
{
	celerix::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	celerix::keypair key1;
	celerix::keypair key2;
	celerix::send_block_builder builder;
	// send1 and send2 fork to different accounts
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);
	node2.process_active (builder.make_block ().from (*send1).build ());
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.active.size ());
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	// Fill node with forked blocks
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	node2.process_active (builder.make_block ().from (*send2).build ());
	ASSERT_TIMELY (5s, node2.active.active (*send2));
	auto election1 (node2.active.election (celerix::qualified_root (celerix::dev::genesis->hash (), celerix::dev::genesis->hash ())));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_TRUE (node1.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node2.block_or_pruned_exists (send1->hash ()));
	// Wait until the genesis rep makes a vote
	ASSERT_TIMELY (1.5min, election1->votes ().size () != 1);
	auto transaction0 (node1.ledger.tx_begin_read ());
	auto transaction1 (node2.ledger.tx_begin_read ());
	// The vote should be in agreement with what we already have.
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.ledger.any.block_exists (transaction0, send1->hash ()));
	ASSERT_TRUE (node2.ledger.any.block_exists (transaction1, send1->hash ()));
}

// This test is racy, there is no guarantee that the election won't be confirmed until all forks are fully processed
TEST (node, fork_flip)
{
	celerix::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	celerix::keypair key1;
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	celerix::publish publish1{ celerix::dev::network_params.network, send1 };
	celerix::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	celerix::publish publish2{ celerix::dev::network_params.network, send2 };
	node1.inbound (publish1, celerix::test::fake_channel (node1));
	node2.inbound (publish2, celerix::test::fake_channel (node2));
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.active.size ());
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	// Fill nodes with forked blocks
	node1.inbound (publish2, celerix::test::fake_channel (node1));
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	node2.inbound (publish1, celerix::test::fake_channel (node2));
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	auto election1 (node2.active.election (celerix::qualified_root (celerix::dev::genesis->hash (), celerix::dev::genesis->hash ())));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_NE (nullptr, node1.block (publish1.block->hash ()));
	ASSERT_NE (nullptr, node2.block (publish2.block->hash ()));
	ASSERT_TIMELY (10s, node2.block_or_pruned_exists (publish1.block->hash ()));
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_TRUE (node2.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_FALSE (node2.block_or_pruned_exists (publish2.block->hash ()));
}

// Test that more than one block can be rolled back
TEST (node, fork_multi_flip)
{
	auto type = celerix::transport::transport_type::tcp;
	celerix::test::system system;
	celerix::node_flags node_flags;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto & node1 (*system.add_node (node_config, node_flags, type));
	node_config.peering_port = system.get_available_port ();
	node_config.bootstrap.account_sets.cooldown = 100ms; // Reduce cooldown to speed up fork resolution
	auto & node2 (*system.add_node (node_config, node_flags, type));
	ASSERT_EQ (1, node1.network.size ());
	celerix::keypair key1;
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	celerix::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .previous (send2->hash ())
				 .destination (key2.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	// Node2 has two blocks that will be rolled back by node1's vote
	ASSERT_EQ (celerix::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send2));
	ASSERT_EQ (celerix::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send3));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv); // Insert voting key in to node1

	auto election = celerix::test::start_election (system, node2, send2->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_TIMELY (10s, election->contains (send1->hash ()));
	celerix::test::confirm (node1.ledger, send1);
	ASSERT_TIMELY (10s, node2.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (celerix::test::block_or_pruned_none_exists (node2, { send2, send3 }));
	auto winner = *election->tally ().begin ();
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, winner.first);
}

// Blocks that are no longer actively being voted on should be able to be evicted through bootstrapping.
// This could happen if a fork wasn't resolved before the process previously shut down
TEST (node, fork_bootstrap_flip)
{
	celerix::test::system system;
	celerix::node_config config1{ system.get_available_port () };
	config1.backlog_scan.enable = false;
	celerix::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto & node1 = *system.add_node (config1, node_flags);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::node_config config2 (system.get_available_port ());
	config2.bootstrap.account_sets.cooldown = 100ms; // Reduce cooldown to speed up fork resolution
	auto & node2 = *system.make_disconnected_node (config2, node_flags);
	celerix::block_hash latest = node1.latest (celerix::dev::genesis_key.pub);
	celerix::keypair key1;
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (latest)
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();
	celerix::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (latest)
				 .destination (key2.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();
	// Insert but don't rebroadcast, simulating settled blocks
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	ASSERT_EQ (celerix::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send2));
	celerix::test::confirm (node1.ledger, send1);
	ASSERT_TIMELY (5s, node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), send1->hash ()));
	ASSERT_TIMELY (5s, node2.ledger.any.block_exists (node2.ledger.tx_begin_read (), send2->hash ()));

	// Additionally add new peer to confirm & replace bootstrap block
	node2.network.merge_peer (node1.network.endpoint ());

	ASSERT_TIMELY (10s, node2.ledger.any.block_exists (node2.ledger.tx_begin_read (), send1->hash ()));
}

TEST (node, fork_open)
{
	celerix::test::system system (1);
	auto & node = *system.nodes[0];
	std::shared_ptr<celerix::election> election;

	// create block send1, to send all the balance from genesis to key1
	// this is done to ensure that the open block(s) cannot be voted on and confirmed
	celerix::keypair key1;
	auto send1 = celerix::send_block_builder ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	celerix::publish publish1{ celerix::dev::network_params.network, send1 };
	auto channel1 = std::make_shared<celerix::transport::fake::channel> (node);
	node.inbound (publish1, channel1);
	ASSERT_TIMELY (5s, (election = node.active.election (publish1.block->qualified_root ())) != nullptr);
	election->force_confirm ();
	ASSERT_TIMELY (5s, node.active.empty () && node.block_confirmed (publish1.block->hash ()));

	// register key for genesis account, not sure why we do this, it seems needless,
	// since the genesis account at this stage has zero voting weight
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	// create the 1st open block to receive send1, which should be regarded as the winner just because it is first
	celerix::open_block_builder builder;
	auto open1 = builder.make_block ()
				 .source (publish1.block->hash ())
				 .representative (1)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	celerix::publish publish2{ celerix::dev::network_params.network, open1 };
	node.inbound (publish2, channel1);
	ASSERT_TIMELY_EQ (5s, 1, node.active.size ());

	// create 2nd open block, which is a fork of open1 block
	auto open2 = builder.make_block ()
				 .source (publish1.block->hash ())
				 .representative (2)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	celerix::publish publish3{ celerix::dev::network_params.network, open2 };
	node.inbound (publish3, channel1);
	ASSERT_TIMELY (5s, (election = node.active.election (publish3.block->qualified_root ())) != nullptr);

	// we expect to find 2 blocks in the election and we expect the first block to be the winner just because it was first
	ASSERT_TIMELY_EQ (5s, 2, election->blocks ().size ());
	ASSERT_EQ (publish2.block->hash (), election->winner ()->hash ());

	// wait for a second and check that the election did not get confirmed
	system.delay_ms (1000ms);
	ASSERT_FALSE (election->confirmed ());

	// check that only the first block is saved to the ledger
	ASSERT_TIMELY (5s, node.block (publish2.block->hash ()));
	ASSERT_FALSE (node.block (publish3.block->hash ()));
}

TEST (node, fork_open_flip)
{
	celerix::test::system system (1);
	auto & node1 = *system.nodes[0];

	std::shared_ptr<celerix::election> election;
	celerix::keypair key1;
	celerix::keypair rep1;
	celerix::keypair rep2;

	// send 1 raw from genesis to key1 on both node1 and node2
	auto send1 = celerix::send_block_builder ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);

	// We should be keeping this block
	celerix::open_block_builder builder;
	auto open1 = builder.make_block ()
				 .source (send1->hash ())
				 .representative (rep1.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();

	// create a fork of block open1, this block will lose the election
	auto open2 = builder.make_block ()
				 .source (send1->hash ())
				 .representative (rep2.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	ASSERT_FALSE (*open1 == *open2);

	// give block open1 to node1, manually trigger an election for open1 and ensure it is in the ledger
	node1.process_active (open1);
	ASSERT_TIMELY (5s, node1.block (open1->hash ()) != nullptr);
	node1.scheduler.manual.push (open1);
	ASSERT_TIMELY (5s, (election = node1.active.election (open1->qualified_root ())) != nullptr);
	election->transition_active ();

	// create node2, with blocks send1 and open2 pre-initialised in the ledger,
	// so that block open1 cannot possibly get in the ledger before open2 via background sync
	system.initialization_blocks.push_back (send1);
	system.initialization_blocks.push_back (open2);
	auto & node2 = *system.add_node ();
	system.initialization_blocks.clear ();

	// ensure open2 is in node2 ledger (and therefore has sideband) and manually trigger an election for open2
	ASSERT_TIMELY (5s, node2.block (open2->hash ()) != nullptr);
	node2.scheduler.manual.push (open2);
	ASSERT_TIMELY (5s, (election = node2.active.election (open2->qualified_root ())) != nullptr);
	election->transition_active ();

	ASSERT_TIMELY_EQ (5s, 2, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 2, node2.active.size ());

	// allow node1 to vote and wait for open1 to be confirmed on node1
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_TIMELY (5s, node1.block_confirmed (open1->hash ()));

	// Notify both nodes of both blocks, both nodes will become aware that a fork exists
	node1.process_active (open2);
	node2.process_active (open1);

	ASSERT_TIMELY_EQ (5s, 2, election->votes ().size ()); // one more than expected due to elections having dummy votes

	// Node2 should eventually settle on open1
	ASSERT_TIMELY (10s, node2.block (open1->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (open1->hash ()));
	auto winner = *election->tally ().begin ();
	ASSERT_EQ (*open1, *winner.second);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 1, winner.first);

	// check the correct blocks are in the ledgers
	auto transaction1 = node1.ledger.tx_begin_read ();
	auto transaction2 = node2.ledger.tx_begin_read ();
	ASSERT_TRUE (node1.ledger.any.block_exists (transaction1, open1->hash ()));
	ASSERT_TRUE (node2.ledger.any.block_exists (transaction2, open1->hash ()));
	ASSERT_FALSE (node2.ledger.any.block_exists (transaction2, open2->hash ()));
}

TEST (node, coherent_observer)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.observers.blocks.add ([&node1] (celerix::election_status const & status_a, std::vector<celerix::vote_with_weight_info> const &, celerix::account const &, celerix::uint128_t const &, bool, bool) {
		ASSERT_TRUE (node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), status_a.winner->hash ()));
	});
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key;
	system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key.pub, 1);
}

TEST (node, fork_no_vote_quorum)
{
	celerix::test::system system (3);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto & node3 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto key4 (system.wallet (0)->deterministic_insert ());
	system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key4, celerix::dev::constants.genesis_amount / 4);
	auto key1 (system.wallet (1)->deterministic_insert ());
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1);
	}
	auto block (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key1, node1.config.receive_minimum.number ()));
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (30s, node3.balance (key1) == node1.config.receive_minimum.number () && node2.balance (key1) == node1.config.receive_minimum.number () && node1.balance (key1) == node1.config.receive_minimum.number ());
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node3.weight (key1));
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (block->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance ((celerix::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2))
				 .link (key1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (block->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node1.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node2.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node3.process (send1));
	auto key2 (system.wallet (2)->deterministic_insert ());
	auto send2 = celerix::send_block_builder ()
				 .previous (block->hash ())
				 .destination (key2)
				 .balance ((celerix::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (block->hash ()))
				 .build ();
	celerix::raw_key key3;
	auto transaction (system.wallet (1)->wallets.tx_begin_read ());
	ASSERT_FALSE (system.wallet (1)->store.fetch (transaction, key1, key3));
	auto vote = std::make_shared<celerix::vote> (key1, key3, 0, 0, std::vector<celerix::block_hash>{ send2->hash () });
	celerix::confirm_ack confirm{ celerix::dev::network_params.network, vote };
	auto channel = node2.network.find_node_id (node3.node_id.pub);
	ASSERT_NE (nullptr, channel);
	channel->send (confirm, celerix::transport::traffic_type::test);
	ASSERT_TIMELY (10s, node3.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::in) >= 3);
	ASSERT_EQ (node1.latest (celerix::dev::genesis_key.pub), send1->hash ());
	ASSERT_EQ (node2.latest (celerix::dev::genesis_key.pub), send1->hash ());
	ASSERT_EQ (node3.latest (celerix::dev::genesis_key.pub), send1->hash ());
}

// Disabled because it sometimes takes way too long (but still eventually finishes)
TEST (node, DISABLED_fork_pre_confirm)
{
	celerix::test::system system (3);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto & node2 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key1;
	system.wallet (1)->insert_adhoc (key1.prv);
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1.pub);
	}
	celerix::keypair key2;
	system.wallet (2)->insert_adhoc (key2.prv);
	{
		auto transaction (system.wallet (2)->wallets.tx_begin_write ());
		system.wallet (2)->store.representative_set (transaction, key2.pub);
	}
	auto block0 (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key1.pub, celerix::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block0);
	ASSERT_TIMELY (30s, node0.balance (key1.pub) != 0);
	auto block1 (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, celerix::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block1);
	ASSERT_TIMELY (30s, node0.balance (key2.pub) != 0);
	celerix::keypair key3;
	celerix::keypair key4;
	celerix::state_block_builder builder;
	auto block2 = builder.make_block ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (node0.latest (celerix::dev::genesis_key.pub))
				  .representative (key3.pub)
				  .balance (node0.balance (celerix::dev::genesis_key.pub))
				  .link (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	auto block3 = builder.make_block ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (node0.latest (celerix::dev::genesis_key.pub))
				  .representative (key4.pub)
				  .balance (node0.balance (celerix::dev::genesis_key.pub))
				  .link (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	node0.work_generate_blocking (*block2);
	node0.work_generate_blocking (*block3);
	node0.process_active (block2);
	node1.process_active (block2);
	node2.process_active (block3);
	auto done (false);
	// Extend deadline; we must finish within a total of 100 seconds
	system.deadline_set (70s);
	while (!done)
	{
		done |= node0.latest (celerix::dev::genesis_key.pub) == block2->hash () && node1.latest (celerix::dev::genesis_key.pub) == block2->hash () && node2.latest (celerix::dev::genesis_key.pub) == block2->hash ();
		done |= node0.latest (celerix::dev::genesis_key.pub) == block3->hash () && node1.latest (celerix::dev::genesis_key.pub) == block3->hash () && node2.latest (celerix::dev::genesis_key.pub) == block3->hash ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Sometimes hangs on the bootstrap_initiator.bootstrap call
TEST (node, DISABLED_fork_stale)
{
	celerix::test::system system1 (1);
	system1.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::test::system system2 (1);
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);

	auto channel = celerix::test::establish_tcp (system1, node2, node1.network.endpoint ());
	auto vote = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, 0, 0, std::vector<celerix::block_hash> ());
	ASSERT_TRUE (node2.rep_crawler.process (vote, channel));
	celerix::keypair key1;
	celerix::keypair key2;
	celerix::state_block_builder builder;
	auto send3 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::celerix_ratio)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send3);
	node1.process_active (send3);
	system2.deadline_set (10s);
	while (node2.block (send3->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::celerix_ratio)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::celerix_ratio)
				 .link (key2.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	{
		auto transaction1 = node1.ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction1, send1));
		auto transaction2 = node2.ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node2.ledger.process (transaction2, send2));
	}
	node1.process_active (send1);
	node1.process_active (send2);
	node2.process_active (send1);
	node2.process_active (send2);
	while (node2.block (send1->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/celerixcurrency/celerix-node/pull/3512
// Issue for investigating it: https://github.com/celerixcurrency/celerix-node/issues/3516
TEST (node, DISABLED_broadcast_elected)
{
	auto type = celerix::transport::transport_type::tcp;
	celerix::node_flags node_flags;
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto node0 = system.add_node (node_config, node_flags, type);
	node_config.peering_port = system.get_available_port ();
	auto node1 = system.add_node (node_config, node_flags, type);
	node_config.peering_port = system.get_available_port ();
	auto node2 = system.add_node (node_config, node_flags, type);
	celerix::keypair rep_big;
	celerix::keypair rep_small;
	celerix::keypair rep_other;
	celerix::block_builder builder;
	{
		auto transaction0 = node0->ledger.tx_begin_write ();
		auto transaction1 = node1->ledger.tx_begin_write ();
		auto transaction2 = node2->ledger.tx_begin_write ();
		auto fund_big = builder.send ()
						.previous (celerix::dev::genesis->hash ())
						.destination (rep_big.pub)
						.balance (celerix::Kcelerix_ratio * 5)
						.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						.work (*system.work.generate (celerix::dev::genesis->hash ()))
						.build ();
		auto open_big = builder.open ()
						.source (fund_big->hash ())
						.representative (rep_big.pub)
						.account (rep_big.pub)
						.sign (rep_big.prv, rep_big.pub)
						.work (*system.work.generate (rep_big.pub))
						.build ();
		auto fund_small = builder.send ()
						  .previous (fund_big->hash ())
						  .destination (rep_small.pub)
						  .balance (celerix::Kcelerix_ratio * 2)
						  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						  .work (*system.work.generate (fund_big->hash ()))
						  .build ();
		auto open_small = builder.open ()
						  .source (fund_small->hash ())
						  .representative (rep_small.pub)
						  .account (rep_small.pub)
						  .sign (rep_small.prv, rep_small.pub)
						  .work (*system.work.generate (rep_small.pub))
						  .build ();
		auto fund_other = builder.send ()
						  .previous (fund_small->hash ())
						  .destination (rep_other.pub)
						  .balance (celerix::Kcelerix_ratio)
						  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						  .work (*system.work.generate (fund_small->hash ()))
						  .build ();
		auto open_other = builder.open ()
						  .source (fund_other->hash ())
						  .representative (rep_other.pub)
						  .account (rep_other.pub)
						  .sign (rep_other.prv, rep_other.pub)
						  .work (*system.work.generate (rep_other.pub))
						  .build ();
		ASSERT_EQ (celerix::block_status::progress, node0->ledger.process (transaction0, fund_big));
		ASSERT_EQ (celerix::block_status::progress, node1->ledger.process (transaction1, fund_big));
		ASSERT_EQ (celerix::block_status::progress, node2->ledger.process (transaction2, fund_big));
		ASSERT_EQ (celerix::block_status::progress, node0->ledger.process (transaction0, open_big));
		ASSERT_EQ (celerix::block_status::progress, node1->ledger.process (transaction1, open_big));
		ASSERT_EQ (celerix::block_status::progress, node2->ledger.process (transaction2, open_big));
		ASSERT_EQ (celerix::block_status::progress, node0->ledger.process (transaction0, fund_small));
		ASSERT_EQ (celerix::block_status::progress, node1->ledger.process (transaction1, fund_small));
		ASSERT_EQ (celerix::block_status::progress, node2->ledger.process (transaction2, fund_small));
		ASSERT_EQ (celerix::block_status::progress, node0->ledger.process (transaction0, open_small));
		ASSERT_EQ (celerix::block_status::progress, node1->ledger.process (transaction1, open_small));
		ASSERT_EQ (celerix::block_status::progress, node2->ledger.process (transaction2, open_small));
		ASSERT_EQ (celerix::block_status::progress, node0->ledger.process (transaction0, fund_other));
		ASSERT_EQ (celerix::block_status::progress, node1->ledger.process (transaction1, fund_other));
		ASSERT_EQ (celerix::block_status::progress, node2->ledger.process (transaction2, fund_other));
		ASSERT_EQ (celerix::block_status::progress, node0->ledger.process (transaction0, open_other));
		ASSERT_EQ (celerix::block_status::progress, node1->ledger.process (transaction1, open_other));
		ASSERT_EQ (celerix::block_status::progress, node2->ledger.process (transaction2, open_other));
	}
	// Confirm blocks to allow voting
	for (auto & node : system.nodes)
	{
		auto block (node->block (node->latest (celerix::dev::genesis_key.pub)));
		ASSERT_NE (nullptr, block);
		node->start_election (block);
		auto election (node->active.election (block->qualified_root ()));
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY_EQ (5s, 4, node->ledger.cemented_count ())
	}

	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (1)->insert_adhoc (rep_small.prv);
	system.wallet (2)->insert_adhoc (rep_other.prv);
	auto fork0 = builder.send ()
				 .previous (node2->latest (celerix::dev::genesis_key.pub))
				 .destination (rep_small.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (node2->latest (celerix::dev::genesis_key.pub)))
				 .build ();
	// A copy is necessary to avoid data races during ledger processing, which sets the sideband
	auto fork0_copy (std::make_shared<celerix::send_block> (*fork0));
	node0->process_active (fork0);
	node1->process_active (fork0_copy);
	auto fork1 = builder.send ()
				 .previous (node2->latest (celerix::dev::genesis_key.pub))
				 .destination (rep_big.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (node2->latest (celerix::dev::genesis_key.pub)))
				 .build ();
	system.wallet (2)->insert_adhoc (rep_small.prv);
	node2->process_active (fork1);
	ASSERT_TIMELY (10s, node0->block_or_pruned_exists (fork0->hash ()) && node1->block_or_pruned_exists (fork0->hash ()));
	system.deadline_set (50s);
	while (!node2->block_or_pruned_exists (fork0->hash ()))
	{
		auto ec = system.poll ();
		ASSERT_TRUE (node0->block_or_pruned_exists (fork0->hash ()));
		ASSERT_TRUE (node1->block_or_pruned_exists (fork0->hash ()));
		ASSERT_NO_ERROR (ec);
	}
	ASSERT_TIMELY (5s, node1->stats.count (celerix::stat::type::confirmation_observer, celerix::stat::detail::inactive_conf_height, celerix::stat::dir::out) != 0);
}

TEST (node, rep_self_vote)
{
	celerix::test::system system;

	celerix::node_flags node_flags;
	node_flags.disable_request_loop = true; // Prevent automatic election cleanup
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = std::numeric_limits<celerix::uint128_t>::max ();
	// Disable automatic election activation
	node_config.backlog_scan.enable = false;
	node_config.priority_scheduler.enable = false;
	node_config.hinted_scheduler.enable = false;
	node_config.optimistic_scheduler.enable = false;
	auto node0 = system.add_node (node_config, node_flags);

	celerix::keypair rep_big;
	celerix::block_builder builder;
	auto fund_big = builder.send ()
					.previous (celerix::dev::genesis->hash ())
					.destination (rep_big.pub)
					.balance (celerix::uint128_t{ "0xb0000000000000000000000000000000" })
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*system.work.generate (celerix::dev::genesis->hash ()))
					.build ();
	auto open_big = builder.open ()
					.source (fund_big->hash ())
					.representative (rep_big.pub)
					.account (rep_big.pub)
					.sign (rep_big.prv, rep_big.pub)
					.work (*system.work.generate (rep_big.pub))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, node0->process (fund_big));
	ASSERT_EQ (celerix::block_status::progress, node0->process (open_big));

	// Confirm both blocks, allowing voting on the upcoming block
	node0->start_election (node0->block (open_big->hash ()));

	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node0->active.election (open_big->qualified_root ()));
	election->force_confirm ();

	// Insert representatives into the node to allow voting
	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_EQ (system.wallet (0)->wallets.reps ().voting, 2);

	auto block0 = builder.send ()
				  .previous (fund_big->hash ())
				  .destination (rep_big.pub)
				  .balance (celerix::uint128_t ("0x60000000000000000000000000000000"))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (fund_big->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, node0->process (block0));

	auto election1 = celerix::test::start_election (system, *node0, block0->hash ());
	ASSERT_NE (nullptr, election1);

	// Wait until representatives are activated & make vote
	ASSERT_TIMELY_EQ (1s, election1->votes ().size (), 3);

	// Election should receive votes from representatives hosted on the same node
	auto rep_votes (election1->votes ());
	ASSERT_NE (rep_votes.end (), rep_votes.find (celerix::dev::genesis_key.pub));
	ASSERT_NE (rep_votes.end (), rep_votes.find (rep_big.pub));
}

// Bootstrapping shouldn't republish the blocks to the network.
TEST (node, DISABLED_bootstrap_no_publish)
{
	celerix::test::system system0 (1);
	celerix::test::system system1 (1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	celerix::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	celerix::block_builder builder;
	auto send0 = builder
				 .send ()
				 .previous (node0->latest (celerix::dev::genesis_key.pub))
				 .destination (key0.pub)
				 .balance (500)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	{
		auto transaction = node0->ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node0->ledger.process (transaction, send0));
	}
	ASSERT_TRUE (node1->active.empty ());
	system1.deadline_set (10s);
	while (node1->block (send0->hash ()) == nullptr)
	{
		// Poll until the TCP connection is torn down and in_progress goes false
		system0.poll ();
		auto ec = system1.poll ();
		// There should never be an active transaction because the only activity is bootstrapping 1 block which shouldn't be publishing.
		ASSERT_TRUE (node1->active.empty ());
		ASSERT_NO_ERROR (ec);
	}
}

// Bootstrapping a forked open block should succeed.
TEST (node, bootstrap_fork_open)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	auto node0 = system.add_node (node_config);
	node_config.peering_port = system.get_available_port ();
	auto node1 = system.add_node (node_config);
	celerix::keypair key0;
	celerix::block_builder builder;
	auto send0 = builder.send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (celerix::dev::constants.genesis_amount - 500)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto open0 = builder.open ()
				 .source (send0->hash ())
				 .representative (1)
				 .account (key0.pub)
				 .sign (key0.prv, key0.pub)
				 .work (*system.work.generate (key0.pub))
				 .build ();
	auto open1 = builder.open ()
				 .source (send0->hash ())
				 .representative (2)
				 .account (key0.pub)
				 .sign (key0.prv, key0.pub)
				 .work (*system.work.generate (key0.pub))
				 .build ();
	// Both know about send0
	ASSERT_EQ (celerix::block_status::progress, node0->process (send0));
	ASSERT_EQ (celerix::block_status::progress, node1->process (send0));
	// Confirm send0 to allow starting and voting on the following blocks
	for (auto node : system.nodes)
	{
		node->start_election (node->block (node->latest (celerix::dev::genesis_key.pub)));
		ASSERT_TIMELY (1s, node->active.election (send0->qualified_root ()));
		auto election = node->active.election (send0->qualified_root ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY (2s, node->active.empty ());
	}
	ASSERT_TIMELY (3s, node0->block_confirmed (send0->hash ()));
	// They disagree about open0/open1
	ASSERT_EQ (celerix::block_status::progress, node0->process (open0));
	ASSERT_EQ (celerix::block_status::progress, node1->process (open1));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_FALSE (node1->block_or_pruned_exists (open0->hash ()));
	ASSERT_TIMELY (1s, node1->active.empty ());
	ASSERT_TIMELY (10s, !node1->block_or_pruned_exists (open1->hash ()) && node1->block_or_pruned_exists (open0->hash ()));
}

// Unconfirmed blocks from bootstrap should be confirmed
TEST (node, bootstrap_confirm_frontiers)
{
	celerix::test::system system;
	auto node0 = system.add_node ();
	auto node1 = system.add_node ();
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key0;

	// create block to send 500 raw from genesis to key0 and save into node0 ledger without immediately triggering an election
	auto send0 = celerix::send_block_builder ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (celerix::dev::constants.genesis_amount - 500)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node0->process (send0));
	ASSERT_TIMELY (10s, node1->block_confirmed (send0->hash ()));
}

// Test that if we create a block that isn't confirmed, the bootstrapping processes sync the missing block.
TEST (node, unconfirmed_send)
{
	celerix::test::system system{};

	auto & node1 = *system.add_node ();
	auto wallet1 = system.wallet (0);
	wallet1->insert_adhoc (celerix::dev::genesis_key.prv);

	celerix::keypair key2{};
	auto & node2 = *system.add_node ();
	auto wallet2 = system.wallet (1);
	wallet2->insert_adhoc (key2.prv);

	// firstly, send two units from node1 to node2 and expect that both nodes see the block as confirmed
	// (node1 will start an election for it, vote on it and node2 gets synced up)
	auto send1 = wallet1->send_action (celerix::dev::genesis_key.pub, key2.pub, 2 * celerix::celerix_ratio);
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send1->hash ()));

	// wait until receive1 (auto-receive created by wallet) is cemented
	ASSERT_TIMELY_EQ (5s, node2.ledger.confirmed.account_height (node2.ledger.tx_begin_read (), key2.pub), 1);
	ASSERT_EQ (node2.balance (key2.pub), 2 * celerix::celerix_ratio);
	auto recv1 = node2.ledger.find_receive_block_by_send_hash (node2.ledger.tx_begin_read (), key2.pub, send1->hash ());

	// create send2 to send from node2 to node1 and save it to node2's ledger without triggering an election (node1 does not hear about it)
	auto send2 = celerix::state_block_builder{}
				 .make_block ()
				 .account (key2.pub)
				 .previous (recv1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::celerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (recv1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node2.process (send2));

	auto send3 = wallet2->send_action (key2.pub, celerix::dev::genesis_key.pub, celerix::celerix_ratio);
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send3->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send3->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.ledger.cemented_count (), 7);
	ASSERT_TIMELY_EQ (5s, node1.balance (celerix::dev::genesis_key.pub), celerix::dev::constants.genesis_amount);
}

// Test that nodes can disable representative voting
TEST (node, no_voting)
{
	celerix::test::system system (1);
	auto & node0 (*system.nodes[0]);
	celerix::node_config node_config (system.get_available_port ());
	node_config.enable_voting = false;
	system.add_node (node_config);

	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node1 has a rep
	wallet1->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	// Broadcast a confirm so others should know this is a rep node
	wallet1->send_action (celerix::dev::genesis_key.pub, key1.pub, celerix::celerix_ratio);
	ASSERT_TIMELY (10s, node0.active.empty ());
	ASSERT_EQ (0, node0.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::in));
}

TEST (node, send_callback)
{
	celerix::test::system system (1);
	auto & node0 (*system.nodes[0]);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	node0.config.callback_address = "localhost";
	node0.config.callback_port = 8010;
	node0.config.callback_target = "/";
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, node0.config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node0.balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max () - node0.config.receive_minimum.number (), node0.balance (celerix::dev::genesis_key.pub));
}

TEST (node, balance_observer)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	std::atomic<int> balances (0);
	celerix::keypair key;
	node1.observers.account_balance.add ([&key, &balances] (celerix::account const & account_a, bool is_pending) {
		if (key.pub == account_a && is_pending)
		{
			balances++;
		}
		else if (celerix::dev::genesis_key.pub == account_a && !is_pending)
		{
			balances++;
		}
	});
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key.pub, 1);
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto ec = system.poll ();
		done = balances.load () == 2;
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, block_confirm)
{
	auto type = celerix::transport::transport_type::tcp;
	celerix::node_flags node_flags;
	celerix::test::system system (2, type, node_flags);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	celerix::keypair key;
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	// A copy is necessary to avoid data races during ledger processing, which sets the sideband
	auto send1_copy = builder.make_block ()
					  .from (*send1)
					  .build ();
	auto hash1 = send1->hash ();
	auto hash2 = send1_copy->hash ();
	node1.block_processor.add (send1);
	node2.block_processor.add (send1_copy);
	ASSERT_TIMELY (5s, node1.block_or_pruned_exists (send1->hash ()) && node2.block_or_pruned_exists (send1_copy->hash ()));
	ASSERT_TRUE (node1.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node2.block_or_pruned_exists (send1_copy->hash ()));
	// Confirm send1 on node2 so it can vote for send2
	node2.start_election (send1_copy);
	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node2.active.election (send1_copy->qualified_root ()));
	// Make node2 genesis representative so it can vote
	system.wallet (1)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (10s, node1.active.recently_cemented.list ().size (), 1);
}

TEST (node, confirm_quorum)
{
	celerix::test::system system (1);
	auto & node1 = *system.nodes[0];
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	// Put greater than node.delta () in pending so quorum can't be reached
	celerix::amount new_balance = node1.online_reps.delta () - celerix::Kcelerix_ratio;
	auto send1 = celerix::state_block_builder ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (new_balance)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node1.process (send1));
	system.wallet (0)->send_action (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.pub, new_balance.number ());
	ASSERT_TIMELY (2s, node1.active.election (send1->qualified_root ()));
	auto election = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());
	ASSERT_EQ (0, node1.balance (celerix::dev::genesis_key.pub));
}

// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	node_config.receive_minimum = celerix::dev::constants.genesis_amount;
	auto & node (*system.add_node (node_config));
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 3 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send2->hash ()))
				 .build ();
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node.ledger.process (transaction, send1));
		ASSERT_EQ (celerix::block_status::progress, node.ledger.process (transaction, send2));
	}
	// Confirm blocks to allow voting
	node.start_election (send2);
	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()));
	election->force_confirm ();
	ASSERT_TIMELY_EQ (3s, node.ledger.cemented_count (), 3);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::confirm_req message1{ celerix::dev::network_params.network, send1->hash (), send1->root () };
	celerix::confirm_req message2{ celerix::dev::network_params.network, send2->hash (), send2->root () };
	auto channel = std::make_shared<celerix::transport::fake::channel> (node);
	node.inbound (message1, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes), 1);
	node.inbound (message2, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes), 2);
	for (auto i (0); i < 100; ++i)
	{
		node.inbound (message1, channel);
		node.inbound (message2, channel);
	}
	// Make sure a new vote was not generated
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes), 2);
	// Max cache
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node.ledger.process (transaction, send3));
	}
	celerix::confirm_req message3{ celerix::dev::network_params.network, send3->hash (), send3->root () };
	node.inbound (message3, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes), 3);
	ASSERT_TIMELY (3s, !node.history.votes (send1->root (), send1->hash ()).empty ());
	ASSERT_TIMELY (3s, !node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_TIMELY (3s, !node.history.votes (send3->root (), send3->hash ()).empty ());
	// All requests should be served from the cache
	for (auto i (0); i < 100; ++i)
	{
		node.inbound (message3, channel);
	}
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes), 3);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/celerixcurrency/celerix-node/pull/3532
// Issue for investigating it: https://github.com/celerixcurrency/celerix-node/issues/3481
// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_batch)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	ASSERT_GE (node.network_params.voting.max_cache, 2);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key1;
	auto send1 = celerix::state_block_builder ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	node.confirming_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node.ledger.confirmed.block_exists_or_pruned (node.ledger.tx_begin_read (), send1->hash ()));
	auto send2 = celerix::state_block_builder ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	auto receive1 = celerix::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	std::vector<std::pair<celerix::block_hash, celerix::root>> batch{ { send2->hash (), send2->root () }, { receive1->hash (), receive1->root () } };
	celerix::confirm_req message{ celerix::dev::network_params.network, batch };
	auto channel = std::make_shared<celerix::transport::fake::channel> (node);
	// Generates and sends one vote for both hashes which is then cached
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out), 1);
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	ASSERT_FALSE (node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (receive1->root (), receive1->hash ()).empty ());
	// Only one confirm_ack should be sent if all hashes are part of the same vote
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out), 2);
	ASSERT_EQ (2, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	// Test when votes are different
	node.history.erase (send2->root ());
	node.history.erase (receive1->root ());
	node.inbound (celerix::confirm_req{ celerix::dev::network_params.network, send2->hash (), send2->root () }, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out), 3);
	ASSERT_EQ (3, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	node.inbound (celerix::confirm_req{ celerix::dev::network_params.network, receive1->hash (), receive1->root () }, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out), 4);
	ASSERT_EQ (4, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	// There are two different votes, so both should be sent in response
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out), 6);
	ASSERT_EQ (6, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
}

/**
 * There is a cache for locally generated votes. This test checks that the node
 * properly caches and uses those votes when replying to confirm_req requests.
 */
// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_generate_new_vote)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	// Send a confirm req for genesis block to node
	celerix::confirm_req message1{ celerix::dev::network_params.network, celerix::dev::genesis->hash (), celerix::dev::genesis->root () };
	auto channel = std::make_shared<celerix::transport::fake::channel> (node);
	node.inbound (message1, channel);

	// check that the node generated a vote for the genesis block and that it is stored in the local vote cache and it is the only vote
	ASSERT_TIMELY (5s, !node.history.votes (celerix::dev::genesis->root (), celerix::dev::genesis->hash ()).empty ());
	auto votes1 = node.history.votes (celerix::dev::genesis->root (), celerix::dev::genesis->hash ());
	ASSERT_EQ (1, votes1.size ());
	ASSERT_EQ (1, votes1[0]->hashes.size ());
	ASSERT_EQ (celerix::dev::genesis->hash (), votes1[0]->hashes[0]);
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes), 1);

	auto send1 = celerix::state_block_builder ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.process (send1));
	// One of the hashes is cached
	std::vector<std::pair<celerix::block_hash, celerix::root>> roots_hashes{ std::make_pair (celerix::dev::genesis->hash (), celerix::dev::genesis->root ()), std::make_pair (send1->hash (), send1->root ()) };
	celerix::confirm_req message2{ celerix::dev::network_params.network, roots_hashes };
	node.inbound (message2, channel);
	ASSERT_TIMELY (3s, !node.history.votes (send1->root (), send1->hash ()).empty ());
	auto votes2 (node.history.votes (send1->root (), send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->hashes.size ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes), 2);
	ASSERT_FALSE (node.history.votes (celerix::dev::genesis->root (), celerix::dev::genesis->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (send1->root (), send1->hash ()).empty ());
	// First generated + again cached + new generated
	ASSERT_TIMELY_EQ (3s, 3, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
}

// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_fork)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_bootstrap_bulk_pull_server = true;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto & node1 (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto send1 = celerix::state_block_builder ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	auto send1_fork = celerix::state_block_builder ()
					  .account (celerix::dev::genesis_key.pub)
					  .previous (celerix::dev::genesis->hash ())
					  .representative (celerix::dev::genesis_key.pub)
					  .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
					  .link (celerix::dev::genesis_key.pub)
					  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					  .work (*node1.work_generate_blocking (celerix::dev::genesis->hash ()))
					  .build ();
	ASSERT_EQ (celerix::block_status::progress, node1.process (send1));
	// Cache vote
	auto vote = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, 0, 0);
	node1.vote_processor.vote (vote, std::make_shared<celerix::transport::fake::channel> (node1));
	node1.history.add (send1->root (), send1->hash (), vote);
	auto votes2 (node1.history.votes (send1->root (), send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->hashes.size ());
	// Start election for forked block
	node_config.peering_port = system.get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags));
	node2.process_active (send1_fork);
	ASSERT_TIMELY (5s, node2.block_or_pruned_exists (send1->hash ()));
}

TEST (node, vote_republish)
{
	celerix::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	celerix::keypair key2;
	// by not setting a private key on node1's wallet for genesis account, it is stopped from voting
	system.wallet (1)->insert_adhoc (key2.prv);

	// send1 and send2 are forks of each other
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();

	// process send1 first, this will make sure send1 goes into the ledger and an election is started
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.block (send1->hash ()));
	ASSERT_TIMELY (5s, node1.active.active (*send1));
	ASSERT_TIMELY (5s, node2.active.active (*send1));

	// now process send2, send2 will not go in the ledger because only the first block of a fork goes in the ledger
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));

	// send2 cannot be synced because it is not in the ledger of node1, it is only in the election object in RAM on node1
	ASSERT_FALSE (node1.block (send2->hash ()));

	// the vote causes the election to reach quorum and for the vote (and block?) to be published from node1 to node2
	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { send2 });
	node1.vote_processor.vote (vote, std::make_shared<celerix::transport::fake::channel> (node1));

	// FIXME: there is a race condition here, if the vote arrives before the block then the vote is wasted and the test fails
	// we could resend the vote but then there is a race condition between the vote resending and the election reaching quorum on node1
	// the proper fix would be to observe on node2 that both the block and the vote arrived in whatever order
	// the real node will do a confirm request if it needs to find a lost vote

	// check that send2 won on both nodes
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));

	// check that send1 is deleted from the ledger on nodes
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.balance (key2.pub), node1.config.receive_minimum.number () * 2);
	ASSERT_TIMELY_EQ (5s, node1.balance (key2.pub), node1.config.receive_minimum.number () * 2);
}

TEST (node, vote_by_hash_bundle)
{
	// Keep max_hashes above system to ensure it is kept in scope as votes can be added during system destruction
	std::atomic<size_t> max_hashes{ 0 };
	celerix::test::system system (1);
	auto & node = *system.nodes[0];
	celerix::state_block_builder builder;
	std::vector<std::shared_ptr<celerix::state_block>> blocks;
	auto block = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	blocks.push_back (block);
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), blocks.back ()));
	for (auto i = 2; i < 200; ++i)
	{
		auto block = builder.make_block ()
					 .from (*blocks.back ())
					 .previous (blocks.back ()->hash ())
					 .balance (celerix::dev::constants.genesis_amount - i)
					 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					 .work (*system.work.generate (blocks.back ()->hash ()))
					 .build ();
		blocks.push_back (block);
		ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), blocks.back ()));
	}

	// Confirming last block will confirm whole chain and allow us to generate votes for those blocks later
	celerix::test::confirm (node.ledger, blocks.back ());

	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key1;
	system.wallet (0)->insert_adhoc (key1.prv);

	system.nodes[0]->observers.vote.add ([&max_hashes] (std::shared_ptr<celerix::vote> const & vote_a, std::shared_ptr<celerix::transport::channel> const &, celerix::vote_source, celerix::vote_code) {
		if (vote_a->hashes.size () > max_hashes)
		{
			max_hashes = vote_a->hashes.size ();
		}
	});

	for (auto const & block : blocks)
	{
		system.nodes[0]->generator.add (block->root (), block->hash ());
	}

	// Verify that bundling occurs. While reaching 12 should be common on most hardware in release mode,
	// we set this low enough to allow the test to pass on CI/with sanitizers.
	ASSERT_TIMELY (20s, max_hashes.load () >= 3);
}

// This test places block send1 onto every node. Then it creates block send2 (which is a fork of send1) and sends it to node1.
// Then it sends a vote for send2 to node1 and expects node2 to also get the block plus vote and confirm send2.
// TODO: This test enforces the order block followed by vote on node1, should vote followed by block also work? It doesn't currently.
TEST (node, vote_by_hash_republish)
{
	celerix::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	celerix::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);

	// send1 and send2 are forks of each other
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();

	// give block send1 to node1 and check that an election for send1 starts on both nodes
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.active.active (*send1));
	ASSERT_TIMELY (5s, node2.active.active (*send1));

	// give block send2 to node1 and wait until the block is received and processed by node1
	node1.network.filter.clear ();
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));

	// construct a vote for send2 in order to overturn send1
	std::vector<celerix::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { vote_blocks });
	node1.vote_processor.vote (vote, std::make_shared<celerix::transport::fake::channel> (node1));

	// send2 should win on both nodes
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.balance (key2.pub), node1.config.receive_minimum.number () * 2);
	ASSERT_TIMELY_EQ (5s, node1.balance (key2.pub), node1.config.receive_minimum.number () * 2);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/celerixcurrency/celerix-node/pull/3629
// Issue for investigating it: https://github.com/celerixcurrency/celerix-node/issues/3638
TEST (node, DISABLED_vote_by_hash_epoch_block_republish)
{
	celerix::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	celerix::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	auto send1 = celerix::send_block_builder ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto epoch1 = celerix::state_block_builder ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (celerix::dev::genesis->hash ()))
				  .build ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	node1.active.publish (epoch1);
	std::vector<celerix::block_hash> vote_blocks;
	vote_blocks.push_back (epoch1->hash ());
	auto vote = celerix::test::make_vote (celerix::dev::genesis_key, { vote_blocks }, 0, 0);
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TRUE (node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<celerix::transport::fake::channel> (node1));
	ASSERT_TIMELY (10s, node1.block (epoch1->hash ()));
	ASSERT_TIMELY (10s, node2.block (epoch1->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
}

TEST (node, epoch_conflict_confirm)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto & node0 = *system.add_node (node_config);
	node_config.peering_port = system.get_available_port ();
	auto & node1 = *system.add_node (node_config);
	celerix::keypair key;
	celerix::keypair epoch_signer (celerix::dev::genesis_key);
	celerix::state_block_builder builder;

	// Node 1 is the voting node
	// Send sends to an account we control: send -> open -> change
	// Send2 sends to an account with public key of the open block
	// Epoch open qualified root: (open, 0) on account with the same public key as the hash of the open block
	// Epoch open and change have the same root!

	auto send = builder.make_block ()
				.account (celerix::dev::genesis_key.pub)
				.previous (celerix::dev::genesis->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.balance (celerix::dev::constants.genesis_amount - 1)
				.link (key.pub)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (celerix::dev::genesis->hash ()))
				.build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto change = builder.make_block ()
				  .account (key.pub)
				  .previous (open->hash ())
				  .representative (key.pub)
				  .balance (1)
				  .link (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (open->hash ()))
				  .build ();
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2)
				 .link (open->hash ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto epoch_open = builder.make_block ()
					  .account (change->root ().as_account ())
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node0.ledger.epoch_link (celerix::epoch::epoch_1))
					  .sign (epoch_signer.prv, epoch_signer.pub)
					  .work (*system.work.generate (open->hash ()))
					  .build ();

	// Process initial blocks
	ASSERT_TRUE (celerix::test::process (node0, celerix::test::clone ({ send, send2, open })));
	ASSERT_TRUE (celerix::test::process (node1, celerix::test::clone ({ send, send2, open })));

	// Process conflicting blocks on nodes as blocks coming from live network
	ASSERT_TRUE (celerix::test::process_live (node0, celerix::test::clone ({ change, epoch_open })));
	ASSERT_TRUE (celerix::test::process_live (node1, celerix::test::clone ({ change, epoch_open })));

	// Ensure blocks were propagated to both nodes
	ASSERT_TIMELY (5s, celerix::test::exists (node0, { change, epoch_open }));
	ASSERT_TIMELY (5s, celerix::test::exists (node1, { change, epoch_open }));

	// Confirm initial blocks in node1 to allow generating votes later
	celerix::test::confirm (node1, { change, epoch_open, send2 });
	ASSERT_TIMELY (5s, celerix::test::confirmed (node1, { change, epoch_open, send2 }));

	// Start elections on node0 for conflicting change and epoch_open blocks (these two blocks have the same root)
	ASSERT_TRUE (celerix::test::activate (node0, { change, epoch_open }));
	ASSERT_TIMELY (5s, celerix::test::active (node0, { change, epoch_open }));

	// Make node1 a representative so it can vote for both blocks
	system.wallet (1)->insert_adhoc (celerix::dev::genesis_key.prv);

	// Ensure the elections for conflicting blocks have started
	ASSERT_TIMELY (5s, celerix::test::active (node0, { change, epoch_open }));

	// Ensure both conflicting blocks were successfully processed and confirmed
	ASSERT_TIMELY (5s, celerix::test::confirmed (node0, { change, epoch_open }));
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/celerixcurrency/celerix-node/pull/3526
// Issue for investigating it: https://github.com/celerixcurrency/celerix-node/issues/3527
TEST (node, DISABLED_fork_invalid_block_signature)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	// Disabling republishing + waiting for a rollback before sending the correct vote below fixes an intermittent failure in this test
	// If these are taken out, one of two things may cause the test two fail often:
	// - Block *send2* might get processed before the rollback happens, simply due to timings, with code "fork", and not be processed again. Waiting for the rollback fixes this issue.
	// - Block *send1* might get processed again after the rollback happens, which causes *send2* to be processed with code "fork". Disabling block republishing ensures "send1" is not processed again.
	// An alternative would be to repeatedly flood the correct vote
	node_flags.disable_block_processor_republishing = true;
	auto & node1 (*system.add_node (node_flags));
	auto & node2 (*system.add_node (node_flags));
	celerix::keypair key2;
	celerix::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<celerix::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2_corrupt (std::make_shared<celerix::send_block> (*send2));
	send2_corrupt->signature = celerix::signature (123);
	auto vote = celerix::test::make_vote (celerix::dev::genesis_key, { send2 }, 0, 0);
	auto vote_corrupt = celerix::test::make_vote (celerix::dev::genesis_key, { send2_corrupt }, 0, 0);

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()));
	// Send the vote with the corrupt block signature
	node2.network.flood_vote (vote_corrupt, 1.0f);
	// Wait for the rollback
	ASSERT_TIMELY (5s, node1.stats.count (celerix::stat::type::rollback));
	// Send the vote with the correct block
	node2.network.flood_vote (vote, 1.0f);
	ASSERT_TIMELY (10s, !node1.block (send1->hash ()));
	ASSERT_TIMELY (10s, node1.block (send2->hash ()));
	ASSERT_EQ (node1.block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, fork_election_invalid_block_signature)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::block_builder builder;
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();
	auto send2 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();
	auto send3 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .sign (celerix::dev::genesis_key.prv, 0) // Invalid signature
				 .build ();

	auto channel1 = std::make_shared<celerix::transport::fake::channel> (node1);
	node1.inbound (celerix::publish{ celerix::dev::network_params.network, send1 }, channel1);
	ASSERT_TIMELY (5s, node1.active.active (send1->qualified_root ()));
	auto election (node1.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());
	node1.inbound (celerix::publish{ celerix::dev::network_params.network, send3 }, channel1);
	node1.inbound (celerix::publish{ celerix::dev::network_params.network, send2 }, channel1);
	ASSERT_TIMELY (3s, election->blocks ().size () > 1);
	ASSERT_EQ (election->blocks ()[send2->hash ()]->block_signature (), send2->block_signature ());
}

TEST (node, block_processor_signatures)
{
	celerix::test::system system{ 1 };
	auto & node1 = *system.nodes[0];
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_hash latest = system.nodes[0]->latest (celerix::dev::genesis_key.pub);
	celerix::state_block_builder builder;
	celerix::keypair key1;
	celerix::keypair key2;
	celerix::keypair key3;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (latest)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (latest))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (key2.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 3 * celerix::Kcelerix_ratio)
				 .link (key3.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send2->hash ()))
				 .build ();
	// Invalid signature bit
	auto send4 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 4 * celerix::Kcelerix_ratio)
				 .link (key3.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send3->hash ()))
				 .build ();
	send4->signature.bytes[32] ^= 0x1;
	// Invalid signature bit (force)
	auto send5 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 5 * celerix::Kcelerix_ratio)
				 .link (key3.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send3->hash ()))
				 .build ();
	send5->signature.bytes[32] ^= 0x1;
	// Invalid signature to unchecked
	node1.unchecked.put (send5->previous (), celerix::unchecked_info{ send5 });
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node1.work_generate_blocking (key1.pub))
					.build ();
	auto receive2 = builder.make_block ()
					.account (key2.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send2->hash ())
					.sign (key2.prv, key2.pub)
					.work (*node1.work_generate_blocking (key2.pub))
					.build ();
	// Invalid private key
	auto receive3 = builder.make_block ()
					.account (key3.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send3->hash ())
					.sign (key2.prv, key3.pub)
					.work (*node1.work_generate_blocking (key3.pub))
					.build ();
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (receive1);
	node1.process_active (receive2);
	node1.process_active (receive3);
	ASSERT_TIMELY (5s, node1.block (receive2->hash ()) != nullptr); // Implies send1, send2, send3, receive1.
	ASSERT_TIMELY_EQ (5s, node1.unchecked.count (), 0);
	ASSERT_EQ (nullptr, node1.block (receive3->hash ())); // Invalid signer
	ASSERT_EQ (nullptr, node1.block (send4->hash ())); // Invalid signature via process_active
	ASSERT_EQ (nullptr, node1.block (send5->hash ())); // Invalid signature via unchecked
}

/*
 *  State blocks go through a different signature path, ensure invalidly signed state blocks are rejected
 *  This test can freeze if the wake conditions in block_processor::flush are off, for that reason this is done async here
 */
TEST (node, block_processor_reject_state)
{
	celerix::test::system system (1);
	auto & node (*system.nodes[0]);
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	send1->signature.bytes[0] ^= 1;
	ASSERT_FALSE (node.block_or_pruned_exists (send1->hash ()));
	node.process_active (send1);
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (celerix::stat::type::block_processor_result, celerix::stat::detail::bad_signature));
	ASSERT_FALSE (node.block_or_pruned_exists (send1->hash ()));
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	node.process_active (send2);
	ASSERT_TIMELY (5s, node.block_or_pruned_exists (send2->hash ()));
}

TEST (node, confirm_back)
{
	celerix::test::system system (1);
	celerix::keypair key;
	auto & node (*system.nodes[0]);
	auto genesis_start_balance (node.balance (celerix::dev::genesis_key.pub));
	auto send1 = celerix::send_block_builder ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key.pub)
				 .balance (genesis_start_balance - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	celerix::state_block_builder builder;
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send1->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto send2 = builder.make_block ()
				 .account (key.pub)
				 .previous (open->hash ())
				 .representative (key.pub)
				 .balance (0)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (open->hash ()))
				 .build ();
	node.process_active (send1);
	node.process_active (open);
	node.process_active (send2);
	ASSERT_TIMELY (5s, node.block (send2->hash ()) != nullptr);
	ASSERT_TRUE (celerix::test::start_elections (system, node, { send1, open, send2 }));
	ASSERT_EQ (3, node.active.size ());
	std::vector<celerix::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { vote_blocks });
	node.vote_processor.vote_blocking (vote, std::make_shared<celerix::transport::fake::channel> (node));
	ASSERT_TIMELY (10s, node.active.empty ());
}

TEST (node, peers)
{
	celerix::test::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());

	auto node2 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
	system.nodes.push_back (node2);

	auto endpoint = node1->network.endpoint ();
	celerix::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto & store = node2->store;
	{
		// Add a peer to the database
		auto transaction (store.tx_begin_write ());
		store.peer.put (transaction, endpoint_key, 37);

		// Add a peer which is not contactable
		store.peer.put (transaction, celerix::endpoint_key{ boost::asio::ip::address_v6::any ().to_bytes (), 55555 }, 42);
	}

	node2->start ();
	ASSERT_TIMELY (10s, !node2->network.empty () && !node1->network.empty ())
	// Wait to finish TCP node ID handshakes
	ASSERT_TIMELY (10s, node1->tcp_listener.realtime_count () != 0 && node2->tcp_listener.realtime_count () != 0);
	// Confirm that the peers match with the endpoints we are expecting
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->get_node_id (), list1[0]->get_node_id ());
	ASSERT_EQ (celerix::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->get_node_id (), list2[0]->get_node_id ());
	ASSERT_EQ (celerix::transport::transport_type::tcp, list2[0]->get_type ());

	// Uncontactable peer should not be stored
	ASSERT_TIMELY_EQ (5s, store.peer.count (store.tx_begin_read ()), 1);
	ASSERT_TRUE (store.peer.exists (store.tx_begin_read (), endpoint_key));

	// Stop the peer node and check that it is removed from the store
	system.stop_node (*node1);

	// TODO: In `tcp_channels::store_all` we skip store operation when there are no peers present,
	// so the best we can do here is check if network is empty
	ASSERT_TIMELY (10s, node2->network.empty ());
}

TEST (node, peer_history_restart)
{
	celerix::test::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());
	auto endpoint = node1->network.endpoint ();
	celerix::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto path (celerix::unique_path ());
	{
		auto node2 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), path, system.work));
		system.nodes.push_back (node2);
		auto & store = node2->store;
		{
			// Add a peer to the database
			auto transaction (store.tx_begin_write ());
			store.peer.put (transaction, endpoint_key, 37);
		}
		node2->start ();
		ASSERT_TIMELY (10s, !node2->network.empty ());
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node2->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_remote_endpoint ());
		ASSERT_EQ (1, node2->network.size ());
		system.stop_node (*node2);
	}
	// Restart node
	{
		celerix::node_flags node_flags;
		node_flags.read_only = true;
		auto node3 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), path, system.work, node_flags));
		system.nodes.push_back (node3);
		// Check cached peers after restart
		node3->network.start ();
		node3->add_initial_peers ();

		auto & store = node3->store;
		{
			auto transaction (store.tx_begin_read ());
			ASSERT_EQ (store.peer.count (transaction), 1);
			ASSERT_TRUE (store.peer.exists (transaction, endpoint_key));
		}
		ASSERT_TIMELY (10s, !node3->network.empty ());
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node3->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_remote_endpoint ());
		ASSERT_EQ (1, node3->network.size ());
		system.stop_node (*node3);
	}
}

/** This checks that a node can be opened (without being blocked) when a write lock is held elsewhere */
TEST (node, dont_write_lock_node)
{
	auto path = celerix::unique_path ();

	std::promise<void> write_lock_held_promise;
	std::promise<void> finished_promise;
	std::thread ([&path, &write_lock_held_promise, &finished_promise] () {
		celerix::logger logger;
		auto store = celerix::make_store (logger, path, celerix::dev::constants, false, true);
		{
			celerix::ledger_cache ledger_cache{ store->rep_weight };
			auto transaction (store->tx_begin_write ());
			store->initialize (transaction, ledger_cache, celerix::dev::constants);
		}

		// Hold write lock open until main thread is done needing it
		auto transaction (store->tx_begin_write ());
		write_lock_held_promise.set_value ();
		finished_promise.get_future ().wait ();
	})
	.detach ();

	write_lock_held_promise.get_future ().wait ();

	// Check inactive node can finish executing while a write lock is open
	celerix::inactive_node node (path, celerix::inactive_node_flag_defaults ());
	finished_promise.set_value ();
}

TEST (node, bidirectional_tcp)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	// Disable bootstrap to start elections for new blocks
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto node1 = system.add_node (node_config, node_flags);
	node_config.peering_port = system.get_available_port ();
	node_config.tcp.max_inbound_connections = 0; // Disable incoming TCP connections for node 2
	auto node2 = system.add_node (node_config, node_flags);
	// Check network connections
	ASSERT_EQ (1, node1->network.size ());
	ASSERT_EQ (1, node2->network.size ());
	auto list1 (node1->network.list (1));
	ASSERT_EQ (celerix::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_NE (node2->network.endpoint (), list1[0]->get_remote_endpoint ()); // Ephemeral port
	ASSERT_EQ (node2->node_id.pub, list1[0]->get_node_id ());
	auto list2 (node2->network.list (1));
	ASSERT_EQ (celerix::transport::transport_type::tcp, list2[0]->get_type ());
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_remote_endpoint ());
	ASSERT_EQ (node1->node_id.pub, list2[0]->get_node_id ());
	// Test block propagation from node 1
	celerix::keypair key;
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	node1->process_active (send1);
	ASSERT_TIMELY (10s, node1->block_or_pruned_exists (send1->hash ()) && node2->block_or_pruned_exists (send1->hash ()));
	// Test block confirmation from node 1 (add representative to node 1)
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	// Wait to find new reresentative
	ASSERT_TIMELY (10s, node2->rep_crawler.representative_count () != 0);
	/* Wait for confirmation
	To check connection we need only node 2 confirmation status
	Node 1 election can be unconfirmed because representative private key was inserted after election start (and node 2 isn't flooding new votes to principal representatives) */
	bool confirmed (false);
	system.deadline_set (10s);
	while (!confirmed)
	{
		auto transaction2 = node2->ledger.tx_begin_read ();
		confirmed = node2->ledger.confirmed.block_exists_or_pruned (transaction2, send1->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	// Test block propagation & confirmation from node 2 (remove representative from node 1)
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, celerix::dev::genesis_key.pub);
	}
	/* Test block propagation from node 2
	Node 2 has only ephemeral TCP port open. Node 1 cannot establish connection to node 2 listening port */
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (send1->hash ()))
				 .build ();
	node2->process_active (send2);
	ASSERT_TIMELY (10s, node1->block_or_pruned_exists (send2->hash ()) && node2->block_or_pruned_exists (send2->hash ()));
	// Test block confirmation from node 2 (add representative to node 2)
	system.wallet (1)->insert_adhoc (celerix::dev::genesis_key.prv);
	// Wait to find changed reresentative
	ASSERT_TIMELY (10s, node1->rep_crawler.representative_count () != 0);
	/* Wait for confirmation
	To check connection we need only node 1 confirmation status
	Node 2 election can be unconfirmed because representative private key was inserted after election start (and node 1 isn't flooding new votes to principal representatives) */
	confirmed = false;
	system.deadline_set (20s);
	while (!confirmed)
	{
		auto transaction1 = node1->ledger.tx_begin_read ();
		confirmed = node1->ledger.confirmed.block_exists_or_pruned (transaction1, send2->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, node_sequence)
{
	celerix::test::system system (3);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (1, system.nodes[1]->node_seq);
	ASSERT_EQ (2, system.nodes[2]->node_seq);
}

/**
 * This test checks that a node can generate a self generated vote to rollback an election.
 * It also checks that the vote aggregrator replies with the election winner at the time.
 */
TEST (node, rollback_vote_self)
{
	celerix::test::system system;
	celerix::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	celerix::state_block_builder builder;
	celerix::keypair key;

	// send half the voting weight to a non voting rep to ensure quorum cannot be reached
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (celerix::dev::constants.genesis_amount - (celerix::dev::constants.genesis_amount / 2))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (celerix::dev::constants.genesis_amount / 2)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	// send 1 raw
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	// fork of send2 block
	auto fork = builder.make_block ()
				.from (*send2)
				.balance (send1->balance_field ().value ().number () - 2)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.build ();

	// Process and mark the first 2 blocks as confirmed to allow voting
	ASSERT_TRUE (celerix::test::process (node, { send1, open }));
	celerix::test::confirm (node.ledger, open);

	// wait until the rep weights have caught up with the weight transfer
	ASSERT_TIMELY_EQ (5s, celerix::dev::constants.genesis_amount / 2, node.weight (key.pub));

	// process forked blocks, send2 will be the winner because it was first and there are no votes yet
	node.process_active (send2);
	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()));
	node.process_active (fork);
	ASSERT_TIMELY_EQ (5s, 2, election->blocks ().size ());
	ASSERT_EQ (election->winner ()->hash (), send2->hash ());

	{
		// The write guard prevents the block processor from performing the rollback
		auto write_guard = node.store.write_queue.wait (celerix::store::writer::testing);

		ASSERT_EQ (0, election->votes_with_weight ().size ());
		// Vote with key to switch the winner
		election->vote (key.pub, 0, fork->hash (), celerix::vote_source::live);
		ASSERT_EQ (1, election->votes_with_weight ().size ());
		// The winner changed
		ASSERT_EQ (election->winner ()->hash (), fork->hash ());

		// Insert genesis key in the wallet
		system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

		// Without the rollback being finished, the aggregator should not reply with any vote
		auto channel = std::make_shared<celerix::transport::fake::channel> (node);
		node.aggregator.request ({ { send2->hash (), send2->root () } }, channel);
		ASSERT_ALWAYS_EQ (1s, node.stats.count (celerix::stat::type::request_aggregator_replies), 0);

		// Going out of the scope allows the rollback to complete
	}

	// A vote is eventually generated from the local representative
	auto is_genesis_vote = [] (celerix::vote_with_weight_info info) {
		return info.representative == celerix::dev::genesis_key.pub;
	};
	ASSERT_TIMELY_EQ (5s, 2, election->votes_with_weight ().size ());
	auto votes_with_weight = election->votes_with_weight ();
	ASSERT_EQ (1, std::count_if (votes_with_weight.begin (), votes_with_weight.end (), is_genesis_vote));
	auto vote = std::find_if (votes_with_weight.begin (), votes_with_weight.end (), is_genesis_vote);
	ASSERT_NE (votes_with_weight.end (), vote);
	ASSERT_EQ (fork->hash (), vote->hash);
}

TEST (node, rollback_gap_source)
{
	celerix::test::system system;
	celerix::node_config node_config (system.get_available_port ());
	node_config.backlog_scan.enable = false;
	auto & node = *system.add_node (node_config);
	celerix::state_block_builder builder;
	celerix::keypair key;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	// Side a of a forked open block receiving from send1
	// This is a losing block
	auto fork1a = builder.make_block ()
				  .account (key.pub)
				  .previous (0)
				  .representative (key.pub)
				  .link (send1->hash ())
				  .balance (1)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (key.pub))
				  .build ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	// Side b of a forked open block receiving from send2.
	// This is the winning block
	auto fork1b = builder.make_block ()
				  .from (*fork1a)
				  .link (send2->hash ())
				  .sign (key.prv, key.pub)
				  .build ();
	// Set 'node' up with losing block 'fork1a'
	ASSERT_EQ (celerix::block_status::progress, node.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node.process (fork1a));
	// Node has 'fork1a' & doesn't have source 'send2' for winning 'fork1b' block
	ASSERT_EQ (nullptr, node.block (send2->hash ()));
	node.block_processor.force (fork1b);
	ASSERT_TIMELY_EQ (5s, node.block (fork1a->hash ()), nullptr);
	// Wait for the rollback (attempt to replace fork with open)
	ASSERT_TIMELY_EQ (5s, node.stats.count (celerix::stat::type::rollback, celerix::stat::detail::open), 1);
	// But replacing is not possible (missing source block - send2)
	ASSERT_EQ (nullptr, node.block (fork1b->hash ()));
	// Fork can be returned by some other forked node
	node.process_active (fork1a);
	ASSERT_TIMELY (5s, node.block (fork1a->hash ()) != nullptr);
	// With send2 block in ledger election can start again to remove fork block
	ASSERT_EQ (celerix::block_status::progress, node.process (send2));
	node.block_processor.force (fork1b);
	// Wait for new rollback
	ASSERT_TIMELY_EQ (5s, node.stats.count (celerix::stat::type::rollback, celerix::stat::detail::open), 2);
	// Now fork block should be replaced with open
	ASSERT_TIMELY (5s, node.block (fork1b->hash ()) != nullptr);
	ASSERT_EQ (nullptr, node.block (fork1a->hash ()));
}

// Confirm a complex dependency graph starting from the first block
TEST (node, dependency_graph)
{
	celerix::test::system system;
	celerix::node_config config (system.get_available_port ());
	config.backlog_scan.enable = false;
	auto & node = *system.add_node (config);

	celerix::state_block_builder builder;
	celerix::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (celerix::dev::genesis_key.pub)
					 .previous (celerix::dev::genesis->hash ())
					 .representative (celerix::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (celerix::dev::constants.genesis_amount - 1)
					 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					 .work (*system.work.generate (celerix::dev::genesis->hash ()))
					 .build ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
					 .account (key1.pub)
					 .previous (0)
					 .representative (key1.pub)
					 .link (gen_send1->hash ())
					 .balance (1)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
					  .account (key1.pub)
					  .previous (key1_open->hash ())
					  .representative (key1.pub)
					  .link (celerix::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (celerix::dev::constants.genesis_amount)
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance_field ().value ().number () - 2)
					 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					 .work (*system.work.generate (gen_receive->hash ()))
					 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
					 .account (key2.pub)
					 .previous (0)
					 .representative (key2.pub)
					 .link (gen_send2->hash ())
					 .balance (2)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
					  .account (key2.pub)
					  .previous (key2_open->hash ())
					  .representative (key2.pub)
					  .link (key3.pub)
					  .balance (1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_open->hash ()))
					  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
					 .account (key3.pub)
					 .previous (0)
					 .representative (key3.pub)
					 .link (key2_send1->hash ())
					 .balance (1)
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
					  .from (*key2_send1)
					  .previous (key2_send1->hash ())
					  .link (key1.pub)
					  .balance (key2_send1->balance_field ().value ().number () - 1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_send1->hash ()))
					  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
						.from (*key1_send1)
						.previous (key1_send1->hash ())
						.link (key2_send2->hash ())
						.balance (key1_send1->balance_field ().value ().number () + 1)
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (key1_send1->hash ()))
						.build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
					  .from (*key1_receive)
					  .previous (key1_receive->hash ())
					  .link (key3.pub)
					  .balance (key1_receive->balance_field ().value ().number () - 1)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_receive->hash ()))
					  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
						.from (*key3_open)
						.previous (key3_open->hash ())
						.link (key1_send2->hash ())
						.balance (key3_open->balance_field ().value ().number () + 1)
						.sign (key3.prv, key3.pub)
						.work (*system.work.generate (key3_open->hash ()))
						.build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
					  .from (*key3_receive)
					  .previous (key3_receive->hash ())
					  .link (node.ledger.epoch_link (celerix::epoch::epoch_1))
					  .balance (key3_receive->balance_field ().value ())
					  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	ASSERT_EQ (celerix::block_status::progress, node.process (gen_send1));
	ASSERT_EQ (celerix::block_status::progress, node.process (key1_open));
	ASSERT_EQ (celerix::block_status::progress, node.process (key1_send1));
	ASSERT_EQ (celerix::block_status::progress, node.process (gen_receive));
	ASSERT_EQ (celerix::block_status::progress, node.process (gen_send2));
	ASSERT_EQ (celerix::block_status::progress, node.process (key2_open));
	ASSERT_EQ (celerix::block_status::progress, node.process (key2_send1));
	ASSERT_EQ (celerix::block_status::progress, node.process (key3_open));
	ASSERT_EQ (celerix::block_status::progress, node.process (key2_send2));
	ASSERT_EQ (celerix::block_status::progress, node.process (key1_receive));
	ASSERT_EQ (celerix::block_status::progress, node.process (key1_send2));
	ASSERT_EQ (celerix::block_status::progress, node.process (key3_receive));
	ASSERT_EQ (celerix::block_status::progress, node.process (key3_epoch));
	ASSERT_TRUE (node.active.empty ());

	// Hash -> Ancestors
	std::unordered_map<celerix::block_hash, std::vector<celerix::block_hash>> dependency_graph{
		{ key1_open->hash (), { gen_send1->hash () } },
		{ key1_send1->hash (), { key1_open->hash () } },
		{ gen_receive->hash (), { gen_send1->hash (), key1_open->hash () } },
		{ gen_send2->hash (), { gen_receive->hash () } },
		{ key2_open->hash (), { gen_send2->hash () } },
		{ key2_send1->hash (), { key2_open->hash () } },
		{ key3_open->hash (), { key2_send1->hash () } },
		{ key2_send2->hash (), { key2_send1->hash () } },
		{ key1_receive->hash (), { key1_send1->hash (), key2_send2->hash () } },
		{ key1_send2->hash (), { key1_send1->hash () } },
		{ key3_receive->hash (), { key3_open->hash (), key1_send2->hash () } },
		{ key3_epoch->hash (), { key3_receive->hash () } },
	};
	ASSERT_EQ (node.ledger.block_count () - 2, dependency_graph.size ());

	// Start an election for the first block of the dependency graph, and ensure all blocks are eventually confirmed
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	node.start_election (gen_send1);

	ASSERT_NO_ERROR (system.poll_until_true (15s, [&] {
		// Not many blocks should be active simultaneously
		EXPECT_LT (node.active.size (), 6);

		// Ensure that active blocks have their ancestors confirmed
		auto error = std::any_of (dependency_graph.cbegin (), dependency_graph.cend (), [&] (auto entry) {
			if (node.vote_router.active (entry.first))
			{
				for (auto ancestor : entry.second)
				{
					if (!node.block_confirmed (ancestor))
					{
						return true;
					}
				}
			}
			return false;
		});

		EXPECT_FALSE (error);
		return error || node.ledger.cemented_count () == node.ledger.block_count ();
	}));
	ASSERT_EQ (node.ledger.cemented_count (), node.ledger.block_count ());
	ASSERT_TIMELY (5s, node.active.empty ());
}

// Confirm a complex dependency graph. Uses frontiers confirmation which will fail to
// confirm a frontier optimistically then fallback to pessimistic confirmation.
TEST (node, dependency_graph_frontier)
{
	celerix::test::system system;
	celerix::node_config config (system.get_available_port ());
	config.backlog_scan.enable = false;
	auto & node1 = *system.add_node (config);
	config.peering_port = system.get_available_port ();
	config.backlog_scan.enable = true;
	auto & node2 = *system.add_node (config);

	celerix::state_block_builder builder;
	celerix::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (celerix::dev::genesis_key.pub)
					 .previous (celerix::dev::genesis->hash ())
					 .representative (celerix::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (celerix::dev::constants.genesis_amount - 1)
					 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					 .work (*system.work.generate (celerix::dev::genesis->hash ()))
					 .build ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
					 .account (key1.pub)
					 .previous (0)
					 .representative (key1.pub)
					 .link (gen_send1->hash ())
					 .balance (1)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
					  .account (key1.pub)
					  .previous (key1_open->hash ())
					  .representative (key1.pub)
					  .link (celerix::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (celerix::dev::constants.genesis_amount)
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance_field ().value ().number () - 2)
					 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					 .work (*system.work.generate (gen_receive->hash ()))
					 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
					 .account (key2.pub)
					 .previous (0)
					 .representative (key2.pub)
					 .link (gen_send2->hash ())
					 .balance (2)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
					  .account (key2.pub)
					  .previous (key2_open->hash ())
					  .representative (key2.pub)
					  .link (key3.pub)
					  .balance (1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_open->hash ()))
					  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
					 .account (key3.pub)
					 .previous (0)
					 .representative (key3.pub)
					 .link (key2_send1->hash ())
					 .balance (1)
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
					  .from (*key2_send1)
					  .previous (key2_send1->hash ())
					  .link (key1.pub)
					  .balance (key2_send1->balance_field ().value ().number () - 1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_send1->hash ()))
					  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
						.from (*key1_send1)
						.previous (key1_send1->hash ())
						.link (key2_send2->hash ())
						.balance (key1_send1->balance_field ().value ().number () + 1)
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (key1_send1->hash ()))
						.build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
					  .from (*key1_receive)
					  .previous (key1_receive->hash ())
					  .link (key3.pub)
					  .balance (key1_receive->balance_field ().value ().number () - 1)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_receive->hash ()))
					  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
						.from (*key3_open)
						.previous (key3_open->hash ())
						.link (key1_send2->hash ())
						.balance (key3_open->balance_field ().value ().number () + 1)
						.sign (key3.prv, key3.pub)
						.work (*system.work.generate (key3_open->hash ()))
						.build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
					  .from (*key3_receive)
					  .previous (key3_receive->hash ())
					  .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
					  .balance (key3_receive->balance_field ().value ())
					  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	for (auto const & node : system.nodes)
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, gen_send1));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key1_open));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key1_send1));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, gen_receive));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, gen_send2));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key2_open));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key2_send1));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key3_open));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key2_send2));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key1_receive));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key1_send2));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key3_receive));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, key3_epoch));
	}

	// node1 can vote, but only on the first block
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	ASSERT_TIMELY (10s, node2.active.active (gen_send1->qualified_root ()));
	node1.start_election (gen_send1);

	ASSERT_TIMELY_EQ (15s, node1.ledger.cemented_count (), node1.ledger.block_count ());
	ASSERT_TIMELY_EQ (15s, node2.ledger.cemented_count (), node2.ledger.block_count ());
}

namespace celerix
{
TEST (node, deferred_dependent_elections)
{
	celerix::test::system system;
	celerix::node_config node_config_1{ system.get_available_port () };
	node_config_1.backlog_scan.enable = false;
	celerix::node_config node_config_2{ system.get_available_port () };
	node_config_2.backlog_scan.enable = false;
	celerix::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config_1, flags);
	auto & node2 = *system.add_node (node_config_2, flags); // node2 will be used to ensure all blocks are being propagated

	celerix::state_block_builder builder;
	celerix::keypair key;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto receive = builder.make_block ()
				   .from (*open)
				   .previous (open->hash ())
				   .link (send2->hash ())
				   .balance (2)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (open->hash ()))
				   .build ();
	auto fork = builder.make_block ()
				.from (*receive)
				.representative (celerix::dev::genesis_key.pub) // was key.pub
				.sign (key.prv, key.pub)
				.build ();

	celerix::test::process (node, { send1 });
	auto election_send1 = celerix::test::start_election (system, node, send1->hash ());
	ASSERT_NE (nullptr, election_send1);

	// Should process and republish but not start an election for any dependent blocks
	celerix::test::process (node, { open, send2 });
	ASSERT_TIMELY (5s, node.block (open->hash ()));
	ASSERT_TIMELY (5s, node.block (send2->hash ()));
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()) || node.active.active (send2->qualified_root ()));
	ASSERT_TIMELY (5s, node2.block (open->hash ()));
	ASSERT_TIMELY (5s, node2.block (send2->hash ()));

	// Re-processing older blocks with updated work also does not start an election
	node.work_generate_blocking (*open, celerix::dev::network_params.work.difficulty (*open) + 1);
	node.process_local (open);
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()));

	// It is however possible to manually start an election from elsewhere
	ASSERT_TRUE (celerix::test::start_election (system, node, open->hash ()));
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));

	/// The election was dropped but it's still not possible to restart it
	node.work_generate_blocking (*open, celerix::dev::network_params.work.difficulty (*open) + 1);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	node.process_local (open);
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()));

	// Drop both elections
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	node.active.erase (*send2);
	ASSERT_FALSE (node.active.active (send2->qualified_root ()));

	// Confirming send1 will automatically start elections for the dependents
	election_send1->force_confirm ();
	ASSERT_TIMELY (5s, node.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (5s, node.active.active (open->qualified_root ()));
	ASSERT_TIMELY (5s, node.active.active (send2->qualified_root ()));
	auto election_open = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election_open);
	auto election_send2 = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election_open);

	// Confirm one of the dependents of the receive but not the other, to ensure both have to be confirmed to start an election on processing
	ASSERT_EQ (celerix::block_status::progress, node.process (receive));
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));
	election_open->force_confirm ();
	ASSERT_TIMELY (5s, node.block_confirmed (open->hash ()));
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.ledger.tx_begin_read (), *receive));
	ASSERT_NEVER (0.5s, node.active.active (receive->qualified_root ()));
	ASSERT_FALSE (node.ledger.rollback (node.ledger.tx_begin_write (), receive->hash ()));
	ASSERT_FALSE (node.block (receive->hash ()));
	node.process_local (receive);
	ASSERT_TIMELY (5s, node.block (receive->hash ()));
	ASSERT_NEVER (0.5s, node.active.active (receive->qualified_root ()));

	// Processing a fork will also not start an election
	ASSERT_EQ (celerix::block_status::fork, node.process (fork));
	node.process_local (fork);
	ASSERT_NEVER (0.5s, node.active.active (receive->qualified_root ()));

	// Confirming the other dependency allows starting an election from a fork
	election_send2->force_confirm ();
	ASSERT_TIMELY (5s, node.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node.active.active (receive->qualified_root ()));
}
}

// Test that a node configured with `enable_pruning` and `max_pruning_age = 1s` will automatically
// prune old confirmed blocks without explicitly saying `node.ledger_pruning` in the unit test
TEST (node, pruning_automatic)
{
	celerix::test::system system{};

	celerix::node_config node_config{ system.get_available_port () };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;
	node_config.max_pruning_age = std::chrono::seconds (1);

	celerix::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	celerix::keypair key1{};
	celerix::send_block_builder builder{};
	auto latest_hash = celerix::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.block (send2->hash ()) != nullptr);

	// Force-confirm both blocks
	node1.confirming_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.confirming_set.add (send2->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Check pruning result
	ASSERT_EQ (3, node1.ledger.block_count ());
	ASSERT_TIMELY_EQ (5s, node1.ledger.pruned_count (), 1);
	ASSERT_TIMELY_EQ (5s, node1.store.pruned.count (node1.store.tx_begin_read ()), 1);
	ASSERT_EQ (1, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	ASSERT_TRUE (celerix::test::block_or_pruned_all_exists (node1, { celerix::dev::genesis, send1, send2 }));
}

TEST (node, DISABLED_pruning_age)
{
	celerix::test::system system{};

	celerix::node_config node_config{ system.get_available_port () };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	celerix::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	celerix::keypair key1{};
	celerix::send_block_builder builder{};
	auto latest_hash = celerix::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.confirming_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.confirming_set.add (send2->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Three blocks in total, nothing pruned yet
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with default age 1 day
	node1.ledger_pruning (1, true);
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with max age 0
	node1.config.max_pruning_age = std::chrono::seconds{ 0 };
	node1.ledger_pruning (1, true);
	ASSERT_EQ (1, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	ASSERT_TRUE (celerix::test::block_or_pruned_all_exists (node1, { celerix::dev::genesis, send1, send2 }));
}

// Test that a node configured with `enable_pruning` will
// prune DEEP-enough confirmed blocks by explicitly saying `node.ledger_pruning` in the unit test
TEST (node, DISABLED_pruning_depth)
{
	celerix::test::system system{};

	celerix::node_config node_config{ system.get_available_port () };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	celerix::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	celerix::keypair key1{};
	celerix::send_block_builder builder{};
	auto latest_hash = celerix::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.confirming_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.confirming_set.add (send2->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Three blocks in total, nothing pruned yet
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with default depth (unlimited)
	node1.ledger_pruning (1, true);
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with max depth 1
	node1.config.max_pruning_depth = 1;
	node1.ledger_pruning (1, true);
	ASSERT_EQ (1, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	ASSERT_TRUE (celerix::test::block_or_pruned_all_exists (node1, { celerix::dev::genesis, send1, send2 }));
}

TEST (node_config, node_id_private_key_persistence)
{
	celerix::test::system system;

	// create the directory and the file
	auto path = celerix::unique_path ();
	ASSERT_TRUE (std::filesystem::exists (path));
	auto priv_key_filename = path / "node_id_private.key";

	// check that the key generated is random when the key does not exist
	celerix::keypair kp1 = celerix::load_or_create_node_id (path);
	std::filesystem::remove (priv_key_filename);
	celerix::keypair kp2 = celerix::load_or_create_node_id (path);
	ASSERT_NE (kp1.prv, kp2.prv);

	// check that the key persists
	celerix::keypair kp3 = celerix::load_or_create_node_id (path);
	ASSERT_EQ (kp2.prv, kp3.prv);

	// write the key file manually and check that right key is loaded
	std::ofstream ofs (priv_key_filename.string (), std::ofstream::out | std::ofstream::trunc);
	ofs << "3F28D035B8AA75EA53DF753BFD065CF6138E742971B2C99B84FD8FE328FED2D9" << std::flush;
	ofs.close ();
	celerix::keypair kp4 = celerix::load_or_create_node_id (path);
	ASSERT_EQ (kp4.prv, celerix::keypair ("3F28D035B8AA75EA53DF753BFD065CF6138E742971B2C99B84FD8FE328FED2D9").prv);
}

TEST (node, port_mapping)
{
	celerix::test::system system;
	auto node = system.add_node ();
	node->port_mapping.refresh_devices ();
}

TEST (node, process_local_overflow)
{
	celerix::test::system system;
	auto config = system.default_config ();
	config.block_processor.max_system_queue = 0;
	auto & node = *system.add_node (config);

	celerix::keypair key1;
	celerix::send_block_builder builder;
	auto latest_hash = celerix::dev::genesis->hash ();
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();

	auto result = node.process_local (send1);
	ASSERT_FALSE (result);
}

TEST (node, local_block_broadcast)
{
	celerix::test::system system;

	// Disable active elections to prevent the block from being broadcasted by the election
	auto node_config = system.default_config ();
	node_config.priority_scheduler.enable = false;
	node_config.hinted_scheduler.enable = false;
	node_config.optimistic_scheduler.enable = false;
	node_config.local_block_broadcaster.rebroadcast_interval = 1s;
	auto & node1 = *system.add_node (node_config);
	auto & node2 = *system.make_disconnected_node ();

	celerix::keypair key1;
	celerix::send_block_builder builder;
	auto latest_hash = celerix::dev::genesis->hash ();
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();

	auto result = node1.process_local (send1);
	ASSERT_TRUE (result);
	ASSERT_NEVER (500ms, node1.active.active (send1->qualified_root ()));

	// Wait until a broadcast is attempted
	ASSERT_TIMELY_EQ (5s, node1.local_block_broadcaster.size (), 1);
	ASSERT_TIMELY (5s, node1.stats.count (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::broadcast, celerix::stat::dir::out) >= 1);

	// The other node should not have received the block
	ASSERT_NEVER (500ms, node2.block (send1->hash ()));

	// Connect the nodes and check that the block is propagated
	node1.network.merge_peer (node2.network.endpoint ());
	ASSERT_TIMELY (5s, node1.network.find_node_id (node2.get_node_id ()));
	ASSERT_TIMELY (10s, node2.block (send1->hash ()));
}

TEST (node, container_info)
{
	celerix::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();

	// Generate some random activity
	std::vector<celerix::account> accounts;
	auto dev_genesis_key = celerix::dev::genesis_key;
	system.wallet (0)->insert_adhoc (dev_genesis_key.prv);
	accounts.push_back (dev_genesis_key.pub);
	for (int n = 0; n < 10; ++n)
	{
		system.generate_activity (node1, accounts);
	}

	// This should just execute, sanitizers will catch any problems
	ASSERT_NO_THROW (node1.container_info ());
	ASSERT_NO_THROW (node2.container_info ());
}

TEST (node, bounded_backlog)
{
	celerix::test::system system;

	celerix::node_config node_config;
	node_config.max_backlog = 10;
	node_config.backlog_scan.enable = false;
	auto & node = *system.add_node (node_config);

	const int howmany_blocks = 64;
	const int howmany_chains = 16;

	auto chains = celerix::test::setup_chains (system, node, howmany_chains, howmany_blocks, celerix::dev::genesis_key, /* do not confirm */ false);

	node.backlog_scan.trigger ();

	ASSERT_TIMELY_EQ (20s, node.ledger.block_count (), 11); // 10 + genesis
}
