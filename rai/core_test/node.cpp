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
    auto node (std::make_shared <rai::node> (init, service, 0, rai::unique_path (), processor));
	ASSERT_TRUE (node->wallets.items.empty ());
    node->stop ();
}

TEST (node, balance)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), system.wallet (0)->store.balance (system.nodes [0]->ledger));
}

TEST (node, send_unkeyed)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (0)->store.password.value_set (rai::uint256_union (1));
    ASSERT_TRUE (system.wallet (0)->send_all (key2.pub, 1000));
}

TEST (node, send_self)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (0)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
    auto iterations (0);
    while (system.nodes [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (node, send_single)
{
    rai::system system (24000, 2);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (1)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_TRUE (system.nodes [0]->ledger.account_balance (key2.pub).is_zero ());
    auto iterations (0);
    while (system.nodes [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (node, send_single_observing_peer)
{
    rai::system system (24000, 3);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (1)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_TRUE (system.nodes [0]->ledger.account_balance (key2.pub).is_zero ());
    auto iterations (0);
    while (std::any_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a) {return node_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (node, send_single_many_peers)
{
    rai::system system (24000, 10);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (1)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.nodes [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_TRUE (system.nodes [0]->ledger.account_balance (key2.pub).is_zero ());
    auto iterations (0);
    while (std::any_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a) {return node_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 2000);
    }
}

TEST (node, send_out_of_order)
{
    rai::system system (24000, 2);
    rai::keypair key2;
    rai::genesis genesis;
    rai::send_block send1;
    send1.hashables.balance = std::numeric_limits <rai::uint128_t>::max () - 1000;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = genesis.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
	rai::work_generate (send1);
    rai::send_block send2;
    send2.hashables.balance = std::numeric_limits <rai::uint128_t>::max () - 2000;
    send2.hashables.destination = key2.pub;
    send2.hashables.previous = send1.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
	rai::work_generate (send2);
    system.nodes [0]->processor.process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (send2)));
    system.nodes [0]->processor.process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (send1)));
    auto iterations (0);
    while (std::any_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a) {return node_a->ledger.account_balance (rai::test_genesis_key.pub) != rai::genesis_amount - 2000;}))
    {
        system.service->poll_one ();
		system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (node, quick_confirm)
{
    rai::system system (24000, 1);
    rai::keypair key;
    system.wallet (0)->store.insert (key.prv);
    rai::send_block send;
    send.hashables.balance = 0;
    send.hashables.destination = key.pub;
    send.hashables.previous = system.nodes [0]->ledger.latest (rai::test_genesis_key.pub);
    system.nodes [0]->work_create (send);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send.hash (), send.signature);
    ASSERT_EQ (rai::process_result::progress, system.nodes [0]->processor.process_receive (send));
    auto iterations (0);
    while (system.nodes [0]->ledger.account_balance (key.pub).is_zero ())
    {
        system.processor.poll_one ();
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (node, auto_bootstrap)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (0)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
    auto iterations1 (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    } while (system.nodes [0]->ledger.account_balance (key2.pub) != 100);
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    node1->network.send_keepalive (system.nodes [0]->network.endpoint ());
    node1->start ();
    ASSERT_FALSE (node1->bootstrap_initiator.warmed_up);
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress);
	ASSERT_FALSE (system.nodes [0]->bootstrap_initiator.warmed_up);
	ASSERT_FALSE (system.nodes [0]->bootstrap_initiator.in_progress);
    auto iterations2 (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
	} while (!node1->bootstrap_initiator.in_progress || !system.nodes [0]->bootstrap_initiator.in_progress);
	ASSERT_TRUE (node1->bootstrap_initiator.warmed_up);
	ASSERT_TRUE (system.nodes [0]->bootstrap_initiator.warmed_up);
	auto iterations3 (0);
	do
	{
		system.service->poll_one ();
		system.processor.poll_one ();
		++iterations3;
		ASSERT_LT (iterations3, 200);
	} while (node1->ledger.account_balance (key2.pub) != 100);
	auto iterations4 (0);
	do
	{
		system.service->poll_one ();
		system.processor.poll_one ();
		++iterations4;
		ASSERT_LT (iterations4, 200);
	} while (node1->bootstrap_initiator.in_progress || system.nodes [0]->bootstrap_initiator.in_progress);
    node1->stop ();
}

TEST (node, auto_bootstrap_reverse)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    rai::keypair key2;
    system.wallet (0)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
    system.nodes [0]->network.send_keepalive (node1->network.endpoint ());
    node1->start ();
    auto iterations (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    } while (node1->ledger.account_balance (key2.pub) != 100);
    node1->stop ();
}

TEST (node, multi_account_send_atomicness)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
    system.wallet (0)->store.insert (key1.prv);
    system.wallet (0)->send_all (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2);
    system.wallet (0)->send_all (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2 + std::numeric_limits<rai::uint128_t>::max () / 4);
}

TEST (node, receive_gap)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    ASSERT_EQ (0, node1.gap_cache.blocks.size ());
    rai::send_block block;
    rai::confirm_req message;
    message.block = block.clone ();
    node1.processor.process_message (message, node1.network.endpoint ());
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
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    auto balance (system.nodes [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 1000));
    auto iterations1 (0);
    while (system.nodes [0]->ledger.account_balance (rai::test_genesis_key.pub) == balance)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    system.wallet (0)->store.insert (key2.prv);
    system.nodes [0]->processor.search_pending ();
    auto iterations2 (0);
    while (system.nodes [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
}

TEST (node, connect_after_junk)
{
    rai::system system (24000, 1);
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, system.processor));
    uint64_t junk;
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