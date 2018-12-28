#include <gtest/gtest.h>
#include <rai/core_test/testutil.hpp>
#include <rai/node/testing.hpp>
#include <rai/node/working.hpp>

#include <boost/make_shared.hpp>
#include <boost/polymorphic_cast.hpp>

using namespace std::chrono_literals;

TEST (node, stop)
{
	rai::system system (24000, 1);
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.begin ());
	system.nodes[0]->stop ();
	system.io_ctx.run ();
	ASSERT_TRUE (true);
}

TEST (node, block_store_path_failure)
{
	rai::node_init init;
	auto service (boost::make_shared<boost::asio::io_context> ());
	rai::alarm alarm (*service);
	auto path (rai::unique_path ());
	rai::logging logging;
	logging.init (path);
	rai::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
	auto node (std::make_shared<rai::node> (init, *service, 0, path, alarm, logging, work));
	ASSERT_TRUE (node->wallets.items.empty ());
	node->stop ();
}

TEST (node, password_fanout)
{
	rai::node_init init;
	auto service (boost::make_shared<boost::asio::io_context> ());
	rai::alarm alarm (*service);
	auto path (rai::unique_path ());
	rai::node_config config;
	config.peering_port = 24000;
	config.logging.init (path);
	rai::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
	config.password_fanout = 10;
	auto node (std::make_shared<rai::node> (init, *service, path, alarm, config, work));
	auto wallet (node->wallets.create (100));
	ASSERT_EQ (10, wallet->store.password.values.size ());
	node->stop ();
}

TEST (node, balance)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto transaction (system.nodes[0]->store.tx_begin (true));
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max (), system.nodes[0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
}

TEST (node, representative)
{
	rai::system system (24000, 1);
	auto block1 (system.nodes[0]->representative (rai::test_genesis_key.pub));
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		ASSERT_TRUE (system.nodes[0]->ledger.store.block_exists (transaction, block1));
	}
	rai::keypair key;
	ASSERT_TRUE (system.nodes[0]->representative (key.pub).is_zero ());
}

TEST (node, send_unkeyed)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->store.password.value_set (rai::keypair ().prv);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
}

TEST (node, send_self)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (rai::test_genesis_key.pub));
}

TEST (node, send_single)
{
	rai::system system (24000, 2);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (rai::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_single_observing_peer)
{
	rai::system system (24000, 3);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (rai::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (10s);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<rai::node> const & node_a) { return node_a->balance (key2.pub).is_zero (); }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_single_many_peers)
{
	rai::system system (24000, 10);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (rai::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (3.5min);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<rai::node> const & node_a) { return node_a->balance (key2.pub).is_zero (); }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_out_of_order)
{
	rai::system system (24000, 2);
	rai::keypair key2;
	rai::genesis genesis;
	rai::send_block send1 (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ()));
	rai::send_block send2 (send1.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (send1.hash ()));
	rai::send_block send3 (send2.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 3, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (send2.hash ()));
	system.nodes[0]->process_active (std::make_shared<rai::send_block> (send3));
	system.nodes[0]->process_active (std::make_shared<rai::send_block> (send2));
	system.nodes[0]->process_active (std::make_shared<rai::send_block> (send1));
	system.deadline_set (10s);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<rai::node> const & node_a) { return node_a->balance (rai::test_genesis_key.pub) != rai::genesis_amount - system.nodes[0]->config.receive_minimum.number () * 3; }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, quick_confirm)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::block_hash previous (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto genesis_start_balance (system.nodes[0]->balance (rai::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto send (std::make_shared<rai::send_block> (previous, key.pub, system.nodes[0]->config.online_weight_minimum.number () + 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (previous)));
	system.nodes[0]->process_active (send);
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->balance (rai::test_genesis_key.pub), system.nodes[0]->config.online_weight_minimum.number () + 1);
	ASSERT_EQ (system.nodes[0]->balance (key.pub), genesis_start_balance - (system.nodes[0]->config.online_weight_minimum.number () + 1));
}

TEST (node, node_receive_quorum)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::block_hash previous (system.nodes[0]->latest (rai::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	auto send (std::make_shared<rai::send_block> (previous, key.pub, rai::genesis_amount - rai::Gxrb_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (previous)));
	system.nodes[0]->process_active (send);
	system.deadline_set (10s);
	while (!system.nodes[0]->ledger.block_exists (send->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto done (false);
	while (!done)
	{
		auto info (system.nodes[0]->active.roots.find (previous));
		ASSERT_NE (system.nodes[0]->active.roots.end (), info);
		done = info->election->announcements > rai::active_transactions::announcement_min;
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::system system2 (24001, 1);
	system2.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	ASSERT_TRUE (system.nodes[0]->balance (key.pub).is_zero ());
	system.nodes[0]->network.send_keepalive (system2.nodes[0]->network.endpoint ());
	while (system.nodes[0]->balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, auto_bootstrap)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::node_init init1;
	auto node1 (std::make_shared<rai::node> (init1, system.io_ctx, 24001, rai::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	node1->network.send_keepalive (system.nodes[0]->network.endpoint ());
	node1->start ();
	system.nodes.push_back (node1);
	while (!node1->bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	while (node1->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	while (node1->bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	};
	node1->stop ();
}

TEST (node, auto_bootstrap_reverse)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	rai::node_init init1;
	auto node1 (std::make_shared<rai::node> (init1, system.io_ctx, 24001, rai::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.nodes[0]->network.send_keepalive (node1->network.endpoint ());
	node1->start ();
	system.nodes.push_back (node1);
	system.deadline_set (10s);
	while (node1->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (node, receive_gap)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (0, node1.gap_cache.blocks.size ());
	auto block (std::make_shared<rai::send_block> (5, 1, 2, rai::keypair ().prv, 4, 0));
	node1.work_generate_blocking (*block);
	rai::publish message (block);
	node1.process_message (message, node1.network.endpoint ());
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.gap_cache.blocks.size ());
}

TEST (node, merge_peers)
{
	rai::system system (24000, 1);
	std::array<rai::endpoint, 8> endpoints;
	endpoints.fill (rai::endpoint (boost::asio::ip::address_v6::loopback (), 24000));
	endpoints[0] = rai::endpoint (boost::asio::ip::address_v6::loopback (), 24001);
	system.nodes[0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes[0]->peers.peers.size ());
}

TEST (node, search_pending)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node (system.nodes[0]);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_same)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node (system.nodes[0]);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub) != 2 * system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_multiple)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	rai::keypair key3;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key3.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key3.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key3.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (key3.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node (system.nodes[0]);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub) != 2 * system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, unlock_search)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	rai::uint128_t balance (system.nodes[0]->balance (rai::test_genesis_key.pub));
	{
		auto transaction (system.wallet (0)->wallets.tx_begin (true));
		system.wallet (0)->store.rekey (transaction, "");
	}
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (rai::test_genesis_key.pub) == balance)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	system.wallet (0)->store.password.value_set (rai::keypair ().prv);
	auto node (system.nodes[0]);
	{
		auto transaction (system.wallet (0)->wallets.tx_begin (true));
		ASSERT_FALSE (system.wallet (0)->enter_password (transaction, ""));
	}
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, connect_after_junk)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (std::make_shared<rai::node> (init1, system.io_ctx, 24001, rai::unique_path (), system.alarm, system.logging, system.work));
	uint64_t junk (0);
	node1->network.socket.async_send_to (boost::asio::buffer (&junk, sizeof (junk)), system.nodes[0]->network.endpoint (), [](boost::system::error_code const &, size_t) {});
	system.deadline_set (10s);
	while (system.nodes[0]->stats.count (rai::stat::type::error) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->start ();
	system.nodes.push_back (node1);
	node1->network.send_keepalive (system.nodes[0]->network.endpoint ());
	system.deadline_set (10s);
	while (node1->peers.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (node, working)
{
	auto path (rai::working_path ());
	ASSERT_FALSE (path.empty ());
}

TEST (logging, serialization)
{
	auto path (rai::unique_path ());
	rai::logging logging1;
	logging1.init (path);
	logging1.ledger_logging_value = !logging1.ledger_logging_value;
	logging1.ledger_duplicate_logging_value = !logging1.ledger_duplicate_logging_value;
	logging1.network_logging_value = !logging1.network_logging_value;
	logging1.network_message_logging_value = !logging1.network_message_logging_value;
	logging1.network_publish_logging_value = !logging1.network_publish_logging_value;
	logging1.network_packet_logging_value = !logging1.network_packet_logging_value;
	logging1.network_keepalive_logging_value = !logging1.network_keepalive_logging_value;
	logging1.network_node_id_handshake_logging_value = !logging1.network_node_id_handshake_logging_value;
	logging1.node_lifetime_tracing_value = !logging1.node_lifetime_tracing_value;
	logging1.insufficient_work_logging_value = !logging1.insufficient_work_logging_value;
	logging1.log_rpc_value = !logging1.log_rpc_value;
	logging1.bulk_pull_logging_value = !logging1.bulk_pull_logging_value;
	logging1.work_generation_time_value = !logging1.work_generation_time_value;
	logging1.log_to_cerr_value = !logging1.log_to_cerr_value;
	logging1.max_size = 10;
	boost::property_tree::ptree tree;
	logging1.serialize_json (tree);
	rai::logging logging2;
	logging2.init (path);
	bool upgraded (false);
	ASSERT_FALSE (logging2.deserialize_json (upgraded, tree));
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (logging1.ledger_logging_value, logging2.ledger_logging_value);
	ASSERT_EQ (logging1.ledger_duplicate_logging_value, logging2.ledger_duplicate_logging_value);
	ASSERT_EQ (logging1.network_logging_value, logging2.network_logging_value);
	ASSERT_EQ (logging1.network_message_logging_value, logging2.network_message_logging_value);
	ASSERT_EQ (logging1.network_publish_logging_value, logging2.network_publish_logging_value);
	ASSERT_EQ (logging1.network_packet_logging_value, logging2.network_packet_logging_value);
	ASSERT_EQ (logging1.network_keepalive_logging_value, logging2.network_keepalive_logging_value);
	ASSERT_EQ (logging1.network_node_id_handshake_logging_value, logging2.network_node_id_handshake_logging_value);
	ASSERT_EQ (logging1.node_lifetime_tracing_value, logging2.node_lifetime_tracing_value);
	ASSERT_EQ (logging1.insufficient_work_logging_value, logging2.insufficient_work_logging_value);
	ASSERT_EQ (logging1.log_rpc_value, logging2.log_rpc_value);
	ASSERT_EQ (logging1.bulk_pull_logging_value, logging2.bulk_pull_logging_value);
	ASSERT_EQ (logging1.work_generation_time_value, logging2.work_generation_time_value);
	ASSERT_EQ (logging1.log_to_cerr_value, logging2.log_to_cerr_value);
	ASSERT_EQ (logging1.max_size, logging2.max_size);
}

TEST (logging, upgrade_v1_v2)
{
	auto path1 (rai::unique_path ());
	auto path2 (rai::unique_path ());
	rai::logging logging1;
	logging1.init (path1);
	rai::logging logging2;
	logging2.init (path2);
	boost::property_tree::ptree tree;
	logging1.serialize_json (tree);
	tree.erase ("version");
	tree.erase ("vote");
	bool upgraded (false);
	ASSERT_FALSE (logging2.deserialize_json (upgraded, tree));
	ASSERT_LE (2, tree.get<int> ("version"));
	ASSERT_FALSE (tree.get<bool> ("vote"));
}

TEST (node, price)
{
	rai::system system (24000, 1);
	auto price1 (system.nodes[0]->price (rai::Gxrb_ratio, 1));
	ASSERT_EQ (rai::node::price_max * 100.0, price1);
	auto price2 (system.nodes[0]->price (rai::Gxrb_ratio * int(rai::node::free_cutoff + 1), 1));
	ASSERT_EQ (0, price2);
	auto price3 (system.nodes[0]->price (rai::Gxrb_ratio * int(rai::node::free_cutoff + 2) / 2, 1));
	ASSERT_EQ (rai::node::price_max * 100.0 / 2, price3);
	auto price4 (system.nodes[0]->price (rai::Gxrb_ratio * int(rai::node::free_cutoff) * 2, 1));
	ASSERT_EQ (0, price4);
}

TEST (node_config, serialization)
{
	auto path (rai::unique_path ());
	rai::logging logging1;
	logging1.init (path);
	rai::node_config config1 (100, logging1);
	config1.bootstrap_fraction_numerator = 10;
	config1.receive_minimum = 10;
	config1.online_weight_minimum = 10;
	config1.online_weight_quorum = 10;
	config1.password_fanout = 20;
	config1.enable_voting = false;
	config1.callback_address = "test";
	config1.callback_port = 10;
	config1.callback_target = "test";
	config1.lmdb_max_dbs = 256;
	boost::property_tree::ptree tree;
	config1.serialize_json (tree);
	rai::logging logging2;
	logging2.init (path);
	logging2.node_lifetime_tracing_value = !logging2.node_lifetime_tracing_value;
	rai::node_config config2 (50, logging2);
	ASSERT_NE (config2.bootstrap_fraction_numerator, config1.bootstrap_fraction_numerator);
	ASSERT_NE (config2.peering_port, config1.peering_port);
	ASSERT_NE (config2.logging.node_lifetime_tracing_value, config1.logging.node_lifetime_tracing_value);
	ASSERT_NE (config2.online_weight_minimum, config1.online_weight_minimum);
	ASSERT_NE (config2.online_weight_quorum, config1.online_weight_quorum);
	ASSERT_NE (config2.password_fanout, config1.password_fanout);
	ASSERT_NE (config2.enable_voting, config1.enable_voting);
	ASSERT_NE (config2.callback_address, config1.callback_address);
	ASSERT_NE (config2.callback_port, config1.callback_port);
	ASSERT_NE (config2.callback_target, config1.callback_target);
	ASSERT_NE (config2.lmdb_max_dbs, config1.lmdb_max_dbs);

	ASSERT_FALSE (tree.get_optional<std::string> ("epoch_block_link"));
	ASSERT_FALSE (tree.get_optional<std::string> ("epoch_block_signer"));

	bool upgraded (false);
	ASSERT_FALSE (config2.deserialize_json (upgraded, tree));
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config2.bootstrap_fraction_numerator, config1.bootstrap_fraction_numerator);
	ASSERT_EQ (config2.peering_port, config1.peering_port);
	ASSERT_EQ (config2.logging.node_lifetime_tracing_value, config1.logging.node_lifetime_tracing_value);
	ASSERT_EQ (config2.online_weight_minimum, config1.online_weight_minimum);
	ASSERT_EQ (config2.online_weight_quorum, config1.online_weight_quorum);
	ASSERT_EQ (config2.password_fanout, config1.password_fanout);
	ASSERT_EQ (config2.enable_voting, config1.enable_voting);
	ASSERT_EQ (config2.callback_address, config1.callback_address);
	ASSERT_EQ (config2.callback_port, config1.callback_port);
	ASSERT_EQ (config2.callback_target, config1.callback_target);
	ASSERT_EQ (config2.lmdb_max_dbs, config1.lmdb_max_dbs);
}

TEST (node_config, v1_v2_upgrade)
{
	auto path (rai::unique_path ());
	rai::logging logging1;
	logging1.init (path);
	boost::property_tree::ptree tree;
	tree.put ("peering_port", std::to_string (0));
	tree.put ("packet_delay_microseconds", std::to_string (0));
	tree.put ("bootstrap_fraction_numerator", std::to_string (0));
	tree.put ("creation_rebroadcast", std::to_string (0));
	tree.put ("rebroadcast_delay", std::to_string (0));
	tree.put ("receive_minimum", rai::amount (0).to_string_dec ());
	boost::property_tree::ptree logging_l;
	logging1.serialize_json (logging_l);
	tree.add_child ("logging", logging_l);
	boost::property_tree::ptree preconfigured_peers_l;
	tree.add_child ("preconfigured_peers", preconfigured_peers_l);
	boost::property_tree::ptree preconfigured_representatives_l;
	tree.add_child ("preconfigured_representatives", preconfigured_representatives_l);
	bool upgraded (false);
	rai::node_config config1;
	config1.logging.init (path);
	ASSERT_FALSE (tree.get_child_optional ("work_peers"));
	config1.deserialize_json (upgraded, tree);
	ASSERT_TRUE (upgraded);
	ASSERT_TRUE (!!tree.get_child_optional ("work_peers"));
}

TEST (node_config, v2_v3_upgrade)
{
	auto path (rai::unique_path ());
	rai::logging logging1;
	logging1.init (path);
	boost::property_tree::ptree tree;
	tree.put ("peering_port", std::to_string (0));
	tree.put ("packet_delay_microseconds", std::to_string (0));
	tree.put ("bootstrap_fraction_numerator", std::to_string (0));
	tree.put ("creation_rebroadcast", std::to_string (0));
	tree.put ("rebroadcast_delay", std::to_string (0));
	tree.put ("receive_minimum", rai::amount (0).to_string_dec ());
	tree.put ("version", "2");
	boost::property_tree::ptree logging_l;
	logging1.serialize_json (logging_l);
	tree.add_child ("logging", logging_l);
	boost::property_tree::ptree preconfigured_peers_l;
	tree.add_child ("preconfigured_peers", preconfigured_peers_l);
	boost::property_tree::ptree preconfigured_representatives_l;
	boost::property_tree::ptree entry;
	entry.put ("", "TR6ZJ4pdp6HC76xMRpVDny5x2s8AEbrhFue3NKVxYYdmKuTEib");
	preconfigured_representatives_l.push_back (std::make_pair ("", entry));
	tree.add_child ("preconfigured_representatives", preconfigured_representatives_l);
	boost::property_tree::ptree work_peers_l;
	tree.add_child ("work_peers", work_peers_l);
	bool upgraded (false);
	rai::node_config config1;
	config1.logging.init (path);
	ASSERT_FALSE (tree.get_optional<std::string> ("inactive_supply"));
	ASSERT_FALSE (tree.get_optional<std::string> ("password_fanout"));
	ASSERT_FALSE (tree.get_optional<std::string> ("io_threads"));
	ASSERT_FALSE (tree.get_optional<std::string> ("work_threads"));
	config1.deserialize_json (upgraded, tree);
	//ASSERT_EQ (rai::uint128_union (0).to_string_dec (), tree.get<std::string> ("inactive_supply"));
	ASSERT_EQ ("1024", tree.get<std::string> ("password_fanout"));
	ASSERT_NE (0, std::stoul (tree.get<std::string> ("password_fanout")));
	ASSERT_NE (0, std::stoul (tree.get<std::string> ("password_fanout")));
	ASSERT_TRUE (upgraded);
	auto version (tree.get<std::string> ("version"));
	ASSERT_GT (std::stoull (version), 2);
}

TEST (node, confirm_locked)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto transaction (system.nodes[0]->store.tx_begin ());
	system.wallet (0)->enter_password (transaction, "1");
	auto block (std::make_shared<rai::send_block> (0, 0, 0, rai::keypair ().prv, 0, 0));
	system.nodes[0]->network.republish_block (block);
}

TEST (node_config, random_rep)
{
	auto path (rai::unique_path ());
	rai::logging logging1;
	logging1.init (path);
	rai::node_config config1 (100, logging1);
	auto rep (config1.random_representative ());
	ASSERT_NE (config1.preconfigured_representatives.end (), std::find (config1.preconfigured_representatives.begin (), config1.preconfigured_representatives.end (), rep));
}

TEST (node, fork_publish)
{
	std::weak_ptr<rai::node> node0;
	{
		rai::system system (24000, 1);
		node0 = system.nodes[0];
		auto & node1 (*system.nodes[0]);
		system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
		rai::keypair key1;
		rai::genesis genesis;
		auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
		node1.work_generate_blocking (*send1);
		rai::keypair key2;
		auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
		node1.work_generate_blocking (*send2);
		node1.process_active (send1);
		node1.block_processor.flush ();
		ASSERT_EQ (1, node1.active.roots.size ());
		auto existing (node1.active.roots.find (send1->root ()));
		ASSERT_NE (node1.active.roots.end (), existing);
		auto election (existing->election);
		auto transaction (node1.store.tx_begin ());
		election->compute_rep_votes (transaction);
		node1.vote_processor.flush ();
		ASSERT_EQ (2, election->last_votes.size ());
		node1.process_active (send2);
		node1.block_processor.flush ();
		auto existing1 (election->last_votes.find (rai::test_genesis_key.pub));
		ASSERT_NE (election->last_votes.end (), existing1);
		ASSERT_EQ (send1->hash (), existing1->second.hash);
		auto winner (*election->tally (transaction).begin ());
		ASSERT_EQ (*send1, *winner.second);
		ASSERT_EQ (rai::genesis_amount - 100, winner.first);
	}
	ASSERT_TRUE (node0.expired ());
}

TEST (node, fork_keep)
{
	rai::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.peers.size ());
	rai::keypair key1;
	rai::keypair key2;
	rai::genesis genesis;
	// send1 and send2 fork to different accounts
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	node1.block_processor.flush ();
	node2.process_active (send1);
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.roots.size ());
	ASSERT_EQ (1, node2.active.roots.size ());
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	node1.process_active (send2);
	node1.block_processor.flush ();
	node2.process_active (send2);
	node2.block_processor.flush ();
	auto conflict (node2.active.roots.find (genesis.hash ()));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	{
		auto transaction0 (system.nodes[0]->store.tx_begin ());
		auto transaction1 (system.nodes[1]->store.tx_begin ());
		ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction0, send1->hash ()));
		ASSERT_TRUE (system.nodes[1]->store.block_exists (transaction1, send1->hash ()));
	}
	system.deadline_set (1.5min);
	// Wait until the genesis rep makes a vote
	while (votes1->last_votes.size () == 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto transaction0 (system.nodes[0]->store.tx_begin ());
	auto transaction1 (system.nodes[1]->store.tx_begin ());
	// The vote should be in agreement with what we already have.
	auto winner (*votes1->tally (transaction1).begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (rai::genesis_amount - 100, winner.first);
	ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction0, send1->hash ()));
	ASSERT_TRUE (system.nodes[1]->store.block_exists (transaction1, send1->hash ()));
}

TEST (node, fork_flip)
{
	rai::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.peers.size ());
	rai::keypair key1;
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	rai::publish publish1 (send1);
	rai::keypair key2;
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	rai::publish publish2 (send2);
	node1.process_message (publish1, node1.network.endpoint ());
	node1.block_processor.flush ();
	node2.process_message (publish2, node1.network.endpoint ());
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.roots.size ());
	ASSERT_EQ (1, node2.active.roots.size ());
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	node1.process_message (publish2, node1.network.endpoint ());
	node1.block_processor.flush ();
	node2.process_message (publish1, node2.network.endpoint ());
	node2.block_processor.flush ();
	auto conflict (node2.active.roots.find (genesis.hash ()));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
	}
	{
		auto transaction (system.nodes[1]->store.tx_begin ());
		ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
	}
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
		done = node2.ledger.block_exists (publish1.block->hash ());
	}
	auto transaction1 (system.nodes[0]->store.tx_begin ());
	auto transaction2 (system.nodes[1]->store.tx_begin ());
	auto winner (*votes1->tally (transaction2).begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (rai::genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, publish1.block->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, publish1.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, publish2.block->hash ()));
}

TEST (node, fork_multi_flip)
{
	rai::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.peers.size ());
	rai::keypair key1;
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	rai::publish publish1 (send1);
	rai::keypair key2;
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	rai::publish publish2 (send2);
	auto send3 (std::make_shared<rai::send_block> (publish2.block->hash (), key2.pub, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (publish2.block->hash ())));
	rai::publish publish3 (send3);
	node1.process_message (publish1, node1.network.endpoint ());
	node1.block_processor.flush ();
	node2.process_message (publish2, node2.network.endpoint ());
	node2.process_message (publish3, node2.network.endpoint ());
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.roots.size ());
	ASSERT_EQ (2, node2.active.roots.size ());
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	node1.process_message (publish2, node1.network.endpoint ());
	node1.process_message (publish3, node1.network.endpoint ());
	node1.block_processor.flush ();
	node2.process_message (publish1, node2.network.endpoint ());
	node2.block_processor.flush ();
	auto conflict (node2.active.roots.find (genesis.hash ()));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
	}
	{
		auto transaction (system.nodes[1]->store.tx_begin ());
		ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
		ASSERT_TRUE (node2.store.block_exists (transaction, publish3.block->hash ()));
	}
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
		done = node2.ledger.block_exists (publish1.block->hash ());
	}
	auto transaction1 (system.nodes[0]->store.tx_begin ());
	auto transaction2 (system.nodes[1]->store.tx_begin ());
	auto winner (*votes1->tally (transaction2).begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (rai::genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, publish1.block->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, publish1.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, publish2.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, publish3.block->hash ()));
}

// Blocks that are no longer actively being voted on should be able to be evicted through bootstrapping.
// This could happen if a fork wasn't resolved before the process previously shut down
TEST (node, fork_bootstrap_flip)
{
	rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto & node1 (*system0.nodes[0]);
	auto & node2 (*system1.nodes[0]);
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::block_hash latest (system0.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::keypair key1;
	auto send1 (std::make_shared<rai::send_block> (latest, key1.pub, rai::genesis_amount - rai::Gxrb_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system0.work.generate (latest)));
	rai::keypair key2;
	auto send2 (std::make_shared<rai::send_block> (latest, key2.pub, rai::genesis_amount - rai::Gxrb_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system0.work.generate (latest)));
	// Insert but don't rebroadcast, simulating settled blocks
	node1.block_processor.add (send1, std::chrono::steady_clock::now ());
	node1.block_processor.flush ();
	node2.block_processor.add (send2, std::chrono::steady_clock::now ());
	node2.block_processor.flush ();
	{
		auto transaction (node2.store.tx_begin ());
		ASSERT_TRUE (node2.store.block_exists (transaction, send2->hash ()));
	}
	node1.network.send_keepalive (node2.network.endpoint ());
	system1.deadline_set (50s);
	while (node2.peers.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	auto again (true);
	system1.deadline_set (50s);
	while (again)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
		auto transaction (node2.store.tx_begin ());
		again = !node2.store.block_exists (transaction, send1->hash ());
	}
}

TEST (node, fork_open)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	rai::keypair key1;
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	rai::publish publish1 (send1);
	node1.process_message (publish1, node1.network.endpoint ());
	node1.block_processor.flush ();
	auto open1 (std::make_shared<rai::open_block> (publish1.block->hash (), 1, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	rai::publish publish2 (open1);
	node1.process_message (publish2, node1.network.endpoint ());
	node1.block_processor.flush ();
	auto open2 (std::make_shared<rai::open_block> (publish1.block->hash (), 2, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	rai::publish publish3 (open2);
	ASSERT_EQ (2, node1.active.roots.size ());
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	node1.process_message (publish3, node1.network.endpoint ());
	node1.block_processor.flush ();
}

TEST (node, fork_open_flip)
{
	rai::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.peers.size ());
	rai::keypair key1;
	rai::genesis genesis;
	rai::keypair rep1;
	rai::keypair rep2;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, rai::genesis_amount - 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	node2.process_active (send1);
	// We should be keeping this block
	auto open1 (std::make_shared<rai::open_block> (send1->hash (), rep1.pub, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	// This block should be evicted
	auto open2 (std::make_shared<rai::open_block> (send1->hash (), rep2.pub, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	ASSERT_FALSE (*open1 == *open2);
	// node1 gets copy that will remain
	node1.process_active (open1);
	node1.block_processor.flush ();
	// node2 gets copy that will be evicted
	node2.process_active (open2);
	node2.block_processor.flush ();
	ASSERT_EQ (2, node1.active.roots.size ());
	ASSERT_EQ (2, node2.active.roots.size ());
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	// Notify both nodes that a fork exists
	node1.process_active (open2);
	node1.block_processor.flush ();
	node2.process_active (open1);
	node2.block_processor.flush ();
	auto conflict (node2.active.roots.find (open1->root ()));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	ASSERT_TRUE (node1.block (open1->hash ()) != nullptr);
	ASSERT_TRUE (node2.block (open2->hash ()) != nullptr);
	system.deadline_set (10s);
	// Node2 should eventually settle on open1
	while (node2.block (open1->hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node2.block_processor.flush ();
	auto transaction1 (system.nodes[0]->store.tx_begin ());
	auto transaction2 (system.nodes[1]->store.tx_begin ());
	auto winner (*votes1->tally (transaction2).begin ());
	ASSERT_EQ (*open1, *winner.second);
	ASSERT_EQ (rai::genesis_amount - 1, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, open1->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, open1->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, open2->hash ()));
}

TEST (node, coherent_observer)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	node1.observers.blocks.add ([&node1](std::shared_ptr<rai::block> block_a, rai::account const &, rai::uint128_t const &, bool) {
		auto transaction (node1.store.tx_begin ());
		ASSERT_TRUE (node1.store.block_exists (transaction, block_a->hash ()));
	});
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1);
}

TEST (node, fork_no_vote_quorum)
{
	rai::system system (24000, 3);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto & node3 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto key4 (system.wallet (0)->deterministic_insert ());
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key4, rai::genesis_amount / 4);
	auto key1 (system.wallet (1)->deterministic_insert ());
	{
		auto transaction (system.wallet (1)->wallets.tx_begin (true));
		system.wallet (1)->store.representative_set (transaction, key1);
	}
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1, node1.config.receive_minimum.number ()));
	ASSERT_NE (nullptr, block);
	system.deadline_set (30s);
	while (node3.balance (key1) != node1.config.receive_minimum.number () || node2.balance (key1) != node1.config.receive_minimum.number () || node1.balance (key1) != node1.config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node3.weight (key1));
	rai::state_block send1 (rai::test_genesis_key.pub, block->hash (), rai::test_genesis_key.pub, (rai::genesis_amount / 4) - (node1.config.receive_minimum.number () * 2), key1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (block->hash ()));
	ASSERT_EQ (rai::process_result::progress, node1.process (send1).code);
	ASSERT_EQ (rai::process_result::progress, node2.process (send1).code);
	ASSERT_EQ (rai::process_result::progress, node3.process (send1).code);
	auto key2 (system.wallet (2)->deterministic_insert ());
	auto send2 (std::make_shared<rai::send_block> (block->hash (), key2, (rai::genesis_amount / 4) - (node1.config.receive_minimum.number () * 2), rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (block->hash ())));
	rai::raw_key key3;
	auto transaction (system.wallet (1)->wallets.tx_begin ());
	ASSERT_FALSE (system.wallet (1)->store.fetch (transaction, key1, key3));
	auto vote (std::make_shared<rai::vote> (key1, key3, 0, send2));
	rai::confirm_ack confirm (vote);
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		rai::vectorstream stream (*bytes);
		confirm.serialize (stream);
	}
	node2.network.confirm_send (confirm, bytes, node3.network.endpoint ());
	while (node3.stats.count (rai::stat::type::message, rai::stat::detail::confirm_ack, rai::stat::dir::in) < 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node1.latest (rai::test_genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node2.latest (rai::test_genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node3.latest (rai::test_genesis_key.pub) == send1.hash ());
}

// Disabled because it sometimes takes way too long (but still eventually finishes)
TEST (node, DISABLED_fork_pre_confirm)
{
	rai::system system (24000, 3);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto & node2 (*system.nodes[2]);
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key1;
	system.wallet (1)->insert_adhoc (key1.prv);
	{
		auto transaction (system.wallet (1)->wallets.tx_begin (true));
		system.wallet (1)->store.representative_set (transaction, key1.pub);
	}
	rai::keypair key2;
	system.wallet (2)->insert_adhoc (key2.prv);
	{
		auto transaction (system.wallet (2)->wallets.tx_begin (true));
		system.wallet (2)->store.representative_set (transaction, key2.pub);
	}
	system.deadline_set (30s);
	auto block0 (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, rai::genesis_amount / 3));
	ASSERT_NE (nullptr, block0);
	while (node0.balance (key1.pub) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto block1 (system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, rai::genesis_amount / 3));
	ASSERT_NE (nullptr, block1);
	while (node0.balance (key2.pub) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::keypair key3;
	rai::keypair key4;
	auto block2 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, node0.latest (rai::test_genesis_key.pub), key3.pub, node0.balance (rai::test_genesis_key.pub), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	auto block3 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, node0.latest (rai::test_genesis_key.pub), key4.pub, node0.balance (rai::test_genesis_key.pub), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
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
		done |= node0.latest (rai::test_genesis_key.pub) == block2->hash () && node1.latest (rai::test_genesis_key.pub) == block2->hash () && node2.latest (rai::test_genesis_key.pub) == block2->hash ();
		done |= node0.latest (rai::test_genesis_key.pub) == block3->hash () && node1.latest (rai::test_genesis_key.pub) == block3->hash () && node2.latest (rai::test_genesis_key.pub) == block3->hash ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Sometimes hangs on the bootstrap_initiator.bootstrap call
TEST (node, DISABLED_fork_stale)
{
	rai::system system1 (24000, 1);
	system1.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::system system2 (24001, 1);
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	node2.peers.rep_response (node1.network.endpoint (), rai::test_genesis_key.pub, rai::genesis_amount);
	rai::genesis genesis;
	rai::keypair key1;
	rai::keypair key2;
	auto send3 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, genesis.hash (), rai::test_genesis_key.pub, rai::genesis_amount - rai::Mxrb_ratio, key1.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send3);
	node1.process_active (send3);
	system2.deadline_set (10s);
	while (node2.block (send3->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
	auto send1 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, send3->hash (), rai::test_genesis_key.pub, rai::genesis_amount - 2 * rai::Mxrb_ratio, key1.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, send3->hash (), rai::test_genesis_key.pub, rai::genesis_amount - 2 * rai::Mxrb_ratio, key2.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	{
		auto transaction1 (node1.store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction1, *send1).code);
		auto transaction2 (node2.store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, node2.ledger.process (transaction2, *send2).code);
	}
	node1.process_active (send1);
	node1.process_active (send2);
	node2.process_active (send1);
	node2.process_active (send2);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	while (node2.block (send1->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, broadcast_elected)
{
	rai::system system (24000, 3);
	auto node0 (system.nodes[0]);
	auto node1 (system.nodes[1]);
	auto node2 (system.nodes[2]);
	rai::keypair rep_big;
	rai::keypair rep_small;
	rai::keypair rep_other;
	//std::cerr << "Big: " << rep_big.pub.to_account () << std::endl;
	//std::cerr << "Small: " << rep_small.pub.to_account () << std::endl;
	//std::cerr << "Other: " << rep_other.pub.to_account () << std::endl;
	{
		auto transaction0 (node0->store.tx_begin (true));
		auto transaction1 (node1->store.tx_begin (true));
		auto transaction2 (node2->store.tx_begin (true));
		rai::send_block fund_big (node0->ledger.latest (transaction0, rai::test_genesis_key.pub), rep_big.pub, rai::Gxrb_ratio * 5, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::open_block open_big (fund_big.hash (), rep_big.pub, rep_big.pub, rep_big.prv, rep_big.pub, 0);
		rai::send_block fund_small (fund_big.hash (), rep_small.pub, rai::Gxrb_ratio * 2, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::open_block open_small (fund_small.hash (), rep_small.pub, rep_small.pub, rep_small.prv, rep_small.pub, 0);
		rai::send_block fund_other (fund_small.hash (), rep_other.pub, rai::Gxrb_ratio * 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::open_block open_other (fund_other.hash (), rep_other.pub, rep_other.pub, rep_other.prv, rep_other.pub, 0);
		node0->work_generate_blocking (fund_big);
		node0->work_generate_blocking (open_big);
		node0->work_generate_blocking (fund_small);
		node0->work_generate_blocking (open_small);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, fund_big).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, fund_big).code);
		ASSERT_EQ (rai::process_result::progress, node2->ledger.process (transaction2, fund_big).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, open_big).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, open_big).code);
		ASSERT_EQ (rai::process_result::progress, node2->ledger.process (transaction2, open_big).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, fund_small).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, fund_small).code);
		ASSERT_EQ (rai::process_result::progress, node2->ledger.process (transaction2, fund_small).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, open_small).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, open_small).code);
		ASSERT_EQ (rai::process_result::progress, node2->ledger.process (transaction2, open_small).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, fund_other).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, fund_other).code);
		ASSERT_EQ (rai::process_result::progress, node2->ledger.process (transaction2, fund_other).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, open_other).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, open_other).code);
		ASSERT_EQ (rai::process_result::progress, node2->ledger.process (transaction2, open_other).code);
	}
	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (1)->insert_adhoc (rep_small.prv);
	system.wallet (2)->insert_adhoc (rep_other.prv);
	auto fork0 (std::make_shared<rai::send_block> (node2->latest (rai::test_genesis_key.pub), rep_small.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node0->work_generate_blocking (*fork0);
	node0->process_active (fork0);
	node1->process_active (fork0);
	auto fork1 (std::make_shared<rai::send_block> (node2->latest (rai::test_genesis_key.pub), rep_big.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node0->work_generate_blocking (*fork1);
	system.wallet (2)->insert_adhoc (rep_small.prv);
	node2->process_active (fork1);
	//std::cerr << "fork0: " << fork_hash.to_string () << std::endl;
	//std::cerr << "fork1: " << fork1.hash ().to_string () << std::endl;
	while (!node0->ledger.block_exists (fork0->hash ()) || !node1->ledger.block_exists (fork0->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (50s);
	while (!node2->ledger.block_exists (fork0->hash ()))
	{
		auto ec = system.poll ();
		ASSERT_TRUE (node0->ledger.block_exists (fork0->hash ()));
		ASSERT_TRUE (node1->ledger.block_exists (fork0->hash ()));
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, rep_self_vote)
{
	rai::system system (24000, 1);
	auto node0 (system.nodes[0]);
	rai::keypair rep_big;
	{
		auto transaction0 (node0->store.tx_begin (true));
		rai::send_block fund_big (node0->ledger.latest (transaction0, rai::test_genesis_key.pub), rep_big.pub, rai::uint128_t ("0xb0000000000000000000000000000000"), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::open_block open_big (fund_big.hash (), rep_big.pub, rep_big.pub, rep_big.prv, rep_big.pub, 0);
		node0->work_generate_blocking (fund_big);
		node0->work_generate_blocking (open_big);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, fund_big).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, open_big).code);
	}
	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block0 (std::make_shared<rai::send_block> (node0->latest (rai::test_genesis_key.pub), rep_big.pub, rai::uint128_t ("0x60000000000000000000000000000000"), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node0->work_generate_blocking (*block0);
	ASSERT_EQ (rai::process_result::progress, node0->process (*block0).code);
	auto & active (node0->active);
	active.start (block0);
	auto existing (active.roots.find (block0->root ()));
	ASSERT_NE (active.roots.end (), existing);
	auto transaction (node0->store.tx_begin ());
	existing->election->compute_rep_votes (transaction);
	node0->vote_processor.flush ();
	auto & rep_votes (existing->election->last_votes);
	ASSERT_EQ (3, rep_votes.size ());
	ASSERT_NE (rep_votes.end (), rep_votes.find (rai::test_genesis_key.pub));
	ASSERT_NE (rep_votes.end (), rep_votes.find (rep_big.pub));
}

// Bootstrapping shouldn't republish the blocks to the network.
TEST (node, DISABLED_bootstrap_no_publish)
{
	rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	rai::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	rai::send_block send0 (system0.nodes[0]->latest (rai::test_genesis_key.pub), key0.pub, 500, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	{
		auto transaction (node0->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system0.nodes[0]->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	ASSERT_TRUE (node1->active.roots.empty ());
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		// Poll until the TCP connection is torn down and in_progress goes false
		system0.poll ();
		auto ec = system1.poll ();
		// There should never be an active transaction because the only activity is bootstrapping 1 block which shouldn't be publishing.
		ASSERT_TRUE (node1->active.roots.empty ());
		ASSERT_NO_ERROR (ec);
	}
}

// Check that an outgoing bootstrap request can push blocks
TEST (node, bootstrap_bulk_push)
{
	rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	rai::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	rai::send_block send0 (system0.nodes[0]->latest (rai::test_genesis_key.pub), key0.pub, 500, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	node0->work_generate_blocking (send0);
	{
		auto transaction (node0->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system0.nodes[0]->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.roots.empty ());
	node0->bootstrap_initiator.bootstrap (node1->network.endpoint (), false);
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// since this uses bulk_push, the new block should be republished
	ASSERT_FALSE (node1->active.roots.empty ());
}

// Bootstrapping a forked open block should succeed.
TEST (node, bootstrap_fork_open)
{
	rai::system system0 (24000, 2);
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto node0 (system0.nodes[0]);
	auto node1 (system0.nodes[1]);
	rai::keypair key0;
	rai::send_block send0 (system0.nodes[0]->latest (rai::test_genesis_key.pub), key0.pub, rai::genesis_amount - 500, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::open_block open0 (send0.hash (), 1, key0.pub, key0.prv, key0.pub, 0);
	rai::open_block open1 (send0.hash (), 2, key0.pub, key0.prv, key0.pub, 0);
	node0->work_generate_blocking (send0);
	node0->work_generate_blocking (open0);
	node0->work_generate_blocking (open1);
	{
		auto transaction0 (node0->store.tx_begin (true));
		auto transaction1 (node1->store.tx_begin (true));
		// Both know about send0
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, send0).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, send0).code);
		// They disagree about open0/open1
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction0, open0).code);
		ASSERT_EQ (rai::process_result::progress, node1->ledger.process (transaction1, open1).code);
	}
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	ASSERT_TRUE (node1->active.roots.empty ());
	system0.deadline_set (10s);
	while (node1->ledger.block_exists (open1.hash ()))
	{
		// Poll until the outvoted block is evicted.
		ASSERT_NO_ERROR (system0.poll ());
	}
}

// Test that if we create a block that isn't confirmed, we sync.
TEST (node, DISABLED_unconfirmed_send)
{
	rai::system system (24000, 2);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	rai::keypair key0;
	wallet1->insert_adhoc (key0.prv);
	wallet0->insert_adhoc (rai::test_genesis_key.prv);
	auto send1 (wallet0->send_action (rai::genesis_account, key0.pub, 2 * rai::Mxrb_ratio));
	system.deadline_set (10s);
	while (node1.balance (key0.pub) != 2 * rai::Mxrb_ratio || node1.bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto latest (node1.latest (key0.pub));
	rai::state_block send2 (key0.pub, latest, rai::genesis_account, rai::Mxrb_ratio, rai::genesis_account, key0.prv, key0.pub, node0.work_generate_blocking (latest));
	{
		auto transaction (node1.store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction, send2).code);
	}
	auto send3 (wallet1->send_action (key0.pub, rai::genesis_account, rai::Mxrb_ratio));
	system.deadline_set (10s);
	while (node0.balance (rai::genesis_account) != rai::genesis_amount)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Test that nodes can track nodes that have rep weight for priority broadcasting
TEST (node, rep_list)
{
	rai::system system (24000, 2);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node0 has a rep
	wallet0->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key1;
	// Broadcast a confirm so others should know this is a rep node
	wallet0->send_action (rai::test_genesis_key.pub, key1.pub, rai::Mxrb_ratio);
	ASSERT_EQ (0, node1.peers.representatives (1).size ());
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto reps (node1.peers.representatives (1));
		if (!reps.empty ())
		{
			if (reps[0].endpoint == node0.network.endpoint ())
			{
				if (!reps[0].rep_weight.is_zero ())
				{
					done = true;
				}
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Test that nodes can disable representative voting
TEST (node, no_voting)
{
	rai::system system (24000, 2);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	node0.config.enable_voting = false;
	// Node0 has a rep
	wallet0->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	// Broadcast a confirm so others should know this is a rep node
	wallet0->send_action (rai::test_genesis_key.pub, key1.pub, rai::Mxrb_ratio);
	system.deadline_set (10s);
	while (!node1.active.roots.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node1.stats.count (rai::stat::type::message, rai::stat::detail::confirm_ack, rai::stat::dir::in));
}

TEST (node, send_callback)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	system.nodes[0]->config.callback_address = "localhost";
	system.nodes[0]->config.callback_port = 8010;
	system.nodes[0]->config.callback_target = "/";
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (rai::test_genesis_key.pub));
}

// Check that votes get replayed back to nodes if they sent an old sequence number.
// This helps representatives continue from their last sequence number if their node is reinitialized and the old sequence number is lost
TEST (node, vote_replay)
{
	rai::system system (24000, 2);
	rai::keypair key;
	auto open (std::make_shared<rai::open_block> (0, 1, key.pub, key.prv, key.pub, 0));
	system.nodes[0]->work_generate_blocking (*open);
	for (auto i (0); i < 11000; ++i)
	{
		auto transaction (system.nodes[1]->store.tx_begin ());
		auto vote (system.nodes[1]->store.vote_generate (transaction, rai::test_genesis_key.pub, rai::test_genesis_key.prv, open));
	}
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		std::lock_guard<std::mutex> lock (boost::polymorphic_downcast<rai::mdb_store *> (system.nodes[0]->store_impl.get ())->cache_mutex);
		auto vote (system.nodes[0]->store.vote_current (transaction, rai::test_genesis_key.pub));
		ASSERT_EQ (nullptr, vote);
	}
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Gxrb_ratio));
	ASSERT_NE (nullptr, block);
	auto done (false);
	system.deadline_set (20s);
	while (!done)
	{
		auto ec = system.poll ();
		auto transaction (system.nodes[0]->store.tx_begin ());
		std::lock_guard<std::mutex> lock (boost::polymorphic_downcast<rai::mdb_store *> (system.nodes[0]->store_impl.get ())->cache_mutex);
		auto vote (system.nodes[0]->store.vote_current (transaction, rai::test_genesis_key.pub));
		done = vote && (vote->sequence >= 10000);
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, balance_observer)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	std::atomic<int> balances (0);
	rai::keypair key;
	node1.observers.account_balance.add ([&key, &balances](rai::account const & account_a, bool is_pending) {
		if (key.pub == account_a && is_pending)
		{
			balances++;
		}
		else if (rai::test_genesis_key.pub == account_a && !is_pending)
		{
			balances++;
		}
	});
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1);
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto ec = system.poll ();
		done = balances.load () == 2;
		ASSERT_NO_ERROR (ec);
	}
}

// ASSERT_NE (nullptr, attempt) sometimes fails
TEST (node, DISABLED_bootstrap_connection_scaling)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	node1.bootstrap_initiator.bootstrap ();
	auto attempt (node1.bootstrap_initiator.current_attempt ());
	ASSERT_NE (nullptr, attempt);
	ASSERT_EQ (34, attempt->target_connections (25000));
	ASSERT_EQ (4, attempt->target_connections (0));
	ASSERT_EQ (64, attempt->target_connections (50000));
	ASSERT_EQ (64, attempt->target_connections (10000000000));
	node1.config.bootstrap_connections = 128;
	ASSERT_EQ (64, attempt->target_connections (0));
	ASSERT_EQ (64, attempt->target_connections (50000));
	node1.config.bootstrap_connections_max = 256;
	ASSERT_EQ (128, attempt->target_connections (0));
	ASSERT_EQ (256, attempt->target_connections (50000));
	node1.config.bootstrap_connections_max = 0;
	ASSERT_EQ (1, attempt->target_connections (0));
	ASSERT_EQ (1, attempt->target_connections (50000));
}

// Test stat counting at both type and detail levels
TEST (node, stat_counting)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	node1.stats.add (rai::stat::type::ledger, rai::stat::dir::in, 1);
	node1.stats.add (rai::stat::type::ledger, rai::stat::dir::in, 5);
	node1.stats.inc (rai::stat::type::ledger, rai::stat::dir::in);
	node1.stats.inc (rai::stat::type::ledger, rai::stat::detail::send, rai::stat::dir::in);
	node1.stats.inc (rai::stat::type::ledger, rai::stat::detail::send, rai::stat::dir::in);
	node1.stats.inc (rai::stat::type::ledger, rai::stat::detail::receive, rai::stat::dir::in);
	ASSERT_EQ (10, node1.stats.count (rai::stat::type::ledger, rai::stat::dir::in));
	ASSERT_EQ (2, node1.stats.count (rai::stat::type::ledger, rai::stat::detail::send, rai::stat::dir::in));
	ASSERT_EQ (1, node1.stats.count (rai::stat::type::ledger, rai::stat::detail::receive, rai::stat::dir::in));
}

TEST (node, online_reps)
{
	rai::system system (24000, 2);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	ASSERT_EQ (system.nodes[1]->config.online_weight_minimum.number (), system.nodes[1]->online_reps.online_stake ());
	system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, rai::Gxrb_ratio);
	system.deadline_set (10s);
	while (system.nodes[1]->online_reps.online_stake () == system.nodes[1]->config.online_weight_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, block_confirm)
{
	rai::system system (24000, 2);
	rai::genesis genesis;
	rai::keypair key;
	system.wallet (1)->insert_adhoc (rai::test_genesis_key.prv);
	auto send1 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, genesis.hash (), rai::test_genesis_key.pub, rai::genesis_amount - rai::Gxrb_ratio, key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	system.nodes[0]->block_processor.add (send1, std::chrono::steady_clock::now ());
	system.nodes[1]->block_processor.add (send1, std::chrono::steady_clock::now ());
	system.deadline_set (std::chrono::seconds (5));
	while (!system.nodes[0]->ledger.block_exists (send1->hash ()) || !system.nodes[1]->ledger.block_exists (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (system.nodes[0]->ledger.block_exists (send1->hash ()));
	ASSERT_TRUE (system.nodes[1]->ledger.block_exists (send1->hash ()));
	auto send2 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, send1->hash (), rai::test_genesis_key.pub, rai::genesis_amount - rai::Gxrb_ratio * 2, key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (send1->hash ())));
	{
		auto transaction (system.nodes[0]->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system.nodes[0]->ledger.process (transaction, *send2).code);
	}
	{
		auto transaction (system.nodes[1]->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system.nodes[1]->ledger.process (transaction, *send2).code);
	}
	system.nodes[0]->block_confirm (send2);
	ASSERT_TRUE (system.nodes[0]->active.confirmed.empty ());
	system.deadline_set (10s);
	while (system.nodes[0]->active.confirmed.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, block_arrival)
{
	rai::system system (24000, 1);
	auto & node (*system.nodes[0]);
	ASSERT_EQ (0, node.block_arrival.arrival.size ());
	rai::block_hash hash1 (1);
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	rai::block_hash hash2 (2);
	node.block_arrival.add (hash2);
	ASSERT_EQ (2, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_size)
{
	rai::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now () - rai::block_arrival::arrival_time_min - std::chrono::seconds (5));
	rai::block_hash hash (0);
	for (auto i (0); i < rai::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.insert (rai::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (rai::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (rai::block_arrival::arrival_size_min, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_time)
{
	rai::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now ());
	rai::block_hash hash (0);
	for (auto i (0); i < rai::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.insert (rai::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (rai::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (rai::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
}

TEST (node, confirm_quorum)
{
	rai::system system (24000, 1);
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	// Put greater than online_weight_minimum in pending so quorum can't be reached
	rai::uint128_union new_balance (system.nodes[0]->config.online_weight_minimum.number () - rai::Gxrb_ratio);
	auto send1 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, genesis.hash (), rai::test_genesis_key.pub, new_balance, rai::test_genesis_key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	{
		auto transaction (system.nodes[0]->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system.nodes[0]->ledger.process (transaction, *send1).code);
	}
	system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, new_balance.number ());
	system.deadline_set (10s);
	while (system.nodes[0]->active.roots.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto done (false);
	while (!done)
	{
		ASSERT_FALSE (system.nodes[0]->active.roots.empty ());
		auto info (system.nodes[0]->active.roots.find (send1->hash ()));
		ASSERT_NE (system.nodes[0]->active.roots.end (), info);
		done = info->election->announcements > rai::active_transactions::announcement_min;
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, system.nodes[0]->balance (rai::test_genesis_key.pub));
}

TEST (node, vote_republish)
{
	rai::system system (24000, 2);
	rai::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[1]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->active.publish (send2);
	auto vote (std::make_shared<rai::vote> (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, send2));
	ASSERT_TRUE (system.nodes[0]->active.active (*send1));
	ASSERT_TRUE (system.nodes[1]->active.active (*send1));
	system.nodes[0]->vote_processor.vote (vote, system.nodes[0]->network.endpoint ());
	while (!system.nodes[0]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[1]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (system.nodes[0]->block (send1->hash ()));
	ASSERT_FALSE (system.nodes[1]->block (send1->hash ()));
	system.deadline_set (5s);
	while (system.nodes[1]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (system.nodes[0]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, vote_by_hash_republish)
{
	rai::system system (24000, 2);
	rai::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[1]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->active.publish (send2);
	std::vector<rai::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote (std::make_shared<rai::vote> (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, vote_blocks));
	ASSERT_TRUE (system.nodes[0]->active.active (*send1));
	ASSERT_TRUE (system.nodes[1]->active.active (*send1));
	system.nodes[0]->vote_processor.vote (vote, system.nodes[0]->network.endpoint ());
	while (!system.nodes[0]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[1]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (system.nodes[0]->block (send1->hash ()));
	ASSERT_FALSE (system.nodes[1]->block (send1->hash ()));
	system.deadline_set (5s);
	while (system.nodes[1]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (system.nodes[0]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, vote_by_hash_epoch_block_republish)
{
	rai::system system (24000, 2);
	rai::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	rai::keypair epoch_signer;
	system.nodes[0]->ledger.epoch_signer = epoch_signer.pub;
	system.nodes[1]->ledger.epoch_signer = epoch_signer.pub;
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto epoch1 (std::make_shared<rai::state_block> (rai::genesis_account, genesis.hash (), rai::genesis_account, rai::genesis_amount, system.nodes[0]->ledger.epoch_link, epoch_signer.prv, epoch_signer.pub, system.work.generate (genesis.hash ())));
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[1]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->active.publish (epoch1);
	std::vector<rai::block_hash> vote_blocks;
	vote_blocks.push_back (epoch1->hash ());
	auto vote (std::make_shared<rai::vote> (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, vote_blocks));
	ASSERT_TRUE (system.nodes[0]->active.active (*send1));
	ASSERT_TRUE (system.nodes[1]->active.active (*send1));
	system.nodes[0]->vote_processor.vote (vote, system.nodes[0]->network.endpoint ());
	while (!system.nodes[0]->block (epoch1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[1]->block (epoch1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (system.nodes[0]->block (send1->hash ()));
	ASSERT_FALSE (system.nodes[1]->block (send1->hash ()));
}

TEST (node, fork_invalid_block_signature)
{
	rai::system system (24000, 2);
	rai::keypair key2;
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2_corrupt (std::make_shared<rai::send_block> (*send2));
	send2_corrupt->signature = rai::signature (123);
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto vote (std::make_shared<rai::vote> (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, send2));
	auto vote_corrupt (std::make_shared<rai::vote> (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, send2_corrupt));
	system.nodes[1]->network.republish_vote (vote_corrupt);
	ASSERT_NO_ERROR (system.poll ());
	system.nodes[1]->network.republish_vote (vote);
	while (system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[0]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, fork_invalid_block_signature_vote_by_hash)
{
	rai::system system (24000, 1);
	rai::keypair key2;
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, std::numeric_limits<rai::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2_corrupt (std::make_shared<rai::send_block> (*send2));
	send2_corrupt->signature = rai::signature (123);
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->active.publish (send2_corrupt);
	ASSERT_NO_ERROR (system.poll ());
	system.nodes[0]->active.publish (send2);
	std::vector<rai::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote (std::make_shared<rai::vote> (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, vote_blocks));
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		std::unique_lock<std::mutex> lock (system.nodes[0]->active.mutex);
		system.nodes[0]->vote_processor.vote_blocking (transaction, vote, system.nodes[0]->network.endpoint ());
	}
	while (system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[0]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, block_processor_signatures)
{
	rai::system system0 (24000, 1);
	auto & node1 (*system0.nodes[0]);
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::block_hash latest (system0.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::keypair key1;
	auto send1 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, latest, rai::test_genesis_key.pub, rai::genesis_amount - rai::Gxrb_ratio, key1.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	rai::keypair key2;
	auto send2 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, send1->hash (), rai::test_genesis_key.pub, rai::genesis_amount - 2 * rai::Gxrb_ratio, key2.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	rai::keypair key3;
	auto send3 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, send2->hash (), rai::test_genesis_key.pub, rai::genesis_amount - 3 * rai::Gxrb_ratio, key3.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send3);
	// Invalid signature bit
	auto send4 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, send3->hash (), rai::test_genesis_key.pub, rai::genesis_amount - 4 * rai::Gxrb_ratio, key3.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send4);
	send4->signature.bytes[32] ^= 0x1;
	// Invalid signature bit (force)
	auto send5 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, send3->hash (), rai::test_genesis_key.pub, rai::genesis_amount - 5 * rai::Gxrb_ratio, key3.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send5);
	send5->signature.bytes[31] ^= 0x1;
	// Invalid signature to unchecked
	{
		auto transaction (node1.store.tx_begin_write ());
		node1.store.unchecked_put (transaction, send5->previous (), send5);
	}
	auto receive1 (std::make_shared<rai::state_block> (key1.pub, 0, rai::test_genesis_key.pub, rai::Gxrb_ratio, send1->hash (), key1.prv, key1.pub, 0));
	node1.work_generate_blocking (*receive1);
	auto receive2 (std::make_shared<rai::state_block> (key2.pub, 0, rai::test_genesis_key.pub, rai::Gxrb_ratio, send2->hash (), key2.prv, key2.pub, 0));
	node1.work_generate_blocking (*receive2);
	// Invalid private key
	auto receive3 (std::make_shared<rai::state_block> (key3.pub, 0, rai::test_genesis_key.pub, rai::Gxrb_ratio, send3->hash (), key2.prv, key3.pub, 0));
	node1.work_generate_blocking (*receive3);
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (receive1);
	node1.process_active (receive2);
	node1.process_active (receive3);
	node1.block_processor.flush ();
	node1.block_processor.force (send5);
	node1.block_processor.flush ();
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_TRUE (node1.store.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, send2->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, send3->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, send4->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, send5->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, receive1->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, receive2->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, receive3->hash ()));
}

/*
 *  State blocks go through a different signature path, ensure invalidly signed state blocks are rejected
 */
TEST (node, block_processor_reject_state)
{
	rai::system system (24000, 1);
	auto & node (*system.nodes[0]);
	rai::genesis genesis;
	auto send1 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, genesis.hash (), rai::test_genesis_key.pub, rai::genesis_amount - rai::Gxrb_ratio, rai::test_genesis_key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send1);
	send1->signature.bytes[0] ^= 1;
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	node.process_active (send1);
	node.block_processor.flush ();
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	auto send2 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, genesis.hash (), rai::test_genesis_key.pub, rai::genesis_amount - 2 * rai::Gxrb_ratio, rai::test_genesis_key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send2);
	node.process_active (send2);
	node.block_processor.flush ();
	ASSERT_TRUE (node.ledger.block_exists (send2->hash ()));
}

TEST (node, confirm_back)
{
	rai::system system (24000, 1);
	rai::keypair key;
	auto & node (*system.nodes[0]);
	rai::genesis genesis;
	auto genesis_start_balance (node.balance (rai::test_genesis_key.pub));
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key.pub, genesis_start_balance - 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto open (std::make_shared<rai::state_block> (key.pub, 0, key.pub, 1, send1->hash (), key.prv, key.pub, system.work.generate (key.pub)));
	auto send2 (std::make_shared<rai::state_block> (key.pub, open->hash (), key.pub, 0, rai::test_genesis_key.pub, key.prv, key.pub, system.work.generate (open->hash ())));
	node.process_active (send1);
	node.process_active (open);
	node.process_active (send2);
	node.block_processor.flush ();
	ASSERT_EQ (3, node.active.roots.size ());
	std::vector<rai::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote (std::make_shared<rai::vote> (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, vote_blocks));
	{
		auto transaction (node.store.tx_begin_read ());
		std::unique_lock<std::mutex> lock (node.active.mutex);
		node.vote_processor.vote_blocking (transaction, vote, node.network.endpoint ());
	}
	system.deadline_set (10s);
	while (!node.active.roots.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}
