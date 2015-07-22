#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <rai/node.hpp>

TEST (network, tcp_connection)
{
    boost::asio::io_service service;
    boost::asio::ip::tcp::acceptor acceptor (service);
    boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), 24000);
    acceptor.open (endpoint.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (endpoint);
    acceptor.listen ();
    boost::asio::ip::tcp::socket incoming (service);
    auto done1 (false);
    std::string message1;
    acceptor.async_accept (incoming, 
       [&done1, &message1] (boost::system::error_code const & ec_a)
       {
           if (ec_a)
           {
               message1 = ec_a.message ();
               std::cerr << message1;
           }
           done1 = true;}
       );
    boost::asio::ip::tcp::socket connector (service);
    auto done2 (false);
    std::string message2;
    connector.async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 24000), 
        [&done2, &message2] (boost::system::error_code const & ec_a)
        {
            if (ec_a)
            {
                message2 = ec_a.message ();
                std::cerr << message2;
            }
            done2 = true;
        });
    while (!done1 || !done2)
    {
        service.poll_one ();
    }
    ASSERT_EQ (0, message1.size ());
    ASSERT_EQ (0, message2.size ());
}

TEST (network, construction)
{
    rai::system system (24000, 1);
    ASSERT_EQ (1, system.nodes.size ());
    ASSERT_EQ (24000, system.nodes [0]->network.socket.local_endpoint ().port ());
}

TEST (network, self_discard)
{
    rai::system system (24000, 1);
	system.nodes [0]->network.remote = system.nodes [0]->network.endpoint ();
	ASSERT_EQ (0, system.nodes [0]->network.bad_sender_count);
	system.nodes [0]->network.receive_action (boost::system::error_code {}, 0);
	ASSERT_EQ (1, system.nodes [0]->network.bad_sender_count);
}

TEST (network, send_keepalive)
{
    rai::system system (24000, 1);
    auto list1 (system.nodes [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    node1->start ();
    system.nodes [0]->network.send_keepalive (node1->network.endpoint ());
    auto initial (system.nodes [0]->network.keepalive_count);
    ASSERT_EQ (0, system.nodes [0]->peers.list ().size ());
    ASSERT_EQ (0, node1->peers.list ().size ());
    auto iterations (0);
    while (system.nodes [0]->network.keepalive_count == initial)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    auto peers1 (system.nodes [0]->peers.list ());
    auto peers2 (node1->peers.list ());
    ASSERT_EQ (1, peers1.size ());
    ASSERT_EQ (1, peers2.size ());
    ASSERT_NE (peers1.end (), std::find_if (peers1.begin (), peers1.end (), [&node1] (rai::peer_information const & information_a) {return information_a.endpoint == node1->network.endpoint ();}));
    ASSERT_NE (peers2.end (), std::find_if (peers2.begin (), peers2.end (), [&system] (rai::peer_information const & information_a) {return information_a.endpoint == system.nodes [0]->network.endpoint ();}));
    node1->stop ();
}

TEST (network, keepalive_ipv4)
{
    rai::system system (24000, 1);
    auto list1 (system.nodes [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    node1->start ();
    node1->send_keepalive (rai::endpoint (boost::asio::ip::address_v4::loopback (), 24000));
    auto initial (system.nodes [0]->network.keepalive_count);
    auto iterations (0);
    while (system.nodes [0]->network.keepalive_count == initial)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    node1->stop ();
}

TEST (network, multi_keepalive)
{
    rai::system system (24000, 1);
    auto list1 (system.nodes [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    ASSERT_FALSE (init1.error ());
    node1->start ();
    ASSERT_EQ (0, node1->peers.size ());
    node1->network.send_keepalive (system.nodes [0]->network.endpoint ());
    ASSERT_EQ (0, node1->peers.size ());
    ASSERT_EQ (0, system.nodes [0]->peers.size ());
    auto iterations1 (0);
    while (system.nodes [0]->peers.size () != 1)
    {
        system.poll ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    rai::node_init init2;
    auto node2 (std::make_shared <rai::node> (init2, system.service, 24002, rai::unique_path (), system.processor, system.logging));
    ASSERT_FALSE (init2.error ());
    node2->start ();
    node2->network.send_keepalive (system.nodes [0]->network.endpoint ());
    auto iterations2 (0);
    while (node1->peers.size () != 2 || system.nodes [0]->peers.size () != 2 || node2->peers.size () != 2)
    {
        system.poll ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
    node1->stop ();
    node2->stop ();
}

TEST (network, send_discarded_publish)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block (0, 1, 2, 3, 4, rai::work_generate (0)));
    system.nodes [0]->network.republish_block (std::move (block), 0);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.nodes [0]->latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.nodes [1]->latest (rai::test_genesis_key.pub));
    auto iterations (0);
    while (system.nodes [1]->network.publish_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (genesis.hash (), system.nodes [0]->latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.nodes [1]->latest (rai::test_genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block (0, 1, 20, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (0)));
    system.nodes [0]->network.republish_block (std::move (block), 0);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.nodes [0]->latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.nodes [1]->latest (rai::test_genesis_key.pub));
    auto iterations (0);
    while (system.nodes [1]->network.publish_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (genesis.hash (), system.nodes [0]->latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.nodes [1]->latest (rai::test_genesis_key.pub));
}

TEST (network, send_valid_confirm_ack)
{
    rai::system system (24000, 2);
    rai::keypair key2;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (1)->insert (key2.prv);
    rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
    rai::send_block block2 (latest1, key2.pub, 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (latest1));
    auto hash2 (block2.hash ());
    rai::block_hash latest2 (system.nodes [1]->latest (rai::test_genesis_key.pub));
	system.nodes [0]->process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (block2)), 0);
    auto iterations (0);
    while (system.nodes [1]->network.confirm_ack_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    rai::block_hash latest3 (system.nodes [1]->latest (rai::test_genesis_key.pub));
    ASSERT_NE (latest2, latest3);
    ASSERT_EQ (hash2, latest3);
    ASSERT_EQ (50, system.nodes [1]->balance (rai::test_genesis_key.pub));
}

TEST (network, send_valid_publish)
{
    rai::system system (24000, 2);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::keypair key2;
	system.wallet (1)->insert (key2.prv);
    rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
    rai::send_block block2 (latest1, key2.pub, 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (latest1));
    auto hash2 (block2.hash ());
    rai::block_hash latest2 (system.nodes [1]->latest (rai::test_genesis_key.pub));
    system.nodes [1]->process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (block2)), 0);
    auto iterations (0);
    while (system.nodes [0]->network.publish_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    rai::block_hash latest3 (system.nodes [1]->latest (rai::test_genesis_key.pub));
    ASSERT_NE (latest2, latest3);
    ASSERT_EQ (hash2, latest3);
    ASSERT_EQ (50, system.nodes [1]->balance (rai::test_genesis_key.pub));
}

TEST (network, send_insufficient_work)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block (0, 1, 20, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
    rai::publish publish (std::move (block));
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        publish.serialize (stream);
    }
    auto node1 (system.nodes [1]->shared ());
    system.nodes [0]->network.send_buffer (bytes->data (), bytes->size (), system.nodes [1]->network.endpoint (), 0, [bytes, node1] (boost::system::error_code const & ec, size_t size) {});
    ASSERT_EQ (0, system.nodes [0]->network.insufficient_work_count);
    auto iterations (0);
    while (system.nodes [1]->network.insufficient_work_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (1, system.nodes [1]->network.insufficient_work_count);
}

TEST (receivable_processor, confirm_insufficient_pos)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::send_block block1 (genesis.hash (), 0, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, node1.process (block1).code);
	auto node_l (system.nodes [0]);
    node1.conflicts.start (block1, [node_l] (rai::block & block_a)
	{
		node_l->process_confirmed (block_a);
	}, true);
    rai::keypair key1;
    rai::confirm_ack con1 (key1.pub, key1.prv, 0, block1.clone ());
	node1.process_message (con1, node1.network.endpoint ());
}

TEST (receivable_processor, confirm_sufficient_pos)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::send_block block1 (genesis.hash (), 0, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, node1.process (block1).code);
	auto node_l (system.nodes [0]);
    node1.conflicts.start (block1, [node_l] (rai::block & block_a)
	{
		node_l->process_confirmed (block_a);
	}, true);
    rai::confirm_ack con1 (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 0, block1.clone ());
	node1.process_message (con1, node1.network.endpoint ());
}

TEST (receivable_processor, send_with_receive)
{
    auto amount (std::numeric_limits <rai::uint128_t>::max ());
    rai::system system (24000, 2);
    rai::keypair key2;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	system.wallet (1)->insert (key2.prv);
    auto block1 (new rai::send_block (latest1, key2.pub, amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (latest1)));
	ASSERT_EQ (amount, system.nodes [0]->balance (rai::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes [0]->balance (key2.pub));
	ASSERT_EQ (amount, system.nodes [1]->balance (rai::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes [1]->balance (key2.pub));
    system.nodes [0]->process_receive_republish (block1->clone (), 0);
    system.nodes [1]->process_receive_republish (block1->clone (), 0);
	ASSERT_EQ (amount - 100, system.nodes [0]->balance (rai::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes [0]->balance (key2.pub));
	ASSERT_EQ (amount - 100, system.nodes [1]->balance (rai::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes [1]->balance (key2.pub));
    auto iterations (0);
    while (system.nodes [0]->balance (key2.pub) != 100)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
	ASSERT_EQ (amount - 100, system.nodes [0]->balance (rai::test_genesis_key.pub));
	ASSERT_EQ (100, system.nodes [0]->balance (key2.pub));
	ASSERT_EQ (amount - 100, system.nodes [1]->balance (rai::test_genesis_key.pub));
	ASSERT_EQ (100, system.nodes [1]->balance (key2.pub));
}

TEST (network, receive_weight_change)
{
    rai::system system (24000, 2);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::keypair key2;
	system.wallet (1)->insert (key2.prv);
	{
		rai::transaction transaction (system.nodes [1]->store.environment, nullptr, true);
		system.wallet (1)->store.representative_set (transaction, key2.pub);
	}
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 2));
	auto iterations (0);
    while (std::any_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a){ return node_a->weight (key2.pub) != 2;}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
		++iterations;
		ASSERT_LT (iterations, 200);
    }
}

TEST (parse_endpoint, valid)
{
    std::string string ("127.0.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_FALSE (rai::parse_endpoint (string, endpoint));
    ASSERT_EQ (boost::asio::ip::address_v4::loopback (), endpoint.address ());
    ASSERT_EQ (24000, endpoint.port ());
}

TEST (parse_endpoint, invalid_port)
{
    std::string string ("127.0.0.1:24a00");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, invalid_address)
{
    std::string string ("127.0q.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, nothing)
{
    std::string string ("127.0q.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_address)
{
    std::string string (":24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_port)
{
    std::string string ("127.0.0.1:");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_colon)
{
    std::string string ("127.0.0.1");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

// If the account doesn't exist, current == end so there's no iteration
TEST (bulk_pull, no_address)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull);
    req->start = 1;
    req->end = 2;
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_TRUE (request->current.is_zero ());
}

TEST (bulk_pull, genesis_to_end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = rai::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (system.nodes [0]->latest (rai::test_genesis_key.pub), request->current);
    ASSERT_EQ (request->request->end, request->request->end);
}

// If we can't find the end block, send everything
TEST (bulk_pull, no_end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = rai::test_genesis_key.pub;
    req->end = 1;
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (system.nodes [0]->latest (rai::test_genesis_key.pub), request->current);
	ASSERT_TRUE (request->request->end.is_zero ());
}

TEST (bulk_pull, end_not_owned)
{
    rai::system system (24000, 1);
    rai::keypair key2;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
	rai::block_hash latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
    rai::open_block open (0, 1, 2, 3, 4, 5);
    open.hashables.account = key2.pub;
    open.hashables.representative = key2.pub;
    open.hashables.source = latest;
    open.signature = rai::sign_message (key2.prv, key2.pub, open.hash ());
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (open).code);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    rai::genesis genesis;
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = key2.pub;
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk_pull, none)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    rai::genesis genesis;
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = genesis.hash ();
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_EQ (nullptr, block);
}

TEST (bulk_pull, get_next_on_open)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = rai::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_NE (nullptr, block);
    ASSERT_TRUE (block->previous ().is_zero ());
    ASSERT_FALSE (connection->requests.empty ());
    ASSERT_FALSE (request->current.is_zero ());
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bootstrap_processor, DISABLED_process_none)
{
    rai::system system (24000, 1);
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    ASSERT_FALSE (init1.error ());
    auto done (false);
	node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
    while (!done)
    {
        system.service->run_one ();
    }
    node1->stop ();
}

TEST (bootstrap_processor, DISABLED_process_incomplete)
{
    rai::system system (24000, 1);
    auto node1 (std::make_shared <rai::bootstrap_client> (system.nodes [0]));
    rai::genesis genesis;
    auto frontier_req_client (std::make_shared <rai::frontier_req_client> (node1));
    frontier_req_client->pulls [rai::test_genesis_key.pub] = genesis.hash ();
    auto bulk_pull_client (std::make_shared <rai::bulk_pull_client> (frontier_req_client));
    std::unique_ptr <rai::send_block> block1 (new rai::send_block (0, 1, 2, 3, 4, 5));
    bulk_pull_client->process_end ();
}

// Bootstrap can pull one basic block
TEST (bootstrap_processor, process_one)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	ASSERT_FALSE (system.wallet (0)->send_all (rai::test_genesis_key.pub, 100));
	rai::node_init init1;
	auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
	rai::block_hash hash1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	rai::block_hash hash2 (node1->latest (rai::test_genesis_key.pub));
	ASSERT_NE (hash1, hash2);
	node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
	auto iterations (0);
	ASSERT_NE (node1->latest (rai::test_genesis_key.pub), system.nodes [0]->latest (rai::test_genesis_key.pub));
	while (node1->latest (rai::test_genesis_key.pub) != system.nodes [0]->latest (rai::test_genesis_key.pub))
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
    node1->stop ();
}

TEST (bootstrap_processor, process_two)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	rai::block_hash hash1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (system.wallet (0)->send_all (rai::test_genesis_key.pub, 50));
	rai::block_hash hash2 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (system.wallet (0)->send_all (rai::test_genesis_key.pub, 50));
	rai::block_hash hash3 (system.nodes [0]->latest ( rai::test_genesis_key.pub));
	ASSERT_NE (hash1, hash2);
	ASSERT_NE (hash1, hash3);
	ASSERT_NE (hash2, hash3);
	rai::node_init init1;
	auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
	ASSERT_FALSE (init1.error ());
	node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
	auto iterations (0);
	ASSERT_NE (node1->latest (rai::test_genesis_key.pub), system.nodes [0]->latest (rai::test_genesis_key.pub));
	while (node1->latest (rai::test_genesis_key.pub) != system.nodes [0]->latest (rai::test_genesis_key.pub))
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	node1->stop ();
}

TEST (bootstrap_processor, process_new)
{
	rai::system system (24000, 2);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	rai::keypair key2;
	system.wallet (1)->insert (key2.prv);
	ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
	auto iterations1 (0);
	while (system.nodes [0]->balance (key2.pub).is_zero ())
	{
		system.poll ();
		++iterations1;
		ASSERT_LT (iterations1, 200);
	}
	rai::uint128_t balance1 (system.nodes [0]->balance (rai::test_genesis_key.pub));
	rai::uint128_t balance2 (system.nodes [0]->balance (key2.pub));
	rai::node_init init1;
	auto node1 (std::make_shared <rai::node> (init1, system.service, 24002, rai::unique_path (), system.processor, system.logging));
	ASSERT_FALSE (init1.error ());
	node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
	auto iterations2 (0);
	while (node1->balance (key2.pub) != balance2)
	{
	        system.poll ();
		++iterations2;
		ASSERT_LT (iterations2, 200);
	}
	ASSERT_EQ (balance1, node1->balance (rai::test_genesis_key.pub));
	node1->stop ();
}

TEST (bootstrap_processor, pull_diamond)
{
	rai::system system (24000, 1);
	rai::keypair key;
	std::unique_ptr <rai::send_block> send1 (new rai::send_block (system.nodes [0]->latest (rai::test_genesis_key.pub), key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (system.nodes [0]->latest (rai::test_genesis_key.pub))));
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (*send1).code);
	std::unique_ptr <rai::open_block> open (new rai::open_block (send1->hash (), 1, key.pub, key.prv, key.pub, rai::work_generate (key.pub)));
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (*open).code);
	std::unique_ptr <rai::send_block> send2 (new rai::send_block (open->hash (), rai::test_genesis_key.pub, std::numeric_limits <rai::uint128_t>::max () - 100, key.prv, key.pub, rai::work_generate (open->hash ())));
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (*send2).code);
	std::unique_ptr <rai::receive_block> receive (new rai::receive_block (send1->hash (), send2->hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (send1->hash ())));
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (*receive).code);
	rai::node_init init1;
	auto node1 (std::make_shared <rai::node> (init1, system.service, 24002, rai::unique_path (), system.processor, system.logging));
	ASSERT_FALSE (init1.error ());
	node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
	auto iterations (0);
	while (node1->balance (rai::test_genesis_key.pub) != 100)
	{
	    system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	ASSERT_EQ (100, node1->balance (rai::test_genesis_key.pub));
	node1->stop ();
}

TEST (bootstrap_processor, push_diamond)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::node_init init1;
	auto node1 (std::make_shared <rai::node> (init1, system.service, 24002, rai::unique_path (), system.processor, system.logging));
	ASSERT_FALSE (init1.error ());
	std::unique_ptr <rai::send_block> send1 (new rai::send_block (system.nodes [0]->latest (rai::test_genesis_key.pub), key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (system.nodes [0]->latest (rai::test_genesis_key.pub))));
	ASSERT_EQ (rai::process_result::progress, node1->process (*send1).code);
	std::unique_ptr <rai::open_block> open (new rai::open_block (send1->hash (), 1, key.pub, key.prv, key.pub, rai::work_generate (key.pub)));
	ASSERT_EQ (rai::process_result::progress, node1->process (*open).code);
	std::unique_ptr <rai::send_block> send2 (new rai::send_block (open->hash (), rai::test_genesis_key.pub, std::numeric_limits <rai::uint128_t>::max () - 100, key.prv, key.pub, rai::work_generate (open->hash ())));
	ASSERT_EQ (rai::process_result::progress, node1->process (*send2).code);
	std::unique_ptr <rai::receive_block> receive (new rai::receive_block (send1->hash (), send2->hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (send1->hash ())));
	ASSERT_EQ (rai::process_result::progress, node1->process (*receive).code);
	node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
	auto iterations (0);
	while (system.nodes [0]->balance (rai::test_genesis_key.pub) != 100)
	{
	    system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	ASSERT_EQ (100, system.nodes [0]->balance (rai::test_genesis_key.pub));
	node1->stop ();
}

TEST (bootstrap_processor, push_one)
{
    rai::system system (24000, 1);
    rai::node_init init1;
    rai::keypair key1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    auto wallet (node1->wallets.create (rai::uint256_union ()));
    ASSERT_NE (nullptr, wallet);
	wallet->insert (rai::test_genesis_key.prv);
    rai::uint128_t balance1 (node1->balance (rai::test_genesis_key.pub));
    ASSERT_FALSE (wallet->send_all (key1.pub, 100));
	ASSERT_NE (balance1, node1->balance (rai::test_genesis_key.pub));
    node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
    auto iterations (0);
    while (system.nodes [0]->balance (rai::test_genesis_key.pub) == balance1)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    node1->stop ();
}

TEST (frontier_req_response, DISABLED_destruction)
{
    {
        std::shared_ptr <rai::frontier_req_server> hold; // Destructing tcp acceptor on non-existent io_service
        {
            rai::system system (24000, 1);
            auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
            std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
            req->start.clear ();
            req->age = std::numeric_limits <decltype (req->age)>::max ();
            req->count = std::numeric_limits <decltype (req->count)>::max ();
            connection->requests.push (std::unique_ptr <rai::message> {});
            hold = std::make_shared <rai::frontier_req_server> (connection, std::move (req));
        }
    }
    ASSERT_TRUE (true);
}

TEST (frontier_req, begin)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_EQ (rai::test_genesis_key.pub, request->current);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), request->info.head);
}

TEST (frontier_req, end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start = rai::test_genesis_key.pub.number () + 1;
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_TRUE (request->current.is_zero ());
}

TEST (frontier_req, time_bound)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = 0;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_TRUE (request->current.is_zero ());
}

TEST (frontier_req, time_cutoff)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.nodes [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = 10;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_EQ (rai::test_genesis_key.pub, request->current);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), request->info.head);
}

TEST (bulk, genesis)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    ASSERT_FALSE (init1.error ());
    rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	rai::block_hash latest2 (node1->latest (rai::test_genesis_key.pub));
    ASSERT_EQ (latest1, latest2);
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
    rai::block_hash latest3 (system.nodes [0]->latest (rai::test_genesis_key.pub));
    ASSERT_NE (latest1, latest3);
    node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
    auto iterations (0);
    while (node1->latest (rai::test_genesis_key.pub) != system.nodes [0]->latest (rai::test_genesis_key.pub))
    {
	system.poll ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (node1->latest (rai::test_genesis_key.pub), system.nodes [0]->latest (rai::test_genesis_key.pub));
    node1->stop ();
}

TEST (bulk, offline_send)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
    ASSERT_FALSE (init1.error ());
    node1->network.send_keepalive (system.nodes [0]->network.endpoint ());
    node1->start ();
    auto iterations (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    } while (system.nodes [0]->peers.empty () || node1->peers.empty ());
    rai::keypair key2;
	auto wallet (node1->wallets.create (rai::uint256_union ()));
	wallet->insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 100));
	ASSERT_NE (std::numeric_limits <rai::uint256_t>::max (), system.nodes [0]->balance (rai::test_genesis_key.pub));
    node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
    auto iterations2 (0);
    while (node1->balance (key2.pub) != 100)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
	node1->stop ();
}

TEST (network, ipv6)
{
    boost::asio::ip::address_v6 address (boost::asio::ip::address_v6::from_string ("::ffff:127.0.0.1"));
    ASSERT_TRUE (address.is_v4_mapped ());
    boost::asio::ip::udp::endpoint endpoint1 (address, 16384);
    std::vector <uint8_t> bytes1;
    {
        rai::vectorstream stream (bytes1);
        rai::write (stream, address.to_bytes ());
    }
    ASSERT_EQ (16, bytes1.size ());
    for (auto i (bytes1.begin ()), n (bytes1.begin () + 10); i != n; ++i)
    {
        ASSERT_EQ (0, *i);
    }
    ASSERT_EQ (0xff, bytes1 [10]);
    ASSERT_EQ (0xff, bytes1 [11]);
    std::array <uint8_t, 16> bytes2;
    rai::bufferstream stream (bytes1.data (), bytes1.size ());
    rai::read (stream, bytes2);
    boost::asio::ip::udp::endpoint endpoint2 (boost::asio::ip::address_v6 (bytes2), 16384);
    ASSERT_EQ (endpoint1, endpoint2);
}

TEST (network, ipv6_from_ipv4)
{
    boost::asio::ip::udp::endpoint endpoint1 (boost::asio::ip::address_v4::loopback(), 16000);
    ASSERT_TRUE (endpoint1.address ().is_v4 ());
    boost::asio::ip::udp::endpoint endpoint2 (boost::asio::ip::address_v6::v4_mapped (endpoint1.address ().to_v4 ()), 16000);
    ASSERT_TRUE (endpoint2.address ().is_v6 ());
}

TEST (network, ipv6_bind_send_ipv4)
{
    boost::asio::io_service service;
    boost::asio::ip::udp::endpoint endpoint1 (boost::asio::ip::address_v6::any (), 24000);
    boost::asio::ip::udp::endpoint endpoint2 (boost::asio::ip::address_v4::any (), 24001);
    std::array <uint8_t, 16> bytes1;
    auto finish1 (false);
    boost::asio::ip::udp::endpoint endpoint3;
    boost::asio::ip::udp::socket socket1 (service, endpoint1);
    socket1.async_receive_from (boost::asio::buffer (bytes1.data (), bytes1.size ()), endpoint3, [&finish1] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (error);
        ASSERT_EQ (16, size_a);
        finish1 = true;
    });
    boost::asio::ip::udp::socket socket2 (service, endpoint2);
    boost::asio::ip::udp::endpoint endpoint5 (boost::asio::ip::address_v4::loopback (), 24000);
    boost::asio::ip::udp::endpoint endpoint6 (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4::loopback ()), 24001);
    socket2.async_send_to (boost::asio::buffer (std::array <uint8_t, 16> {}, 16), endpoint5, [] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (error);
        ASSERT_EQ (16, size_a);
    });
    while (!finish1)
    {
        service.poll_one ();
    }
    ASSERT_EQ (endpoint6, endpoint3);
    std::array <uint8_t, 16> bytes2;
    auto finish2 (false);
    boost::asio::ip::udp::endpoint endpoint4;
    socket2.async_receive_from (boost::asio::buffer (bytes2.data (), bytes2.size ()), endpoint4, [&finish2] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (!error);
        ASSERT_EQ (16, size_a);
    });
    socket1.async_send_to (boost::asio::buffer (std::array <uint8_t, 16> {}, 16), endpoint6, [] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (error);
        ASSERT_EQ (16, size_a);
    });
}

TEST (bootstrap_processor, unchecked_only)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging));
	rai::send_block block1 (node1->latest (rai::test_genesis_key.pub), rai::test_genesis_key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	node1->store.unchecked_put (rai::transaction (node1->store.environment, nullptr, true), block1.hash (), block1);
	node1->bootstrap_initiator.bootstrap (system.nodes [0]->network.endpoint ());
	ASSERT_EQ (block1.previous (), node1->latest (rai::test_genesis_key.pub));
	auto iterations (0);
	while (node1->latest (rai::test_genesis_key.pub) == block1.previous ())
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
    node1->stop ();
}
