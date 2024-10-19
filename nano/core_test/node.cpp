#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/inactive_node.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/portmapping.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/manual.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/vote_generator.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <numeric>

using namespace std::chrono_literals;

TEST (node, null_account)
{
	auto const & null_account = nano::account::null ();
	ASSERT_EQ (null_account, nullptr);
	ASSERT_FALSE (null_account != nullptr);

	nano::account default_account{};
	ASSERT_FALSE (default_account == nullptr);
	ASSERT_NE (default_account, nullptr);
}

TEST (node, stop)
{
	nano::test::system system (1);
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.begin ());
	system.stop_node (*system.nodes[0]);
	ASSERT_TRUE (true);
}

TEST (node, work_generate)
{
	nano::test::system system (1);
	auto & node (*system.nodes[0]);
	nano::block_hash root{ 1 };
	nano::work_version version{ nano::work_version::work_1 };
	{
		auto difficulty = nano::difficulty::from_multiplier (1.5, node.network_params.work.base);
		auto work = node.work_generate_blocking (version, root, difficulty);
		ASSERT_TRUE (work.has_value ());
		ASSERT_GE (nano::dev::network_params.work.difficulty (version, root, work.value ()), difficulty);
	}
	{
		auto difficulty = nano::difficulty::from_multiplier (0.5, node.network_params.work.base);
		std::optional<uint64_t> work;
		do
		{
			work = node.work_generate_blocking (version, root, difficulty);
		} while (nano::dev::network_params.work.difficulty (version, root, work.value ()) >= node.network_params.work.base);
		ASSERT_TRUE (work.has_value ());
		ASSERT_GE (nano::dev::network_params.work.difficulty (version, root, work.value ()), difficulty);
		ASSERT_FALSE (nano::dev::network_params.work.difficulty (version, root, work.value ()) >= node.network_params.work.base);
	}
}

TEST (node, block_store_path_failure)
{
	nano::test::system system;
	auto path (nano::unique_path ());
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto node (std::make_shared<nano::node> (system.io_ctx, system.get_available_port (), path, pool));
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
	if (nano::rocksdb_config::using_rocksdb_in_tests ())
	{
		nano::inactive_node node (nano::unique_path (), nano::inactive_node_flag_defaults ());
		ASSERT_TRUE (node.node->init_error ());
	}
	else
	{
		ASSERT_EXIT (nano::inactive_node node (nano::unique_path (), nano::inactive_node_flag_defaults ()), ::testing::ExitedWithCode (1), "");
	}
}

TEST (node, password_fanout)
{
	nano::test::system system;
	nano::node_config config;
	config.peering_port = system.get_available_port ();
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	config.password_fanout = 10;
	auto & node = *system.add_node (config);
	auto wallet (node.wallets.create (100));
	ASSERT_EQ (10, wallet->store.password.values.size ());
}

TEST (node, balance)
{
	nano::test::system system (1);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto transaction = system.nodes[0]->ledger.tx_begin_write ();
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max (), system.nodes[0]->ledger.any.account_balance (transaction, nano::dev::genesis_key.pub));
}

TEST (node, send_unkeyed)
{
	nano::test::system system (1);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->store.password.value_set (nano::keypair ().prv);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
}

TEST (node, send_self)
{
	nano::test::system system (1);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (nano::dev::genesis_key.pub));
}

TEST (node, send_single)
{
	nano::test::system system (2);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (nano::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
}

TEST (node, send_single_observing_peer)
{
	nano::test::system system (3);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (nano::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<nano::node> const & node_a) { return !node_a->balance (key2.pub).is_zero (); }));
}

TEST (node, send_out_of_order)
{
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	nano::keypair key2;
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - 2 * node1.config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .previous (send2->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - 3 * node1.config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	node1.process_active (send3);
	node1.process_active (send2);
	node1.process_active (send1);
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<nano::node> const & node_a) { return node_a->balance (nano::dev::genesis_key.pub) == nano::dev::constants.genesis_amount - node1.config.receive_minimum.number () * 3; }));
}

TEST (node, quick_confirm)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::keypair key;
	nano::block_hash previous (node1.latest (nano::dev::genesis_key.pub));
	auto genesis_start_balance (node1.balance (nano::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto send = nano::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (node1.online_reps.delta () + 1)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, !node1.balance (key.pub).is_zero ());
	ASSERT_EQ (node1.balance (nano::dev::genesis_key.pub), node1.online_reps.delta () + 1);
	ASSERT_EQ (node1.balance (key.pub), genesis_start_balance - (node1.online_reps.delta () + 1));
}

TEST (node, node_receive_quorum)
{
	nano::test::system system (1);
	auto & node1 = *system.nodes[0];
	nano::keypair key;
	nano::block_hash previous (node1.latest (nano::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	auto send = nano::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, node1.block_or_pruned_exists (send->hash ()));
	ASSERT_TIMELY (10s, node1.active.election (nano::qualified_root (previous, previous)) != nullptr);
	auto election (node1.active.election (nano::qualified_root (previous, previous)));
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());

	nano::test::system system2;
	system2.add_node ();

	system2.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
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
	nano::test::system system;
	nano::node_config config (system.get_available_port ());
	config.backlog_population.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto send1 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_TIMELY_EQ (10s, node0->balance (key2.pub), node0->config.receive_minimum.number ());
	auto node1 (std::make_shared<nano::node> (system.io_ctx, system.get_available_port (), nano::unique_path (), system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, nano::test::establish_tcp (system, *node1, node0->network.endpoint ()));
	ASSERT_TIMELY_EQ (10s, node1->balance (key2.pub), node0->config.receive_minimum.number ());
	ASSERT_TIMELY (10s, !node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->block_or_pruned_exists (send1->hash ()));
	// Wait block receive
	ASSERT_TIMELY_EQ (5s, node1->ledger.block_count (), 3);
	// Confirmation for all blocks
	ASSERT_TIMELY_EQ (5s, node1->ledger.cemented_count (), 3);
}

TEST (node, auto_bootstrap_reverse)
{
	nano::test::system system;
	nano::node_config config (system.get_available_port ());
	config.backlog_population.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node1 (std::make_shared<nano::node> (system.io_ctx, system.get_available_port (), nano::unique_path (), system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, nano::test::establish_tcp (system, *node0, node1->network.endpoint ()));
	ASSERT_TIMELY_EQ (10s, node1->balance (key2.pub), node0->config.receive_minimum.number ());
}

TEST (node, auto_bootstrap_age)
{
	nano::test::system system;
	nano::node_config config (system.get_available_port ());
	config.backlog_population.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.bootstrap_interval = 1;
	auto node0 = system.add_node (config, node_flags);
	auto node1 (std::make_shared<nano::node> (system.io_ctx, system.get_available_port (), nano::unique_path (), system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, nano::test::establish_tcp (system, *node1, node0->network.endpoint ()));
	// 4 bootstraps with frontiers age
	ASSERT_TIMELY (10s, node0->stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate_legacy_age, nano::stat::dir::out) >= 3);
	// More attempts with frontiers age
	ASSERT_GE (node0->stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate_legacy_age, nano::stat::dir::out), node0->stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate, nano::stat::dir::out));
}

TEST (node, merge_peers)
{
	nano::test::system system (1);
	std::array<nano::endpoint, 8> endpoints;
	endpoints.fill (nano::endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ()));
	endpoints[0] = nano::endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	system.nodes[0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes[0]->network.size ());
}

TEST (node, search_receivable)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, search_receivable_same)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY_EQ (10s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_multiple)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	nano::keypair key2;
	nano::keypair key3;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key3.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key3.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !node->balance (key3.pub).is_zero ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (key3.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY_EQ (10s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_confirmed)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto node = system.add_node (node_config);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	auto send1 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_TIMELY (5s, nano::test::confirmed (*node, { send1 }));

	auto send2 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_TIMELY (5s, nano::test::confirmed (*node, { send2 }));

	{
		auto transaction (node->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, nano::dev::genesis_key.pub);
	}

	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable (system.wallet (0)->wallets.tx_begin_read ()));
	ASSERT_TIMELY (5s, !node->vote_router.active (send1->hash ()));
	ASSERT_TIMELY (5s, !node->vote_router.active (send2->hash ()));
	ASSERT_TIMELY_EQ (5s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_pruned)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto node1 = system.add_node (node_config);
	nano::node_flags node_flags;
	node_flags.enable_pruning = true;
	nano::node_config config (system.get_available_port ());
	config.enable_voting = false; // Remove after allowing pruned voting
	auto node2 = system.add_node (config, node_flags);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto send1 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	auto send2 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);

	// Confirmation
	ASSERT_TIMELY (10s, node1->active.empty () && node2->active.empty ());
	ASSERT_TIMELY (5s, node1->ledger.confirmed.block_exists_or_pruned (node1->ledger.tx_begin_read (), send2->hash ()));
	ASSERT_TIMELY_EQ (5s, node2->ledger.cemented_count (), 3);
	system.wallet (0)->store.erase (node1->wallets.tx_begin_write (), nano::dev::genesis_key.pub);

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
	nano::test::system system (1);
	auto node (system.nodes[0]);
	nano::keypair key2;
	nano::uint128_t balance (node->balance (nano::dev::genesis_key.pub));
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.rekey (transaction, "");
	}
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node->balance (nano::dev::genesis_key.pub) != balance);
	ASSERT_TIMELY (10s, node->active.empty ());
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		nano::lock_guard<std::recursive_mutex> lock{ system.wallet (0)->store.mutex };
		system.wallet (0)->store.password.value_set (nano::keypair ().prv);
	}
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		ASSERT_FALSE (system.wallet (0)->enter_password (transaction, ""));
	}
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, working)
{
	auto path (nano::working_path ());
	ASSERT_FALSE (path.empty ());
}

TEST (node, confirm_locked)
{
	nano::test::system system (1);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto transaction (system.wallet (0)->wallets.tx_begin_read ());
	system.wallet (0)->enter_password (transaction, "1");
	auto block = nano::send_block_builder ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (nano::keypair ().prv, 0)
				 .work (0)
				 .build ();
	system.nodes[0]->network.flood_block (block);
}

TEST (node_config, random_rep)
{
	auto path (nano::unique_path ());
	nano::node_config config1 (100);
	auto rep (config1.random_representative ());
	ASSERT_NE (config1.preconfigured_representatives.end (), std::find (config1.preconfigured_representatives.begin (), config1.preconfigured_representatives.end (), rep));
}

TEST (node, expire)
{
	std::weak_ptr<nano::node> node0;
	{
		nano::test::system system (1);
		node0 = system.nodes[0];
		auto & node1 (*system.nodes[0]);
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	}
	ASSERT_TRUE (node0.expired ());
}

// This test is racy, there is no guarantee that the election won't be confirmed until all forks are fully processed
TEST (node, fork_publish)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	nano::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	node1.process_active (send1);
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	auto election (node1.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	// Wait until the genesis rep activated & makes vote
	ASSERT_TIMELY_EQ (1s, election->votes ().size (), 2);
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	auto votes1 (election->votes ());
	auto existing1 (votes1.find (nano::dev::genesis_key.pub));
	ASSERT_NE (votes1.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	auto winner (*election->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, winner.first);
}

// In test case there used to be a race condition, it was worked around in:.
// https://github.com/nanocurrency/nano-node/pull/4091
// The election and the processing of block send2 happen in parallel.
// Usually the election happens first and the send2 block is added to the election.
// However, if the send2 block is processed before the election is started then
// there is a race somewhere and the election might not notice the send2 block.
// The test case can be made to pass by ensuring the election is started before the send2 is processed.
// However, is this a problem with the test case or this is a problem with the node handling of forks?
TEST (node, fork_publish_inactive)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::keypair key1;
	nano::keypair key2;

	nano::send_block_builder builder;

	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (send1->block_work ())
				 .build ();

	node.process_active (send1);
	ASSERT_TIMELY (5s, node.block (send1->hash ()));

	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send1->qualified_root ()));

	ASSERT_EQ (nano::block_status::fork, node.process_local (send2).value ());

	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);

	auto find_block = [&election] (nano::block_hash hash_a) -> bool {
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
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	nano::keypair key1;
	nano::keypair key2;
	nano::send_block_builder builder;
	// send1 and send2 fork to different accounts
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);
	node2.process_active (builder.make_block ().from (*send1).build ());
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.active.size ());
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	// Fill node with forked blocks
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	node2.process_active (builder.make_block ().from (*send2).build ());
	ASSERT_TIMELY (5s, node2.active.active (*send2));
	auto election1 (node2.active.election (nano::qualified_root (nano::dev::genesis->hash (), nano::dev::genesis->hash ())));
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
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.ledger.any.block_exists (transaction0, send1->hash ()));
	ASSERT_TRUE (node2.ledger.any.block_exists (transaction1, send1->hash ()));
}

// This test is racy, there is no guarantee that the election won't be confirmed until all forks are fully processed
TEST (node, fork_flip)
{
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	nano::keypair key1;
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	nano::publish publish1{ nano::dev::network_params.network, send1 };
	nano::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	nano::publish publish2{ nano::dev::network_params.network, send2 };
	node1.inbound (publish1, nano::test::fake_channel (node1));
	node2.inbound (publish2, nano::test::fake_channel (node2));
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.active.size ());
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	// Fill nodes with forked blocks
	node1.inbound (publish2, nano::test::fake_channel (node1));
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	node2.inbound (publish1, nano::test::fake_channel (node2));
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	auto election1 (node2.active.election (nano::qualified_root (nano::dev::genesis->hash (), nano::dev::genesis->hash ())));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_NE (nullptr, node1.block (publish1.block->hash ()));
	ASSERT_NE (nullptr, node2.block (publish2.block->hash ()));
	ASSERT_TIMELY (10s, node2.block_or_pruned_exists (publish1.block->hash ()));
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_TRUE (node2.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_FALSE (node2.block_or_pruned_exists (publish2.block->hash ()));
}

// Test that more than one block can be rolled back
TEST (node, fork_multi_flip)
{
	auto type = nano::transport::transport_type::tcp;
	nano::test::system system;
	nano::node_flags node_flags;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto & node1 (*system.add_node (node_config, node_flags, type));
	node_config.peering_port = system.get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags, type));
	ASSERT_EQ (1, node1.network.size ());
	nano::keypair key1;
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	nano::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .previous (send2->hash ())
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	// Node2 has two blocks that will be rolled back by node1's vote
	ASSERT_EQ (nano::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send2));
	ASSERT_EQ (nano::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send3));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv); // Insert voting key in to node1

	auto election = nano::test::start_election (system, node2, send2->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_TIMELY (5s, election->contains (send1->hash ()));
	nano::test::confirm (node1.ledger, send1);
	ASSERT_TIMELY (5s, node2.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (nano::test::block_or_pruned_none_exists (node2, { send2, send3 }));
	auto winner = *election->tally ().begin ();
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, winner.first);
}

// Blocks that are no longer actively being voted on should be able to be evicted through bootstrapping.
// This could happen if a fork wasn't resolved before the process previously shut down
TEST (node, fork_bootstrap_flip)
{
	nano::test::system system;
	nano::node_config config0{ system.get_available_port () };
	config0.backlog_population.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto & node1 = *system.add_node (config0, node_flags);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::node_config config1 (system.get_available_port ());
	auto & node2 = *system.make_disconnected_node (config1, node_flags);
	nano::block_hash latest = node1.latest (nano::dev::genesis_key.pub);
	nano::keypair key1;
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (latest)
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();
	nano::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (latest)
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();
	// Insert but don't rebroadcast, simulating settled blocks
	ASSERT_EQ (nano::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	ASSERT_EQ (nano::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send2));
	nano::test::confirm (node1.ledger, send1);
	ASSERT_TIMELY (1s, node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), send1->hash ()));
	ASSERT_TIMELY (1s, node2.ledger.any.block_exists (node2.ledger.tx_begin_read (), send2->hash ()));

	// Additionally add new peer to confirm & replace bootstrap block
	node2.network.merge_peer (node1.network.endpoint ());

	ASSERT_TIMELY (10s, node2.ledger.any.block_exists (node2.ledger.tx_begin_read (), send1->hash ()));
}

TEST (node, fork_open)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	std::shared_ptr<nano::election> election;

	// create block send1, to send all the balance from genesis to key1
	// this is done to ensure that the open block(s) cannot be voted on and confirmed
	nano::keypair key1;
	auto send1 = nano::send_block_builder ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	nano::publish publish1{ nano::dev::network_params.network, send1 };
	auto channel1 = std::make_shared<nano::transport::fake::channel> (node);
	node.inbound (publish1, channel1);
	ASSERT_TIMELY (5s, (election = node.active.election (publish1.block->qualified_root ())) != nullptr);
	election->force_confirm ();
	ASSERT_TIMELY (5s, node.active.empty () && node.block_confirmed (publish1.block->hash ()));

	// register key for genesis account, not sure why we do this, it seems needless,
	// since the genesis account at this stage has zero voting weight
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// create the 1st open block to receive send1, which should be regarded as the winner just because it is first
	nano::open_block_builder builder;
	auto open1 = builder.make_block ()
				 .source (publish1.block->hash ())
				 .representative (1)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	nano::publish publish2{ nano::dev::network_params.network, open1 };
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
	nano::publish publish3{ nano::dev::network_params.network, open2 };
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
	nano::test::system system (1);
	auto & node1 = *system.nodes[0];

	std::shared_ptr<nano::election> election;
	nano::keypair key1;
	nano::keypair rep1;
	nano::keypair rep2;

	// send 1 raw from genesis to key1 on both node1 and node2
	auto send1 = nano::send_block_builder ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);

	// We should be keeping this block
	nano::open_block_builder builder;
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
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
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
	ASSERT_EQ (nano::dev::constants.genesis_amount - 1, winner.first);

	// check the correct blocks are in the ledgers
	auto transaction1 = node1.ledger.tx_begin_read ();
	auto transaction2 = node2.ledger.tx_begin_read ();
	ASSERT_TRUE (node1.ledger.any.block_exists (transaction1, open1->hash ()));
	ASSERT_TRUE (node2.ledger.any.block_exists (transaction2, open1->hash ()));
	ASSERT_FALSE (node2.ledger.any.block_exists (transaction2, open2->hash ()));
}

TEST (node, coherent_observer)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.observers.blocks.add ([&node1] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const &, nano::account const &, nano::uint128_t const &, bool, bool) {
		ASSERT_TRUE (node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), status_a.winner->hash ()));
	});
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key;
	system.wallet (0)->send_action (nano::dev::genesis_key.pub, key.pub, 1);
}

TEST (node, fork_no_vote_quorum)
{
	nano::test::system system (3);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto & node3 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto key4 (system.wallet (0)->deterministic_insert ());
	system.wallet (0)->send_action (nano::dev::genesis_key.pub, key4, nano::dev::constants.genesis_amount / 4);
	auto key1 (system.wallet (1)->deterministic_insert ());
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1);
	}
	auto block (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1, node1.config.receive_minimum.number ()));
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (30s, node3.balance (key1) == node1.config.receive_minimum.number () && node2.balance (key1) == node1.config.receive_minimum.number () && node1.balance (key1) == node1.config.receive_minimum.number ());
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node3.weight (key1));
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (block->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance ((nano::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2))
				 .link (key1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (block->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (send1));
	ASSERT_EQ (nano::block_status::progress, node2.process (send1));
	ASSERT_EQ (nano::block_status::progress, node3.process (send1));
	auto key2 (system.wallet (2)->deterministic_insert ());
	auto send2 = nano::send_block_builder ()
				 .previous (block->hash ())
				 .destination (key2)
				 .balance ((nano::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (block->hash ()))
				 .build ();
	nano::raw_key key3;
	auto transaction (system.wallet (1)->wallets.tx_begin_read ());
	ASSERT_FALSE (system.wallet (1)->store.fetch (transaction, key1, key3));
	auto vote = std::make_shared<nano::vote> (key1, key3, 0, 0, std::vector<nano::block_hash>{ send2->hash () });
	nano::confirm_ack confirm{ nano::dev::network_params.network, vote };
	std::vector<uint8_t> buffer;
	{
		nano::vectorstream stream (buffer);
		confirm.serialize (stream);
	}
	auto channel = node2.network.find_node_id (node3.node_id.pub);
	ASSERT_NE (nullptr, channel);
	channel->send_buffer (nano::shared_const_buffer (std::move (buffer)));
	ASSERT_TIMELY (10s, node3.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in) >= 3);
	ASSERT_EQ (node1.latest (nano::dev::genesis_key.pub), send1->hash ());
	ASSERT_EQ (node2.latest (nano::dev::genesis_key.pub), send1->hash ());
	ASSERT_EQ (node3.latest (nano::dev::genesis_key.pub), send1->hash ());
}

// Disabled because it sometimes takes way too long (but still eventually finishes)
TEST (node, DISABLED_fork_pre_confirm)
{
	nano::test::system system (3);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto & node2 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	system.wallet (1)->insert_adhoc (key1.prv);
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1.pub);
	}
	nano::keypair key2;
	system.wallet (2)->insert_adhoc (key2.prv);
	{
		auto transaction (system.wallet (2)->wallets.tx_begin_write ());
		system.wallet (2)->store.representative_set (transaction, key2.pub);
	}
	auto block0 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, nano::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block0);
	ASSERT_TIMELY (30s, node0.balance (key1.pub) != 0);
	auto block1 (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, nano::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block1);
	ASSERT_TIMELY (30s, node0.balance (key2.pub) != 0);
	nano::keypair key3;
	nano::keypair key4;
	nano::state_block_builder builder;
	auto block2 = builder.make_block ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (node0.latest (nano::dev::genesis_key.pub))
				  .representative (key3.pub)
				  .balance (node0.balance (nano::dev::genesis_key.pub))
				  .link (0)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	auto block3 = builder.make_block ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (node0.latest (nano::dev::genesis_key.pub))
				  .representative (key4.pub)
				  .balance (node0.balance (nano::dev::genesis_key.pub))
				  .link (0)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
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
		done |= node0.latest (nano::dev::genesis_key.pub) == block2->hash () && node1.latest (nano::dev::genesis_key.pub) == block2->hash () && node2.latest (nano::dev::genesis_key.pub) == block2->hash ();
		done |= node0.latest (nano::dev::genesis_key.pub) == block3->hash () && node1.latest (nano::dev::genesis_key.pub) == block3->hash () && node2.latest (nano::dev::genesis_key.pub) == block3->hash ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Sometimes hangs on the bootstrap_initiator.bootstrap call
TEST (node, DISABLED_fork_stale)
{
	nano::test::system system1 (1);
	system1.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::test::system system2 (1);
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint (), false);

	auto channel = nano::test::establish_tcp (system1, node2, node1.network.endpoint ());
	auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, 0, 0, std::vector<nano::block_hash> ());
	ASSERT_TRUE (node2.rep_crawler.process (vote, channel));
	nano::keypair key1;
	nano::keypair key2;
	nano::state_block_builder builder;
	auto send3 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::nano_ratio)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
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
				 .account (nano::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::nano_ratio)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::nano_ratio)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	{
		auto transaction1 = node1.ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node1.ledger.process (transaction1, send1));
		auto transaction2 = node2.ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node2.ledger.process (transaction2, send2));
	}
	node1.process_active (send1);
	node1.process_active (send2);
	node2.process_active (send1);
	node2.process_active (send2);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint (), false);
	while (node2.block (send1->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3512
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3516
TEST (node, DISABLED_broadcast_elected)
{
	auto type = nano::transport::transport_type::tcp;
	nano::node_flags node_flags;
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto node0 = system.add_node (node_config, node_flags, type);
	node_config.peering_port = system.get_available_port ();
	auto node1 = system.add_node (node_config, node_flags, type);
	node_config.peering_port = system.get_available_port ();
	auto node2 = system.add_node (node_config, node_flags, type);
	nano::keypair rep_big;
	nano::keypair rep_small;
	nano::keypair rep_other;
	nano::block_builder builder;
	{
		auto transaction0 = node0->ledger.tx_begin_write ();
		auto transaction1 = node1->ledger.tx_begin_write ();
		auto transaction2 = node2->ledger.tx_begin_write ();
		auto fund_big = builder.send ()
						.previous (nano::dev::genesis->hash ())
						.destination (rep_big.pub)
						.balance (nano::Knano_ratio * 5)
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (nano::dev::genesis->hash ()))
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
						  .balance (nano::Knano_ratio * 2)
						  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
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
						  .balance (nano::Knano_ratio)
						  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						  .work (*system.work.generate (fund_small->hash ()))
						  .build ();
		auto open_other = builder.open ()
						  .source (fund_other->hash ())
						  .representative (rep_other.pub)
						  .account (rep_other.pub)
						  .sign (rep_other.prv, rep_other.pub)
						  .work (*system.work.generate (rep_other.pub))
						  .build ();
		ASSERT_EQ (nano::block_status::progress, node0->ledger.process (transaction0, fund_big));
		ASSERT_EQ (nano::block_status::progress, node1->ledger.process (transaction1, fund_big));
		ASSERT_EQ (nano::block_status::progress, node2->ledger.process (transaction2, fund_big));
		ASSERT_EQ (nano::block_status::progress, node0->ledger.process (transaction0, open_big));
		ASSERT_EQ (nano::block_status::progress, node1->ledger.process (transaction1, open_big));
		ASSERT_EQ (nano::block_status::progress, node2->ledger.process (transaction2, open_big));
		ASSERT_EQ (nano::block_status::progress, node0->ledger.process (transaction0, fund_small));
		ASSERT_EQ (nano::block_status::progress, node1->ledger.process (transaction1, fund_small));
		ASSERT_EQ (nano::block_status::progress, node2->ledger.process (transaction2, fund_small));
		ASSERT_EQ (nano::block_status::progress, node0->ledger.process (transaction0, open_small));
		ASSERT_EQ (nano::block_status::progress, node1->ledger.process (transaction1, open_small));
		ASSERT_EQ (nano::block_status::progress, node2->ledger.process (transaction2, open_small));
		ASSERT_EQ (nano::block_status::progress, node0->ledger.process (transaction0, fund_other));
		ASSERT_EQ (nano::block_status::progress, node1->ledger.process (transaction1, fund_other));
		ASSERT_EQ (nano::block_status::progress, node2->ledger.process (transaction2, fund_other));
		ASSERT_EQ (nano::block_status::progress, node0->ledger.process (transaction0, open_other));
		ASSERT_EQ (nano::block_status::progress, node1->ledger.process (transaction1, open_other));
		ASSERT_EQ (nano::block_status::progress, node2->ledger.process (transaction2, open_other));
	}
	// Confirm blocks to allow voting
	for (auto & node : system.nodes)
	{
		auto block (node->block (node->latest (nano::dev::genesis_key.pub)));
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
				 .previous (node2->latest (nano::dev::genesis_key.pub))
				 .destination (rep_small.pub)
				 .balance (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (node2->latest (nano::dev::genesis_key.pub)))
				 .build ();
	// A copy is necessary to avoid data races during ledger processing, which sets the sideband
	auto fork0_copy (std::make_shared<nano::send_block> (*fork0));
	node0->process_active (fork0);
	node1->process_active (fork0_copy);
	auto fork1 = builder.send ()
				 .previous (node2->latest (nano::dev::genesis_key.pub))
				 .destination (rep_big.pub)
				 .balance (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (node2->latest (nano::dev::genesis_key.pub)))
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
	ASSERT_TIMELY (5s, node1->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out) != 0);
}

TEST (node, rep_self_vote)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.online_weight_minimum = std::numeric_limits<nano::uint128_t>::max ();
	node_config.backlog_population.enable = false;
	auto node0 = system.add_node (node_config);
	nano::keypair rep_big;
	nano::block_builder builder;
	auto fund_big = builder.send ()
					.previous (nano::dev::genesis->hash ())
					.destination (rep_big.pub)
					.balance (nano::uint128_t{ "0xb0000000000000000000000000000000" })
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (nano::dev::genesis->hash ()))
					.build ();
	auto open_big = builder.open ()
					.source (fund_big->hash ())
					.representative (rep_big.pub)
					.account (rep_big.pub)
					.sign (rep_big.prv, rep_big.pub)
					.work (*system.work.generate (rep_big.pub))
					.build ();
	ASSERT_EQ (nano::block_status::progress, node0->process (fund_big));
	ASSERT_EQ (nano::block_status::progress, node0->process (open_big));
	// Confirm both blocks, allowing voting on the upcoming block
	node0->start_election (node0->block (open_big->hash ()));
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node0->active.election (open_big->qualified_root ()));
	election->force_confirm ();

	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_EQ (system.wallet (0)->wallets.reps ().voting, 2);
	auto block0 = builder.send ()
				  .previous (fund_big->hash ())
				  .destination (rep_big.pub)
				  .balance (nano::uint128_t ("0x60000000000000000000000000000000"))
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*system.work.generate (fund_big->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node0->process (block0));
	auto & active = node0->active;
	auto & scheduler = node0->scheduler;
	auto election1 = nano::test::start_election (system, *node0, block0->hash ());
	ASSERT_NE (nullptr, election1);
	// Wait until representatives are activated & make vote
	ASSERT_TIMELY_EQ (1s, election1->votes ().size (), 3);
	auto rep_votes (election1->votes ());
	ASSERT_NE (rep_votes.end (), rep_votes.find (nano::dev::genesis_key.pub));
	ASSERT_NE (rep_votes.end (), rep_votes.find (rep_big.pub));
}

// Bootstrapping shouldn't republish the blocks to the network.
TEST (node, DISABLED_bootstrap_no_publish)
{
	nano::test::system system0 (1);
	nano::test::system system1 (1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	nano::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	nano::block_builder builder;
	auto send0 = builder
				 .send ()
				 .previous (node0->latest (nano::dev::genesis_key.pub))
				 .destination (key0.pub)
				 .balance (500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	{
		auto transaction = node0->ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node0->ledger.process (transaction, send0));
	}
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint (), false);
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

// Check that an outgoing bootstrap request can push blocks
// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3512
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3515
TEST (node, DISABLED_bootstrap_bulk_push)
{
	nano::test::system system;
	nano::test::system system0;
	nano::test::system system1;
	nano::node_config config0 (system.get_available_port ());
	config0.backlog_population.enable = false;
	auto node0 (system0.add_node (config0));
	nano::node_config config1 (system.get_available_port ());
	config1.backlog_population.enable = false;
	auto node1 (system1.add_node (config1));
	nano::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	auto send0 = nano::send_block_builder ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node0->process (send0));

	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());
	node0->bootstrap_initiator.bootstrap (node1->network.endpoint (), false);
	system1.deadline_set (10s);
	while (node1->block (send0->hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// since this uses bulk_push, the new block should be republished
	system1.deadline_set (10s);
	while (node1->active.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
}

// Bootstrapping a forked open block should succeed.
TEST (node, bootstrap_fork_open)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	auto node0 = system.add_node (node_config);
	node_config.peering_port = system.get_available_port ();
	auto node1 = system.add_node (node_config);
	nano::keypair key0;
	nano::block_builder builder;
	auto send0 = builder.send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (nano::dev::constants.genesis_amount - 500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
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
	ASSERT_EQ (nano::block_status::progress, node0->process (send0));
	ASSERT_EQ (nano::block_status::progress, node1->process (send0));
	// Confirm send0 to allow starting and voting on the following blocks
	for (auto node : system.nodes)
	{
		node->start_election (node->block (node->latest (nano::dev::genesis_key.pub)));
		ASSERT_TIMELY (1s, node->active.election (send0->qualified_root ()));
		auto election = node->active.election (send0->qualified_root ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY (2s, node->active.empty ());
	}
	ASSERT_TIMELY (3s, node0->block_confirmed (send0->hash ()));
	// They disagree about open0/open1
	ASSERT_EQ (nano::block_status::progress, node0->process (open0));
	ASSERT_EQ (nano::block_status::progress, node1->process (open1));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_FALSE (node1->block_or_pruned_exists (open0->hash ()));
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint (), false);
	ASSERT_TIMELY (1s, node1->active.empty ());
	ASSERT_TIMELY (10s, !node1->block_or_pruned_exists (open1->hash ()) && node1->block_or_pruned_exists (open0->hash ()));
}

// Unconfirmed blocks from bootstrap should be confirmed
TEST (node, bootstrap_confirm_frontiers)
{
	// create 2 separate systems, the 2 system do not interact with each other automatically
	nano::test::system system0 (1);
	nano::test::system system1 (1);
	auto node0 = system0.nodes[0];
	auto node1 = system1.nodes[0];
	system0.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key0;

	// create block to send 500 raw from genesis to key0 and save into node0 ledger without immediately triggering an election
	auto send0 = nano::send_block_builder ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (nano::dev::constants.genesis_amount - 500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node0->process (send0));

	// each system only has one node, so there should be no bootstrapping going on
	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());

	// create a bootstrap connection from node1 to node0
	// this also has the side effect of adding node0 to node1's list of peers, which will trigger realtime connections too
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());

	// Wait until the block is confirmed on node1. Poll more than usual because we are polling
	// on 2 different systems at once and in sequence and there might be strange timing effects.
	system0.deadline_set (10s);
	system1.deadline_set (10s);
	while (!node1->ledger.confirmed.block_exists_or_pruned (node1->ledger.tx_begin_read (), send0->hash ()))
	{
		ASSERT_NO_ERROR (system0.poll (std::chrono::milliseconds (1)));
		ASSERT_NO_ERROR (system1.poll (std::chrono::milliseconds (1)));
	}
}

// Test that if we create a block that isn't confirmed, the bootstrapping processes sync the missing block.
TEST (node, unconfirmed_send)
{
	nano::test::system system{};

	auto & node1 = *system.add_node ();
	auto wallet1 = system.wallet (0);
	wallet1->insert_adhoc (nano::dev::genesis_key.prv);

	nano::keypair key2{};
	auto & node2 = *system.add_node ();
	auto wallet2 = system.wallet (1);
	wallet2->insert_adhoc (key2.prv);

	// firstly, send two units from node1 to node2 and expect that both nodes see the block as confirmed
	// (node1 will start an election for it, vote on it and node2 gets synced up)
	auto send1 = wallet1->send_action (nano::dev::genesis_key.pub, key2.pub, 2 * nano::nano_ratio);
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send1->hash ()));

	// wait until receive1 (auto-receive created by wallet) is cemented
	ASSERT_TIMELY_EQ (5s, node2.ledger.confirmed.account_height (node2.ledger.tx_begin_read (), key2.pub), 1);
	ASSERT_EQ (node2.balance (key2.pub), 2 * nano::nano_ratio);
	auto recv1 = node2.ledger.find_receive_block_by_send_hash (node2.ledger.tx_begin_read (), key2.pub, send1->hash ());

	// create send2 to send from node2 to node1 and save it to node2's ledger without triggering an election (node1 does not hear about it)
	auto send2 = nano::state_block_builder{}
				 .make_block ()
				 .account (key2.pub)
				 .previous (recv1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::nano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (recv1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node2.process (send2));

	auto send3 = wallet2->send_action (key2.pub, nano::dev::genesis_key.pub, nano::nano_ratio);
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send3->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send3->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.ledger.cemented_count (), 7);
	ASSERT_TIMELY_EQ (5s, node1.balance (nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount);
}

// Test that nodes can disable representative voting
TEST (node, no_voting)
{
	nano::test::system system (1);
	auto & node0 (*system.nodes[0]);
	nano::node_config node_config (system.get_available_port ());
	node_config.enable_voting = false;
	system.add_node (node_config);

	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node1 has a rep
	wallet1->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	// Broadcast a confirm so others should know this is a rep node
	wallet1->send_action (nano::dev::genesis_key.pub, key1.pub, nano::nano_ratio);
	ASSERT_TIMELY (10s, node0.active.empty ());
	ASSERT_EQ (0, node0.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
}

TEST (node, send_callback)
{
	nano::test::system system (1);
	auto & node0 (*system.nodes[0]);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	node0.config.callback_address = "localhost";
	node0.config.callback_port = 8010;
	node0.config.callback_target = "/";
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, node0.config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node0.balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - node0.config.receive_minimum.number (), node0.balance (nano::dev::genesis_key.pub));
}

TEST (node, balance_observer)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	std::atomic<int> balances (0);
	nano::keypair key;
	node1.observers.account_balance.add ([&key, &balances] (nano::account const & account_a, bool is_pending) {
		if (key.pub == account_a && is_pending)
		{
			balances++;
		}
		else if (nano::dev::genesis_key.pub == account_a && !is_pending)
		{
			balances++;
		}
	});
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->send_action (nano::dev::genesis_key.pub, key.pub, 1);
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto ec = system.poll ();
		done = balances.load () == 2;
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, bootstrap_connection_scaling)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (34, node1.bootstrap_initiator.connections->target_connections (5000, 1));
	ASSERT_EQ (4, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 1));
	ASSERT_EQ (32, node1.bootstrap_initiator.connections->target_connections (5000, 0));
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (0, 0));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 0));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 0));
	ASSERT_EQ (36, node1.bootstrap_initiator.connections->target_connections (5000, 2));
	ASSERT_EQ (8, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 2));
	node1.config.bootstrap_connections = 128;
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	node1.config.bootstrap_connections_max = 256;
	ASSERT_EQ (128, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	node1.config.bootstrap_connections_max = 0;
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (50000, 1));
}

TEST (node, online_reps)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	// 1 sample of minimum weight
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	auto vote (std::make_shared<nano::vote> ());
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.online_reps.observe (nano::dev::genesis_key.pub);
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.online ());
	// 1 minimum, 1 maximum
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	node1.online_reps.sample ();
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.trended ());
	node1.online_reps.clear ();
	// 2 minimum, 1 maximum
	node1.online_reps.sample ();
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
}

TEST (node, online_reps_rep_crawler)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::milliseconds_since_epoch (), 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	// Without rep crawler
	node1.vote_processor.vote_blocking (vote, std::make_shared<nano::transport::fake::channel> (node1));
	ASSERT_EQ (0, node1.online_reps.online ());
	// After inserting to rep crawler
	auto channel = std::make_shared<nano::transport::fake::channel> (node1);
	node1.rep_crawler.force_query (nano::dev::genesis->hash (), channel);
	node1.vote_processor.vote_blocking (vote, channel);
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.online ());
}

TEST (node, online_reps_election)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	// Start election
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	// Process vote for ongoing election
	auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::milliseconds_since_epoch (), 0, std::vector<nano::block_hash>{ send1->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.vote_processor.vote_blocking (vote, std::make_shared<nano::transport::fake::channel> (node1));
	ASSERT_EQ (nano::dev::constants.genesis_amount - nano::Knano_ratio, node1.online_reps.online ());
}

TEST (node, block_confirm)
{
	auto type = nano::transport::transport_type::tcp;
	nano::node_flags node_flags;
	nano::test::system system (2, type, node_flags);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (nano::dev::genesis->hash ()))
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
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node2.active.election (send1_copy->qualified_root ()));
	// Make node2 genesis representative so it can vote
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (10s, node1.active.recently_cemented.list ().size (), 1);
}

TEST (node, confirm_quorum)
{
	nano::test::system system (1);
	auto & node1 = *system.nodes[0];
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	// Put greater than node.delta () in pending so quorum can't be reached
	nano::amount new_balance = node1.online_reps.delta () - nano::Knano_ratio;
	auto send1 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (new_balance)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (send1));
	system.wallet (0)->send_action (nano::dev::genesis_key.pub, nano::dev::genesis_key.pub, new_balance.number ());
	ASSERT_TIMELY (2s, node1.active.election (send1->qualified_root ()));
	auto election = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());
	ASSERT_EQ (0, node1.balance (nano::dev::genesis_key.pub));
}

// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	node_config.receive_minimum = nano::dev::constants.genesis_amount;
	auto & node (*system.add_node (node_config));
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 3 * nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send2->hash ()))
				 .build ();
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node.ledger.process (transaction, send1));
		ASSERT_EQ (nano::block_status::progress, node.ledger.process (transaction, send2));
	}
	// Confirm blocks to allow voting
	node.start_election (send2);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()));
	election->force_confirm ();
	ASSERT_TIMELY_EQ (3s, node.ledger.cemented_count (), 3);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::confirm_req message1{ nano::dev::network_params.network, send1->hash (), send1->root () };
	nano::confirm_req message2{ nano::dev::network_params.network, send2->hash (), send2->root () };
	auto channel = std::make_shared<nano::transport::fake::channel> (node);
	node.inbound (message1, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes), 1);
	node.inbound (message2, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes), 2);
	for (auto i (0); i < 100; ++i)
	{
		node.inbound (message1, channel);
		node.inbound (message2, channel);
	}
	// Make sure a new vote was not generated
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes), 2);
	// Max cache
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node.ledger.process (transaction, send3));
	}
	nano::confirm_req message3{ nano::dev::network_params.network, send3->hash (), send3->root () };
	node.inbound (message3, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes), 3);
	ASSERT_TIMELY (3s, !node.history.votes (send1->root (), send1->hash ()).empty ());
	ASSERT_TIMELY (3s, !node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_TIMELY (3s, !node.history.votes (send3->root (), send3->hash ()).empty ());
	// All requests should be served from the cache
	for (auto i (0); i < 100; ++i)
	{
		node.inbound (message3, channel);
	}
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes), 3);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3532
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3481
// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_batch)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto & node (*system.add_node (node_config));
	ASSERT_GE (node.network_params.voting.max_cache, 2);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	auto send1 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	node.confirming_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node.ledger.confirmed.block_exists_or_pruned (node.ledger.tx_begin_read (), send1->hash ()));
	auto send2 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	auto receive1 = nano::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::Knano_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	std::vector<std::pair<nano::block_hash, nano::root>> batch{ { send2->hash (), send2->root () }, { receive1->hash (), receive1->root () } };
	nano::confirm_req message{ nano::dev::network_params.network, batch };
	auto channel = std::make_shared<nano::transport::fake::channel> (node);
	// Generates and sends one vote for both hashes which is then cached
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out), 1);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	ASSERT_FALSE (node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (receive1->root (), receive1->hash ()).empty ());
	// Only one confirm_ack should be sent if all hashes are part of the same vote
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out), 2);
	ASSERT_EQ (2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	// Test when votes are different
	node.history.erase (send2->root ());
	node.history.erase (receive1->root ());
	node.inbound (nano::confirm_req{ nano::dev::network_params.network, send2->hash (), send2->root () }, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out), 3);
	ASSERT_EQ (3, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	node.inbound (nano::confirm_req{ nano::dev::network_params.network, receive1->hash (), receive1->root () }, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out), 4);
	ASSERT_EQ (4, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	// There are two different votes, so both should be sent in response
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out), 6);
	ASSERT_EQ (6, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

/**
 * There is a cache for locally generated votes. This test checks that the node
 * properly caches and uses those votes when replying to confirm_req requests.
 */
// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_generate_new_vote)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Send a confirm req for genesis block to node
	nano::confirm_req message1{ nano::dev::network_params.network, nano::dev::genesis->hash (), nano::dev::genesis->root () };
	auto channel = std::make_shared<nano::transport::fake::channel> (node);
	node.inbound (message1, channel);

	// check that the node generated a vote for the genesis block and that it is stored in the local vote cache and it is the only vote
	ASSERT_TIMELY (5s, !node.history.votes (nano::dev::genesis->root (), nano::dev::genesis->hash ()).empty ());
	auto votes1 = node.history.votes (nano::dev::genesis->root (), nano::dev::genesis->hash ());
	ASSERT_EQ (1, votes1.size ());
	ASSERT_EQ (1, votes1[0]->hashes.size ());
	ASSERT_EQ (nano::dev::genesis->hash (), votes1[0]->hashes[0]);
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes), 1);

	auto send1 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send1));
	// One of the hashes is cached
	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes{ std::make_pair (nano::dev::genesis->hash (), nano::dev::genesis->root ()), std::make_pair (send1->hash (), send1->root ()) };
	nano::confirm_req message2{ nano::dev::network_params.network, roots_hashes };
	node.inbound (message2, channel);
	ASSERT_TIMELY (3s, !node.history.votes (send1->root (), send1->hash ()).empty ());
	auto votes2 (node.history.votes (send1->root (), send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->hashes.size ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes), 2);
	ASSERT_FALSE (node.history.votes (nano::dev::genesis->root (), nano::dev::genesis->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (send1->root (), send1->hash ()).empty ());
	// First generated + again cached + new generated
	ASSERT_TIMELY_EQ (3s, 3, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_fork)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_bootstrap_bulk_pull_server = true;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto & node1 (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto send1 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	auto send1_fork = nano::state_block_builder ()
					  .account (nano::dev::genesis_key.pub)
					  .previous (nano::dev::genesis->hash ())
					  .representative (nano::dev::genesis_key.pub)
					  .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
					  .link (nano::dev::genesis_key.pub)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*node1.work_generate_blocking (nano::dev::genesis->hash ()))
					  .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (send1));
	// Cache vote
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { send1 }, 0, 0);
	node1.vote_processor.vote (vote, std::make_shared<nano::transport::fake::channel> (node1));
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
	nano::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	nano::keypair key2;
	// by not setting a private key on node1's wallet for genesis account, it is stopped from voting
	system.wallet (1)->insert_adhoc (key2.prv);

	// send1 and send2 are forks of each other
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
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
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { send2 });
	node1.vote_processor.vote (vote, std::make_shared<nano::transport::fake::channel> (node1));

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
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::state_block_builder builder;
	std::vector<std::shared_ptr<nano::state_block>> blocks;
	auto block = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	blocks.push_back (block);
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), blocks.back ()));
	for (auto i = 2; i < 200; ++i)
	{
		auto block = builder.make_block ()
					 .from (*blocks.back ())
					 .previous (blocks.back ()->hash ())
					 .balance (nano::dev::constants.genesis_amount - i)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (blocks.back ()->hash ()))
					 .build ();
		blocks.push_back (block);
		ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), blocks.back ()));
	}

	// Confirming last block will confirm whole chain and allow us to generate votes for those blocks later
	nano::test::confirm (node.ledger, blocks.back ());

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	system.wallet (0)->insert_adhoc (key1.prv);

	system.nodes[0]->observers.vote.add ([&max_hashes] (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const &, nano::vote_source, nano::vote_code) {
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
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	nano::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);

	// send1 and send2 are forks of each other
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
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
	std::vector<nano::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { vote_blocks });
	node1.vote_processor.vote (vote, std::make_shared<nano::transport::fake::channel> (node1));

	// send2 should win on both nodes
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.balance (key2.pub), node1.config.receive_minimum.number () * 2);
	ASSERT_TIMELY_EQ (5s, node1.balance (key2.pub), node1.config.receive_minimum.number () * 2);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3629
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3638
TEST (node, DISABLED_vote_by_hash_epoch_block_republish)
{
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	nano::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	auto send1 = nano::send_block_builder ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto epoch1 = nano::state_block_builder ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount)
				  .link (node1.ledger.epoch_link (nano::epoch::epoch_1))
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*system.work.generate (nano::dev::genesis->hash ()))
				  .build ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	node1.active.publish (epoch1);
	std::vector<nano::block_hash> vote_blocks;
	vote_blocks.push_back (epoch1->hash ());
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { vote_blocks }, 0, 0);
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TRUE (node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<nano::transport::fake::channel> (node1));
	ASSERT_TIMELY (10s, node1.block (epoch1->hash ()));
	ASSERT_TIMELY (10s, node2.block (epoch1->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
}

TEST (node, epoch_conflict_confirm)
{
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto & node0 = *system.add_node (node_config);
	node_config.peering_port = system.get_available_port ();
	auto & node1 = *system.add_node (node_config);
	nano::keypair key;
	nano::keypair epoch_signer (nano::dev::genesis_key);
	nano::state_block_builder builder;
	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
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
				 .account (nano::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .link (open->hash ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto epoch_open = builder.make_block ()
					  .account (change->root ().as_account ())
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node0.ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (epoch_signer.prv, epoch_signer.pub)
					  .work (*system.work.generate (open->hash ()))
					  .build ();

	// Process initial blocks on node1
	ASSERT_TRUE (nano::test::process (node1, { send, send2, open }));

	// Confirm open block in node1 to allow generating votes
	nano::test::confirm (node1.ledger, open);

	// Process initial blocks on node0
	ASSERT_TRUE (nano::test::process (node0, { send, send2, open }));

	// Process conflicting blocks on node 0 as blocks coming from live network
	ASSERT_TRUE (nano::test::process_live (node0, { change, epoch_open }));

	// Ensure blocks were propagated to both nodes
	ASSERT_TIMELY (5s, nano::test::exists (node0, { change, epoch_open }));
	ASSERT_TIMELY (5s, nano::test::exists (node1, { change, epoch_open }));

	// Confirm initial blocks in node1 to allow generating votes later
	ASSERT_TRUE (nano::test::start_elections (system, node1, { change, epoch_open, send2 }, true));
	ASSERT_TIMELY (5s, nano::test::confirmed (node1, { change, epoch_open, send2 }));

	// Start elections for node0 for conflicting change and epoch_open blocks (those two blocks have the same root)
	ASSERT_TRUE (nano::test::activate (node0, { change, epoch_open }));
	ASSERT_TIMELY (5s, nano::test::active (node0, { change, epoch_open }));

	// Make node1 a representative
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);

	// Ensure the elections for conflicting blocks have completed
	ASSERT_TIMELY (5s, nano::test::active (node0, { change, epoch_open }));

	// Ensure both conflicting blocks were successfully processed and confirmed
	ASSERT_TIMELY (5s, nano::test::confirmed (node0, { change, epoch_open }));
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3526
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3527
TEST (node, DISABLED_fork_invalid_block_signature)
{
	nano::test::system system;
	nano::node_flags node_flags;
	// Disabling republishing + waiting for a rollback before sending the correct vote below fixes an intermittent failure in this test
	// If these are taken out, one of two things may cause the test two fail often:
	// - Block *send2* might get processed before the rollback happens, simply due to timings, with code "fork", and not be processed again. Waiting for the rollback fixes this issue.
	// - Block *send1* might get processed again after the rollback happens, which causes *send2* to be processed with code "fork". Disabling block republishing ensures "send1" is not processed again.
	// An alternative would be to repeatedly flood the correct vote
	node_flags.disable_block_processor_republishing = true;
	auto & node1 (*system.add_node (node_flags));
	auto & node2 (*system.add_node (node_flags));
	nano::keypair key2;
	nano::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2_corrupt (std::make_shared<nano::send_block> (*send2));
	send2_corrupt->signature = nano::signature (123);
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { send2 }, 0, 0);
	auto vote_corrupt = nano::test::make_vote (nano::dev::genesis_key, { send2_corrupt }, 0, 0);

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()));
	// Send the vote with the corrupt block signature
	node2.network.flood_vote (vote_corrupt, 1.0f);
	// Wait for the rollback
	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::rollback));
	// Send the vote with the correct block
	node2.network.flood_vote (vote, 1.0f);
	ASSERT_TIMELY (10s, !node1.block (send1->hash ()));
	ASSERT_TIMELY (10s, node1.block (send2->hash ()));
	ASSERT_EQ (node1.block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, fork_election_invalid_block_signature)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::block_builder builder;
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build ();
	auto send3 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .sign (nano::dev::genesis_key.prv, 0) // Invalid signature
				 .build ();

	auto channel1 = std::make_shared<nano::transport::fake::channel> (node1);
	node1.inbound (nano::publish{ nano::dev::network_params.network, send1 }, channel1);
	ASSERT_TIMELY (5s, node1.active.active (send1->qualified_root ()));
	auto election (node1.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());
	node1.inbound (nano::publish{ nano::dev::network_params.network, send3 }, channel1);
	node1.inbound (nano::publish{ nano::dev::network_params.network, send2 }, channel1);
	ASSERT_TIMELY (3s, election->blocks ().size () > 1);
	ASSERT_EQ (election->blocks ()[send2->hash ()]->block_signature (), send2->block_signature ());
}

TEST (node, block_processor_signatures)
{
	nano::test::system system{ 1 };
	auto & node1 = *system.nodes[0];
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_hash latest = system.nodes[0]->latest (nano::dev::genesis_key.pub);
	nano::state_block_builder builder;
	nano::keypair key1;
	nano::keypair key2;
	nano::keypair key3;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (latest)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (latest))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 3 * nano::Knano_ratio)
				 .link (key3.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send2->hash ()))
				 .build ();
	// Invalid signature bit
	auto send4 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 4 * nano::Knano_ratio)
				 .link (key3.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send3->hash ()))
				 .build ();
	send4->signature.bytes[32] ^= 0x1;
	// Invalid signature bit (force)
	auto send5 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 5 * nano::Knano_ratio)
				 .link (key3.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send3->hash ()))
				 .build ();
	send5->signature.bytes[32] ^= 0x1;
	// Invalid signature to unchecked
	node1.unchecked.put (send5->previous (), nano::unchecked_info{ send5 });
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::Knano_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node1.work_generate_blocking (key1.pub))
					.build ();
	auto receive2 = builder.make_block ()
					.account (key2.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::Knano_ratio)
					.link (send2->hash ())
					.sign (key2.prv, key2.pub)
					.work (*node1.work_generate_blocking (key2.pub))
					.build ();
	// Invalid private key
	auto receive3 = builder.make_block ()
					.account (key3.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::Knano_ratio)
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
	nano::test::system system (1);
	auto & node (*system.nodes[0]);
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	send1->signature.bytes[0] ^= 1;
	ASSERT_FALSE (node.block_or_pruned_exists (send1->hash ()));
	node.process_active (send1);
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::blockprocessor_result, nano::stat::detail::bad_signature));
	ASSERT_FALSE (node.block_or_pruned_exists (send1->hash ()));
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	node.process_active (send2);
	ASSERT_TIMELY (5s, node.block_or_pruned_exists (send2->hash ()));
}

TEST (node, confirm_back)
{
	nano::test::system system (1);
	nano::keypair key;
	auto & node (*system.nodes[0]);
	auto genesis_start_balance (node.balance (nano::dev::genesis_key.pub));
	auto send1 = nano::send_block_builder ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key.pub)
				 .balance (genesis_start_balance - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	nano::state_block_builder builder;
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
				 .link (nano::dev::genesis_key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (open->hash ()))
				 .build ();
	node.process_active (send1);
	node.process_active (open);
	node.process_active (send2);
	ASSERT_TIMELY (5s, node.block (send2->hash ()) != nullptr);
	ASSERT_TRUE (nano::test::start_elections (system, node, { send1, open, send2 }));
	ASSERT_EQ (3, node.active.size ());
	std::vector<nano::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { vote_blocks });
	node.vote_processor.vote_blocking (vote, std::make_shared<nano::transport::fake::channel> (node));
	ASSERT_TIMELY (10s, node.active.empty ());
}

TEST (node, peers)
{
	nano::test::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());

	auto node2 (std::make_shared<nano::node> (system.io_ctx, system.get_available_port (), nano::unique_path (), system.work));
	system.nodes.push_back (node2);

	auto endpoint = node1->network.endpoint ();
	nano::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto & store = node2->store;
	{
		// Add a peer to the database
		auto transaction (store.tx_begin_write ());
		store.peer.put (transaction, endpoint_key, 37);

		// Add a peer which is not contactable
		store.peer.put (transaction, nano::endpoint_key{ boost::asio::ip::address_v6::any ().to_bytes (), 55555 }, 42);
	}

	node2->start ();
	ASSERT_TIMELY (10s, !node2->network.empty () && !node1->network.empty ())
	// Wait to finish TCP node ID handshakes
	ASSERT_TIMELY (10s, node1->tcp_listener.realtime_count () != 0 && node2->tcp_listener.realtime_count () != 0);
	// Confirm that the peers match with the endpoints we are expecting
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->get_node_id (), list1[0]->get_node_id ());
	ASSERT_EQ (nano::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->get_node_id (), list2[0]->get_node_id ());
	ASSERT_EQ (nano::transport::transport_type::tcp, list2[0]->get_type ());

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
	nano::test::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());
	auto endpoint = node1->network.endpoint ();
	nano::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto path (nano::unique_path ());
	{
		auto node2 (std::make_shared<nano::node> (system.io_ctx, system.get_available_port (), path, system.work));
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
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node2->network.size ());
		system.stop_node (*node2);
	}
	// Restart node
	{
		nano::node_flags node_flags;
		node_flags.read_only = true;
		auto node3 (std::make_shared<nano::node> (system.io_ctx, system.get_available_port (), path, system.work, node_flags));
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
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node3->network.size ());
		system.stop_node (*node3);
	}
}

/** This checks that a node can be opened (without being blocked) when a write lock is held elsewhere */
TEST (node, dont_write_lock_node)
{
	auto path = nano::unique_path ();

	std::promise<void> write_lock_held_promise;
	std::promise<void> finished_promise;
	std::thread ([&path, &write_lock_held_promise, &finished_promise] () {
		nano::logger logger;
		auto store = nano::make_store (logger, path, nano::dev::constants, false, true);
		{
			nano::ledger_cache ledger_cache{ store->rep_weight };
			auto transaction (store->tx_begin_write ());
			store->initialize (transaction, ledger_cache, nano::dev::constants);
		}

		// Hold write lock open until main thread is done needing it
		auto transaction (store->tx_begin_write ());
		write_lock_held_promise.set_value ();
		finished_promise.get_future ().wait ();
	})
	.detach ();

	write_lock_held_promise.get_future ().wait ();

	// Check inactive node can finish executing while a write lock is open
	nano::inactive_node node (path, nano::inactive_node_flag_defaults ());
	finished_promise.set_value ();
}

TEST (node, bidirectional_tcp)
{
#ifdef _WIN32
	if (nano::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
#endif
	nano::test::system system;
	nano::node_flags node_flags;
	// Disable bootstrap to start elections for new blocks
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto node1 = system.add_node (node_config, node_flags);
	node_config.peering_port = system.get_available_port ();
	node_config.tcp_incoming_connections_max = 0; // Disable incoming TCP connections for node 2
	auto node2 = system.add_node (node_config, node_flags);
	// Check network connections
	ASSERT_EQ (1, node1->network.size ());
	ASSERT_EQ (1, node2->network.size ());
	auto list1 (node1->network.list (1));
	ASSERT_EQ (nano::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_NE (node2->network.endpoint (), list1[0]->get_endpoint ()); // Ephemeral port
	ASSERT_EQ (node2->node_id.pub, list1[0]->get_node_id ());
	auto list2 (node2->network.list (1));
	ASSERT_EQ (nano::transport::transport_type::tcp, list2[0]->get_type ());
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (node1->node_id.pub, list2[0]->get_node_id ());
	// Test block propagation from node 1
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	node1->process_active (send1);
	ASSERT_TIMELY (10s, node1->block_or_pruned_exists (send1->hash ()) && node2->block_or_pruned_exists (send1->hash ()));
	// Test block confirmation from node 1 (add representative to node 1)
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
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
		system.wallet (0)->store.erase (transaction, nano::dev::genesis_key.pub);
	}
	/* Test block propagation from node 2
	Node 2 has only ephemeral TCP port open. Node 1 cannot establish connection to node 2 listening port */
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (send1->hash ()))
				 .build ();
	node2->process_active (send2);
	ASSERT_TIMELY (10s, node1->block_or_pruned_exists (send2->hash ()) && node2->block_or_pruned_exists (send2->hash ()));
	// Test block confirmation from node 2 (add representative to node 2)
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
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
	nano::test::system system (3);
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
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	nano::state_block_builder builder;
	nano::keypair key;

	// send half the voting weight to a non voting rep to ensure quorum cannot be reached
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - (nano::dev::constants.genesis_amount / 2))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (nano::dev::constants.genesis_amount / 2)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	// send 1 raw
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	// fork of send2 block
	auto fork = builder.make_block ()
				.from (*send2)
				.balance (send1->balance_field ().value ().number () - 2)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.build ();

	// Process and mark the first 2 blocks as confirmed to allow voting
	ASSERT_TRUE (nano::test::process (node, { send1, open }));
	nano::test::confirm (node.ledger, open);

	// wait until the rep weights have caught up with the weight transfer
	ASSERT_TIMELY_EQ (5s, nano::dev::constants.genesis_amount / 2, node.weight (key.pub));

	// process forked blocks, send2 will be the winner because it was first and there are no votes yet
	node.process_active (send2);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()));
	node.process_active (fork);
	ASSERT_TIMELY_EQ (5s, 2, election->blocks ().size ());
	ASSERT_EQ (election->winner ()->hash (), send2->hash ());

	{
		// The write guard prevents the block processor from performing the rollback
		auto write_guard = node.store.write_queue.wait (nano::store::writer::testing);

		ASSERT_EQ (0, election->votes_with_weight ().size ());
		// Vote with key to switch the winner
		election->vote (key.pub, 0, fork->hash (), nano::vote_source::live);
		ASSERT_EQ (1, election->votes_with_weight ().size ());
		// The winner changed
		ASSERT_EQ (election->winner ()->hash (), fork->hash ());

		// Insert genesis key in the wallet
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

		// Without the rollback being finished, the aggregator should not reply with any vote
		auto channel = std::make_shared<nano::transport::fake::channel> (node);
		node.aggregator.request ({ { send2->hash (), send2->root () } }, channel);
		ASSERT_ALWAYS_EQ (1s, node.stats.count (nano::stat::type::request_aggregator_replies), 0);

		// Going out of the scope allows the rollback to complete
	}

	// A vote is eventually generated from the local representative
	auto is_genesis_vote = [] (nano::vote_with_weight_info info) {
		return info.representative == nano::dev::genesis_key.pub;
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
	nano::test::system system;
	nano::node_config node_config (system.get_available_port ());
	node_config.backlog_population.enable = false;
	auto & node = *system.add_node (node_config);
	nano::state_block_builder builder;
	nano::keypair key;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
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
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
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
	ASSERT_EQ (nano::block_status::progress, node.process (send1));
	ASSERT_EQ (nano::block_status::progress, node.process (fork1a));
	// Node has 'fork1a' & doesn't have source 'send2' for winning 'fork1b' block
	ASSERT_EQ (nullptr, node.block (send2->hash ()));
	node.block_processor.force (fork1b);
	ASSERT_TIMELY_EQ (5s, node.block (fork1a->hash ()), nullptr);
	// Wait for the rollback (attempt to replace fork with open)
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::rollback, nano::stat::detail::open), 1);
	// But replacing is not possible (missing source block - send2)
	ASSERT_EQ (nullptr, node.block (fork1b->hash ()));
	// Fork can be returned by some other forked node
	node.process_active (fork1a);
	ASSERT_TIMELY (5s, node.block (fork1a->hash ()) != nullptr);
	// With send2 block in ledger election can start again to remove fork block
	ASSERT_EQ (nano::block_status::progress, node.process (send2));
	node.block_processor.force (fork1b);
	// Wait for new rollback
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::rollback, nano::stat::detail::open), 2);
	// Now fork block should be replaced with open
	ASSERT_TIMELY (5s, node.block (fork1b->hash ()) != nullptr);
	ASSERT_EQ (nullptr, node.block (fork1a->hash ()));
}

// Confirm a complex dependency graph starting from the first block
TEST (node, dependency_graph)
{
	nano::test::system system;
	nano::node_config config (system.get_available_port ());
	config.backlog_population.enable = false;
	auto & node = *system.add_node (config);

	nano::state_block_builder builder;
	nano::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - 1)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (nano::dev::genesis->hash ()))
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
					  .link (nano::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (nano::dev::constants.genesis_amount)
					   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance_field ().value ().number () - 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
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
					  .link (node.ledger.epoch_link (nano::epoch::epoch_1))
					  .balance (key3_receive->balance_field ().value ())
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	ASSERT_EQ (nano::block_status::progress, node.process (gen_send1));
	ASSERT_EQ (nano::block_status::progress, node.process (key1_open));
	ASSERT_EQ (nano::block_status::progress, node.process (key1_send1));
	ASSERT_EQ (nano::block_status::progress, node.process (gen_receive));
	ASSERT_EQ (nano::block_status::progress, node.process (gen_send2));
	ASSERT_EQ (nano::block_status::progress, node.process (key2_open));
	ASSERT_EQ (nano::block_status::progress, node.process (key2_send1));
	ASSERT_EQ (nano::block_status::progress, node.process (key3_open));
	ASSERT_EQ (nano::block_status::progress, node.process (key2_send2));
	ASSERT_EQ (nano::block_status::progress, node.process (key1_receive));
	ASSERT_EQ (nano::block_status::progress, node.process (key1_send2));
	ASSERT_EQ (nano::block_status::progress, node.process (key3_receive));
	ASSERT_EQ (nano::block_status::progress, node.process (key3_epoch));
	ASSERT_TRUE (node.active.empty ());

	// Hash -> Ancestors
	std::unordered_map<nano::block_hash, std::vector<nano::block_hash>> dependency_graph{
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
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
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
	nano::test::system system;
	nano::node_config config (system.get_available_port ());
	config.backlog_population.enable = false;
	auto & node1 = *system.add_node (config);
	config.peering_port = system.get_available_port ();
	config.backlog_population.enable = true;
	auto & node2 = *system.add_node (config);

	nano::state_block_builder builder;
	nano::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - 1)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (nano::dev::genesis->hash ()))
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
					  .link (nano::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (nano::dev::constants.genesis_amount)
					   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance_field ().value ().number () - 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
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
					  .link (node1.ledger.epoch_link (nano::epoch::epoch_1))
					  .balance (key3_receive->balance_field ().value ())
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	for (auto const & node : system.nodes)
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, gen_send1));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key1_open));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key1_send1));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, gen_receive));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, gen_send2));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key2_open));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key2_send1));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key3_open));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key2_send2));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key1_receive));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key1_send2));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key3_receive));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, key3_epoch));
	}

	// node1 can vote, but only on the first block
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	ASSERT_TIMELY (10s, node2.active.active (gen_send1->qualified_root ()));
	node1.start_election (gen_send1);

	ASSERT_TIMELY_EQ (15s, node1.ledger.cemented_count (), node1.ledger.block_count ());
	ASSERT_TIMELY_EQ (15s, node2.ledger.cemented_count (), node2.ledger.block_count ());
}

namespace nano
{
TEST (node, deferred_dependent_elections)
{
	nano::test::system system;
	nano::node_config node_config_1{ system.get_available_port () };
	node_config_1.backlog_population.enable = false;
	nano::node_config node_config_2{ system.get_available_port () };
	node_config_2.backlog_population.enable = false;
	nano::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config_1, flags);
	auto & node2 = *system.add_node (node_config_2, flags); // node2 will be used to ensure all blocks are being propagated

	nano::state_block_builder builder;
	nano::keypair key;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
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
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
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
				.representative (nano::dev::genesis_key.pub) // was key.pub
				.sign (key.prv, key.pub)
				.build ();

	nano::test::process (node, { send1 });
	auto election_send1 = nano::test::start_election (system, node, send1->hash ());
	ASSERT_NE (nullptr, election_send1);

	// Should process and republish but not start an election for any dependent blocks
	nano::test::process (node, { open, send2 });
	ASSERT_TIMELY (5s, node.block (open->hash ()));
	ASSERT_TIMELY (5s, node.block (send2->hash ()));
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()) || node.active.active (send2->qualified_root ()));
	ASSERT_TIMELY (5s, node2.block (open->hash ()));
	ASSERT_TIMELY (5s, node2.block (send2->hash ()));

	// Re-processing older blocks with updated work also does not start an election
	node.work_generate_blocking (*open, nano::dev::network_params.work.difficulty (*open) + 1);
	node.process_local (open);
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()));

	// It is however possible to manually start an election from elsewhere
	ASSERT_TRUE (nano::test::start_election (system, node, open->hash ()));
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));

	/// The election was dropped but it's still not possible to restart it
	node.work_generate_blocking (*open, nano::dev::network_params.work.difficulty (*open) + 1);
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
	ASSERT_EQ (nano::block_status::progress, node.process (receive));
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
	ASSERT_EQ (nano::block_status::fork, node.process (fork));
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
	nano::test::system system{};

	nano::node_config node_config{ system.get_available_port () };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;
	node_config.max_pruning_age = std::chrono::seconds (1);

	nano::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	nano::keypair key1{};
	nano::send_block_builder builder{};
	auto latest_hash = nano::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.block (send2->hash ()) != nullptr);

	// Force-confirm both blocks
	node1.process_confirmed (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.process_confirmed (send2->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Check pruning result
	ASSERT_EQ (3, node1.ledger.block_count ());
	ASSERT_TIMELY_EQ (5s, node1.ledger.pruned_count (), 1);
	ASSERT_TIMELY_EQ (5s, node1.store.pruned.count (node1.store.tx_begin_read ()), 1);
	ASSERT_EQ (1, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	ASSERT_TRUE (nano::test::block_or_pruned_all_exists (node1, { nano::dev::genesis, send1, send2 }));
}

TEST (node, DISABLED_pruning_age)
{
	nano::test::system system{};

	nano::node_config node_config{ system.get_available_port () };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	nano::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	nano::keypair key1{};
	nano::send_block_builder builder{};
	auto latest_hash = nano::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.process_confirmed (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.process_confirmed (send2->hash ());
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

	ASSERT_TRUE (nano::test::block_or_pruned_all_exists (node1, { nano::dev::genesis, send1, send2 }));
}

// Test that a node configured with `enable_pruning` will
// prune DEEP-enough confirmed blocks by explicitly saying `node.ledger_pruning` in the unit test
TEST (node, DISABLED_pruning_depth)
{
	nano::test::system system{};

	nano::node_config node_config{ system.get_available_port () };
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	nano::node_flags node_flags{};
	node_flags.enable_pruning = true;

	auto & node1 = *system.add_node (node_config, node_flags);
	nano::keypair key1{};
	nano::send_block_builder builder{};
	auto latest_hash = nano::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.process_confirmed (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.process_confirmed (send2->hash ());
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

	ASSERT_TRUE (nano::test::block_or_pruned_all_exists (node1, { nano::dev::genesis, send1, send2 }));
}

TEST (node_config, node_id_private_key_persistence)
{
	nano::test::system system;

	// create the directory and the file
	auto path = nano::unique_path ();
	ASSERT_TRUE (std::filesystem::exists (path));
	auto priv_key_filename = path / "node_id_private.key";

	// check that the key generated is random when the key does not exist
	nano::keypair kp1 = nano::load_or_create_node_id (path);
	std::filesystem::remove (priv_key_filename);
	nano::keypair kp2 = nano::load_or_create_node_id (path);
	ASSERT_NE (kp1.prv, kp2.prv);

	// check that the key persists
	nano::keypair kp3 = nano::load_or_create_node_id (path);
	ASSERT_EQ (kp2.prv, kp3.prv);

	// write the key file manually and check that right key is loaded
	std::ofstream ofs (priv_key_filename.string (), std::ofstream::out | std::ofstream::trunc);
	ofs << "3F28D035B8AA75EA53DF753BFD065CF6138E742971B2C99B84FD8FE328FED2D9" << std::flush;
	ofs.close ();
	nano::keypair kp4 = nano::load_or_create_node_id (path);
	ASSERT_EQ (kp4.prv, nano::keypair ("3F28D035B8AA75EA53DF753BFD065CF6138E742971B2C99B84FD8FE328FED2D9").prv);
}

TEST (node, port_mapping)
{
	nano::test::system system;
	auto node = system.add_node ();
	node->port_mapping.refresh_devices ();
}

TEST (node, process_local_overflow)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.block_processor.max_system_queue = 0;
	auto & node = *system.add_node (config);

	nano::keypair key1;
	nano::send_block_builder builder;
	auto latest_hash = nano::dev::genesis->hash ();
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();

	auto result = node.process_local (send1);
	ASSERT_FALSE (result);
}

TEST (node, local_block_broadcast)
{
	nano::test::system system;

	// Disable active elections to prevent the block from being broadcasted by the election
	auto node_config = system.default_config ();
	node_config.priority_scheduler.enable = false;
	node_config.hinted_scheduler.enable = false;
	node_config.optimistic_scheduler.enable = false;
	node_config.local_block_broadcaster.rebroadcast_interval = 1s;
	auto & node1 = *system.add_node (node_config);
	auto & node2 = *system.make_disconnected_node ();

	nano::keypair key1;
	nano::send_block_builder builder;
	auto latest_hash = nano::dev::genesis->hash ();
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();

	auto result = node1.process_local (send1);
	ASSERT_TRUE (result);
	ASSERT_NEVER (500ms, node1.active.active (send1->qualified_root ()));

	// Wait until a broadcast is attempted
	ASSERT_TIMELY_EQ (5s, node1.local_block_broadcaster.size (), 1);
	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::local_block_broadcaster, nano::stat::detail::broadcast, nano::stat::dir::out) >= 1);

	// The other node should not have received the block
	ASSERT_NEVER (500ms, node2.block (send1->hash ()));

	// Connect the nodes and check that the block is propagated
	node1.network.merge_peer (node2.network.endpoint ());
	ASSERT_TIMELY (5s, node1.network.find_node_id (node2.get_node_id ()));
	ASSERT_TIMELY (10s, node2.block (send1->hash ()));
}

TEST (node, container_info)
{
	nano::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();

	// Generate some random activity
	std::vector<nano::account> accounts;
	auto dev_genesis_key = nano::dev::genesis_key;
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