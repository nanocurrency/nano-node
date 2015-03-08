#include <gtest/gtest.h>
#include <rai/node.hpp>
#include <rai/working.hpp>

TEST (node, stop)
{
    rai::system system (24000, 1);
    ASSERT_NE (system.nodes [0]->wallets.items.end (), system.nodes [0]->wallets.items.begin ());
    system.nodes [0]->stop ();
    system.processor.run ();
    system.service->run ();
    ASSERT_TRUE (true);
}

TEST (node, block_store_path_failure)
{
    rai::node_init init;
    rai::processor_service processor;
    auto service (boost::make_shared <boost::asio::io_service> ());
	rai::logging logging;
    auto node (std::make_shared <rai::node> (init, service, 0, rai::unique_path (), processor, logging));
	ASSERT_TRUE (node->wallets.items.empty ());
    node->stop ();
}

TEST (node, balance)
{
    rai::system system (24000, 1);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
    system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), system.wallet (0)->store.balance (transaction, system.nodes [0]->ledger));
}

TEST (node, send_unkeyed)
{
    rai::system system (24000, 1);
    rai::keypair key2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
		system.wallet (0)->store.password.value_set (rai::uint256_union (1));
	}
    ASSERT_TRUE (system.wallet (0)->send_all (key2.pub, 1000));
}

TEST (node, send_self)
{
    rai::system system (24000, 1);
    rai::keypair key2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
		system.wallet (0)->store.insert (transaction, key2.prv);
	}
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
    auto iterations (0);
	auto again (true);
    while (again)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = system.nodes [0]->ledger.account_balance (transaction, key2.pub).is_zero ();
    }
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
}

TEST (node, send_single)
{
    rai::system system (24000, 2);
    rai::keypair key2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
	}
	{
		rai::transaction transaction (system.nodes [1]->store.environment, nullptr, true);
		system.wallet (1)->store.insert (transaction, key2.prv);
	}
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
		ASSERT_TRUE (system.nodes [0]->ledger.account_balance (transaction, key2.pub).is_zero ());
	}
    auto iterations (0);
	auto again (true);
    while (again)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = system.nodes [0]->ledger.account_balance (transaction, key2.pub).is_zero ();
    }
}

TEST (node, send_single_observing_peer)
{
    rai::system system (24000, 3);
    rai::keypair key2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
	}
	{
		rai::transaction transaction (system.nodes [1]->store.environment, nullptr, true);
		system.wallet (1)->store.insert (transaction, key2.prv);
	}
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
		ASSERT_TRUE (system.nodes [0]->ledger.account_balance (transaction, key2.pub).is_zero ());
	}
    auto iterations (0);
	auto again (true);
    while (again)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = std::any_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a) {return node_a->ledger.account_balance (transaction, key2.pub).is_zero();});
    }
}

TEST (node, send_single_many_peers)
{
    rai::system system (24000, 10);
    rai::keypair key2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
	}
	{
		rai::transaction transaction (system.nodes [1]->store.environment, nullptr, true);
		system.wallet (1)->store.insert (transaction, key2.prv);
	}
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
		ASSERT_TRUE (system.nodes [0]->ledger.account_balance (transaction, key2.pub).is_zero ());
	}
    auto iterations (0);
	auto again (true);
    while (again)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 2000);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = std::any_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a) {return node_a->ledger.account_balance (transaction, key2.pub).is_zero();});
    }
}

TEST (node, send_out_of_order)
{
    rai::system system (24000, 2);
    rai::keypair key2;
    rai::genesis genesis;
    rai::send_block send1 (key2.pub, genesis.hash (), std::numeric_limits <rai::uint128_t>::max () - 1000, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (genesis.hash ()));
    rai::send_block send2 (key2.pub, send1.hash (), std::numeric_limits <rai::uint128_t>::max () - 2000, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (send1.hash ()));
    system.nodes [0]->process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (send2)));
    system.nodes [0]->process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (send1)));
    auto iterations (0);
	auto again (true);
    while (again)
    {
        system.service->poll_one ();
		system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = std::any_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a) {return node_a->ledger.account_balance (transaction, rai::test_genesis_key.pub) != rai::genesis_amount - 2000;});
    }
}

TEST (node, quick_confirm)
{
    rai::system system (24000, 1);
    rai::keypair key;
	rai::block_hash previous;
	uint64_t work;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, key.prv);
		previous = system.nodes [0]->ledger.latest (transaction, rai::test_genesis_key.pub);
		work = rai::work_generate (system.nodes [0]->ledger.latest (transaction, rai::test_genesis_key.pub));
	}
    rai::send_block send (key.pub, previous, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, work);
    ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process_receive (send));
    auto iterations (0);
	auto again (true);
    while (again)
    {
        system.processor.poll_one ();
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = system.nodes [0]->ledger.account_balance (transaction, key.pub).is_zero ();
    }
}

TEST (node, auto_bootstrap)
{
    rai::system system (24000, 1);
    rai::keypair key2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
		system.wallet (0)->store.insert (transaction, key2.prv);
	}
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
    auto iterations1 (0);
	auto again1 (true);
    while (again1)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again1 = system.nodes [0]->ledger.account_balance (transaction, key2.pub) != 100;
    }
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    ASSERT_FALSE (init1.error ());
    node1->network.send_keepalive (system.nodes [0]->network.endpoint ());
    node1->start ();
    ASSERT_FALSE (node1->bootstrap_initiator.warmed_up);
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress);
	ASSERT_FALSE (system.nodes [0]->bootstrap_initiator.warmed_up);
	ASSERT_FALSE (system.nodes [0]->bootstrap_initiator.in_progress);
    auto iterations2 (0);
	auto again2 (true);
    while (again2)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again2 = !node1->bootstrap_initiator.in_progress || !system.nodes [0]->bootstrap_initiator.in_progress;
	}
	ASSERT_TRUE (node1->bootstrap_initiator.warmed_up);
	ASSERT_TRUE (system.nodes [0]->bootstrap_initiator.warmed_up);
	auto iterations3 (0);
	auto again3 (true);
	while (again3)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
		++iterations3;
		ASSERT_LT (iterations3, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again3 = node1->ledger.account_balance (transaction, key2.pub) != 100;
	}
	auto iterations4 (0);
	auto again4 (true);
	while (again4)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
		++iterations4;
		ASSERT_LT (iterations4, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again4 = node1->bootstrap_initiator.in_progress || system.nodes [0]->bootstrap_initiator.in_progress;
	};
    node1->stop ();
}

TEST (node, auto_bootstrap_reverse)
{
    rai::system system (24000, 1);
    rai::keypair key2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
    system.wallet (0)->store.insert (transaction, key2.prv);
	}
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    ASSERT_FALSE (init1.error ());
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
    system.nodes [0]->network.send_keepalive (node1->network.endpoint ());
    node1->start ();
    auto iterations (0);
	auto again (true);
    while (again)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = node1->ledger.account_balance (transaction, key2.pub) != 100;
    }
    node1->stop ();
}

TEST (node, multi_account_send_atomicness)
{
    rai::system system (24000, 1);
    rai::keypair key1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
		system.wallet (0)->store.insert (transaction, key1.prv);
	}
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    system.wallet (0)->send_all (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2);
    system.wallet (0)->send_all (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2 + std::numeric_limits<rai::uint128_t>::max () / 4);
}

TEST (node, receive_gap)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    ASSERT_EQ (0, node1.gap_cache.blocks.size ());
    rai::send_block block (0, 1, 2, 3, 4, 5);
    rai::confirm_req message;
    message.block = block.clone ();
    node1.process_message (message, node1.network.endpoint ());
    ASSERT_EQ (1, node1.gap_cache.blocks.size ());
}

TEST (node, scaling)
{
    rai::system system (24000, 1);
    auto max (std::numeric_limits <rai::uint128_t>::max ());
    auto down (rai::scale_down (max));
    auto up1 (rai::scale_up (down));
    auto up2 (rai::scale_up (down - 1));
    ASSERT_LT (up2, up1);
    ASSERT_EQ (up1 - up2, rai::scale_64bit_base10);
}

TEST (node, scale_num)
{
    rai::system system (24000, 1);
    rai::uint128_t num ("60000000000000000000000000000000000000");
    auto down (rai::scale_down (num));
    auto up (rai::scale_up (down));
    ASSERT_EQ (num, up);
}

TEST (node, merge_peers)
{
	rai::system system (24000, 1);
	std::array <rai::endpoint, 8> endpoints;
	endpoints.fill (rai::endpoint (boost::asio::ip::address_v6::loopback (), 24000));
	endpoints [0] = rai::endpoint (boost::asio::ip::address_v6::loopback (), 24001);
	system.nodes [0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes [0]->peers.peers.size ());
}

TEST (node, search_pending)
{
    rai::system system (24000, 1);
    rai::keypair key2;
	rai::uint128_t balance;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
		balance = system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub);
	}
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
    auto iterations1 (0);
	auto again1 (true);
    while (again1)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again1 = system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub) == balance;
    }
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, key2.prv);
	}
	auto node (system.nodes [0]);
    node->background ([node] {node->search_pending ();});
    auto iterations2 (0);
	auto again2 (true);
    while (again2)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again2 = system.nodes [0]->ledger.account_balance (transaction, key2.pub).is_zero ();
    }
}

TEST (node, connect_after_junk)
{
    rai::system system (24000, 1);
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    uint64_t junk (0);
    node1->network.socket.async_send_to (boost::asio::buffer (&junk, sizeof (junk)), system.nodes [0]->network.endpoint (), [] (boost::system::error_code const &, size_t) {});
    auto iterations1 (0);
    while (system.nodes [0]->network.error_count == 0)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    node1->start ();
    node1->network.send_keepalive (system.nodes [0]->network.endpoint ());
    auto iterations2 (0);
    while (node1->peers.empty ())
    {
        system.service->poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
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
	rai::logging logging1;
	logging1.ledger_logging_value = !logging1.ledger_logging_value;
	logging1.ledger_duplicate_logging_value = !logging1.ledger_duplicate_logging_value;
	logging1.network_logging_value = !logging1.network_logging_value;
	logging1.network_message_logging_value = !logging1.network_message_logging_value;
	logging1.network_publish_logging_value = !logging1.network_publish_logging_value;
	logging1.network_packet_logging_value = !logging1.network_packet_logging_value;
	logging1.network_keepalive_logging_value = !logging1.network_keepalive_logging_value;
	logging1.node_lifetime_tracing_value = !logging1.node_lifetime_tracing_value;
	logging1.insufficient_work_logging_value = !logging1.insufficient_work_logging_value;
	logging1.log_rpc_value = !logging1.log_rpc_value;
	logging1.bulk_pull_logging_value = !logging1.bulk_pull_logging_value;
	logging1.work_generation_time_value = !logging1.work_generation_time_value;
	logging1.log_to_cerr_value = !logging1.log_to_cerr_value;
	boost::property_tree::ptree tree;
	logging1.serialize_json (tree);
	rai::logging logging2;
	ASSERT_FALSE (logging2.deserialize_json (tree));
	ASSERT_EQ (logging1.ledger_logging_value, logging2.ledger_logging_value);
	ASSERT_EQ (logging1.ledger_duplicate_logging_value, logging2.ledger_duplicate_logging_value);
	ASSERT_EQ (logging1.network_logging_value, logging2.network_logging_value);
	ASSERT_EQ (logging1.network_message_logging_value, logging2.network_message_logging_value);
	ASSERT_EQ (logging1.network_publish_logging_value, logging2.network_publish_logging_value);
	ASSERT_EQ (logging1.network_packet_logging_value, logging2.network_packet_logging_value);
	ASSERT_EQ (logging1.network_keepalive_logging_value, logging2.network_keepalive_logging_value);
	ASSERT_EQ (logging1.node_lifetime_tracing_value, logging2.node_lifetime_tracing_value);
	ASSERT_EQ (logging1.insufficient_work_logging_value, logging2.insufficient_work_logging_value);
	ASSERT_EQ (logging1.log_rpc_value, logging2.log_rpc_value);
	ASSERT_EQ (logging1.bulk_pull_logging_value, logging2.bulk_pull_logging_value);
	ASSERT_EQ (logging1.work_generation_time_value, logging2.work_generation_time_value);
	ASSERT_EQ (logging1.log_to_cerr_value, logging2.log_to_cerr_value);
}