#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/socket.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/transport/udp.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/iostreams/stream_buffer.hpp>
#include <boost/range/join.hpp>
#include <boost/thread.hpp>

using namespace std::chrono_literals;

TEST (network, tcp_connection)
{
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor (io_ctx);
	auto port = nano::test::get_available_port ();
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), port);
	acceptor.open (endpoint.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
	acceptor.bind (endpoint);
	acceptor.listen ();
	boost::asio::ip::tcp::socket incoming (io_ctx);
	std::atomic<bool> done1 (false);
	std::string message1;
	acceptor.async_accept (incoming,
	[&done1, &message1] (boost::system::error_code const & ec_a) {
		   if (ec_a)
		   {
			   message1 = ec_a.message ();
			   std::cerr << message1;
		   }
		   done1 = true; });
	boost::asio::ip::tcp::socket connector (io_ctx);
	std::atomic<bool> done2 (false);
	std::string message2;
	connector.async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), acceptor.local_endpoint ().port ()),
	[&done2, &message2] (boost::system::error_code const & ec_a) {
		if (ec_a)
		{
			message2 = ec_a.message ();
			std::cerr << message2;
		}
		done2 = true;
	});
	while (!done1 || !done2)
	{
		io_ctx.poll ();
	}
	ASSERT_EQ (0, message1.size ());
	ASSERT_EQ (0, message2.size ());
}

TEST (network, construction_with_specified_port)
{
	nano::test::system system{};
	auto const port = nano::test::get_available_port ();
	auto const node = system.add_node (nano::node_config{ port, system.logging });
	EXPECT_EQ (port, node->network.port);
	EXPECT_EQ (port, node->network.endpoint ().port ());
	EXPECT_EQ (port, node->tcp_listener.endpoint ().port ());
}

TEST (network, construction_without_specified_port)
{
	nano::test::system system{};
	auto const node = system.add_node ();
	auto const port = node->network.port.load ();
	EXPECT_NE (0, port);
	EXPECT_EQ (port, node->network.endpoint ().port ());
	EXPECT_EQ (port, node->tcp_listener.endpoint ().port ());
}

TEST (network, self_discard)
{
	nano::node_flags node_flags;
	node_flags.disable_udp = false;
	nano::test::system system (1, nano::transport::transport_type::tcp, node_flags);
	nano::message_buffer data;
	data.endpoint = system.nodes[0]->network.endpoint ();
	ASSERT_EQ (0, system.nodes[0]->stats.count (nano::stat::type::error, nano::stat::detail::bad_sender));
	system.nodes[0]->network.udp_channels.receive_action (&data);
	ASSERT_EQ (1, system.nodes[0]->stats.count (nano::stat::type::error, nano::stat::detail::bad_sender));
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3611
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3612
TEST (network, DISABLED_send_node_id_handshake)
{
	nano::node_flags node_flags;
	node_flags.disable_udp = false;
	nano::test::system system;
	auto node0 = system.add_node (node_flags);
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work, node_flags));
	node1->start ();
	system.nodes.push_back (node1);
	auto initial (node0->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in));
	auto initial_node1 (node1->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in));
	auto channel (std::make_shared<nano::transport::channel_udp> (node0->network.udp_channels, node1->network.endpoint (), node1->network_params.network.protocol_version));
	node0->network.send_keepalive (channel);
	ASSERT_EQ (0, node0->network.size ());
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_TIMELY (10s, node1->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in) != initial_node1);
	ASSERT_TIMELY (10s, node0->network.size () == 0 || node1->network.size () == 1);
	ASSERT_TIMELY (10s, node0->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in) == initial + 2);
	ASSERT_TIMELY (10s, node0->network.size () == 1 || node1->network.size () == 1);
	auto list1 (node0->network.list (1));
	ASSERT_EQ (node1->network.endpoint (), list1[0]->get_endpoint ());
	auto list2 (node1->network.list (1));
	ASSERT_EQ (node0->network.endpoint (), list2[0]->get_endpoint ());
	node1->stop ();
}

TEST (network, send_node_id_handshake_tcp)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work));
	node1->start ();
	system.nodes.push_back (node1);
	auto initial (node0->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in));
	auto initial_node1 (node1->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in));
	auto initial_keepalive (node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
	std::weak_ptr<nano::node> node_w (node0);
	node0->network.tcp_channels.start_tcp (node1->network.endpoint ());
	ASSERT_EQ (0, node0->network.size ());
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_TIMELY (10s, node0->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in) >= initial + 2);
	ASSERT_TIMELY (5s, node1->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in) >= initial_node1 + 2);
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in) >= initial_keepalive + 2);
	ASSERT_TIMELY (5s, node1->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in) >= initial_keepalive + 2);
	ASSERT_EQ (1, node0->network.size ());
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node0->network.list (1));
	ASSERT_EQ (nano::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (node1->get_node_id (), list1[0]->get_node_id ());
	auto list2 (node1->network.list (1));
	ASSERT_EQ (nano::transport::transport_type::tcp, list2[0]->get_type ());
	ASSERT_EQ (node0->get_node_id (), list2[0]->get_node_id ());
	node1->stop ();
}

TEST (network, last_contacted)
{
	nano::test::system system (1);

	auto node0 = system.nodes[0];
	ASSERT_EQ (0, node0->network.size ());

	nano::node_config node1_config (nano::test::get_available_port (), system.logging);
	node1_config.tcp_incoming_connections_max = 0; // Prevent ephemeral node1->node0 channel repacement with incoming connection
	auto node1 (std::make_shared<nano::node> (system.io_ctx, nano::unique_path (), node1_config, system.work));
	node1->start ();
	system.nodes.push_back (node1);

	auto channel1 = nano::test::establish_tcp (system, *node1, node0->network.endpoint ());
	ASSERT_NE (nullptr, channel1);
	ASSERT_TIMELY (3s, node0->network.size () == 1);

	// channel0 is the other side of channel1, same connection different endpoint
	auto channel0 = node0->network.tcp_channels.find_node_id (node1->node_id.pub);
	ASSERT_NE (nullptr, channel0);

	{
		// check that the endpoints are part of the same connection
		std::shared_ptr<nano::socket> sock0 = channel0->socket.lock ();
		std::shared_ptr<nano::socket> sock1 = channel1->socket.lock ();
		ASSERT_TRUE (sock0->local_endpoint () == sock1->remote_endpoint ());
		ASSERT_TRUE (sock1->local_endpoint () == sock0->remote_endpoint ());
	}

	// capture the state before and ensure the clock ticks at least once
	auto timestamp_before_keepalive = channel0->get_last_packet_received ();
	auto keepalive_count = node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
	ASSERT_TIMELY (3s, std::chrono::steady_clock::now () > timestamp_before_keepalive);

	// send 3 keepalives
	// we need an extra keepalive to handle the race condition between the timestamp set and the counter increment
	// and we need one more keepalive to handle the possibility that there is a keepalive already in flight when we start the crucial part of the test
	// it is possible that there could be multiple keepalives in flight but we assume here that there will be no more than one in flight for the purposes of this test
	node1->network.send_keepalive (channel1);
	node1->network.send_keepalive (channel1);
	node1->network.send_keepalive (channel1);

	ASSERT_TIMELY (3s, node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in) >= keepalive_count + 3);
	ASSERT_EQ (node0->network.size (), 1);
	auto timestamp_after_keepalive = channel0->get_last_packet_received ();
	ASSERT_GT (timestamp_after_keepalive, timestamp_before_keepalive);
}

TEST (network, multi_keepalive)
{
	nano::test::system system (1);
	auto node0 = system.nodes[0];
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work));
	ASSERT_FALSE (node1->init_error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_EQ (0, node0->network.size ());
	node1->network.tcp_channels.start_tcp (node0->network.endpoint ());
	ASSERT_TIMELY (10s, node0->network.size () == 1 && node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive) >= 1);
	auto node2 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work));
	ASSERT_FALSE (node2->init_error ());
	node2->start ();
	system.nodes.push_back (node2);
	node2->network.tcp_channels.start_tcp (node0->network.endpoint ());
	ASSERT_TIMELY (10s, node1->network.size () == 2 && node0->network.size () == 2 && node2->network.size () == 2 && node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive) >= 2);
}

TEST (network, send_discarded_publish)
{
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	{
		auto transaction (node1.store.tx_begin_read ());
		node1.network.flood_block (block);
		ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.latest (transaction, nano::dev::genesis_key.pub));
		ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
	}
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.latest (transaction, nano::dev::genesis_key.pub));
	ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (20)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	{
		auto transaction (node1.store.tx_begin_read ());
		node1.network.flood_block (block);
		ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.latest (transaction, nano::dev::genesis_key.pub));
		ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
	}
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.latest (transaction, nano::dev::genesis_key.pub));
	ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
}

TEST (network, send_valid_confirm_ack)
{
	std::vector<nano::transport::transport_type> types{ nano::transport::transport_type::tcp, nano::transport::transport_type::udp };
	for (auto & type : types)
	{
		nano::node_flags node_flags;
		if (type == nano::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		nano::test::system system (2, type, node_flags);
		auto & node1 (*system.nodes[0]);
		auto & node2 (*system.nodes[1]);
		nano::keypair key2;
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		system.wallet (1)->insert_adhoc (key2.prv);
		nano::block_hash latest1 (node1.latest (nano::dev::genesis_key.pub));
		nano::block_builder builder;
		auto block2 = builder
					  .send ()
					  .previous (latest1)
					  .destination (key2.pub)
					  .balance (50)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (latest1))
					  .build ();
		nano::block_hash latest2 (node2.latest (nano::dev::genesis_key.pub));
		node1.process_active (std::make_shared<nano::send_block> (*block2));
		// Keep polling until latest block changes
		ASSERT_TIMELY (10s, node2.latest (nano::dev::genesis_key.pub) != latest2);
		// Make sure the balance has decreased after processing the block.
		ASSERT_EQ (50, node2.balance (nano::dev::genesis_key.pub));
	}
}

TEST (network, send_valid_publish)
{
	std::vector<nano::transport::transport_type> types{ nano::transport::transport_type::tcp, nano::transport::transport_type::udp };
	for (auto & type : types)
	{
		nano::node_flags node_flags;
		if (type == nano::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		nano::test::system system (2, type, node_flags);
		auto & node1 (*system.nodes[0]);
		auto & node2 (*system.nodes[1]);
		node1.bootstrap_initiator.stop ();
		node2.bootstrap_initiator.stop ();
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		nano::keypair key2;
		system.wallet (1)->insert_adhoc (key2.prv);
		nano::block_hash latest1 (node1.latest (nano::dev::genesis_key.pub));
		nano::block_builder builder;
		auto block2 = builder
					  .send ()
					  .previous (latest1)
					  .destination (key2.pub)
					  .balance (50)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (latest1))
					  .build ();
		auto hash2 (block2->hash ());
		nano::block_hash latest2 (node2.latest (nano::dev::genesis_key.pub));
		node2.process_active (std::make_shared<nano::send_block> (*block2));
		ASSERT_TIMELY (10s, node1.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
		ASSERT_NE (hash2, latest2);
		ASSERT_TIMELY (10s, node2.latest (nano::dev::genesis_key.pub) != latest2);
		ASSERT_EQ (50, node2.balance (nano::dev::genesis_key.pub));
	}
}

TEST (network, send_insufficient_work_udp)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_udp = false;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (1)
				 .balance (20)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build_shared ();
	nano::publish publish{ nano::dev::network_params.network, block };
	nano::transport::channel_udp channel (node1.network.udp_channels, node2.network.endpoint (), node1.network_params.network.protocol_version);
	channel.send (publish, [] (boost::system::error_code const & ec, size_t size) {});
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work) != 0);
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
}

TEST (network, send_insufficient_work)
{
	nano::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	// Block zero work
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (0)
				  .destination (1)
				  .balance (20)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (0)
				  .build_shared ();
	nano::publish publish1{ nano::dev::network_params.network, block1 };
	auto tcp_channel (node1.network.tcp_channels.find_node_id (node2.get_node_id ()));
	ASSERT_NE (nullptr, tcp_channel);
	tcp_channel->send (publish1, [] (boost::system::error_code const & ec, size_t size) {});
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work) != 0);
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
	// Legacy block work between epoch_2_recieve & epoch_1
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (1)
				  .balance (20)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (system.work_generate_limited (block1->hash (), node1.network_params.work.epoch_2_receive, node1.network_params.work.epoch_1 - 1))
				  .build_shared ();
	nano::publish publish2{ nano::dev::network_params.network, block2 };
	tcp_channel->send (publish2, [] (boost::system::error_code const & ec, size_t size) {});
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work) != 1);
	ASSERT_EQ (2, node2.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
	// Legacy block work epoch_1
	auto block3 = builder
				  .send ()
				  .previous (block2->hash ())
				  .destination (1)
				  .balance (20)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*system.work.generate (block2->hash (), node1.network_params.work.epoch_2))
				  .build_shared ();
	nano::publish publish3{ nano::dev::network_params.network, block3 };
	tcp_channel->send (publish3, [] (boost::system::error_code const & ec, size_t size) {});
	ASSERT_EQ (0, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	// State block work epoch_2_recieve
	auto block4 = builder
				  .state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (block1->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (20)
				  .link (1)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (system.work_generate_limited (block1->hash (), node1.network_params.work.epoch_2_receive, node1.network_params.work.epoch_1 - 1))
				  .build_shared ();
	nano::publish publish4{ nano::dev::network_params.network, block4 };
	tcp_channel->send (publish4, [] (boost::system::error_code const & ec, size_t size) {});
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (2, node2.stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
}

TEST (receivable_processor, confirm_insufficient_pos)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (nano::dev::genesis->hash ())
				  .destination (0)
				  .balance (0)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (0)
				  .build_shared ();
	node1.work_generate_blocking (*block1);
	ASSERT_EQ (nano::process_result::progress, node1.process (*block1).code);
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	nano::keypair key1;
	auto vote (std::make_shared<nano::vote> (key1.pub, key1.prv, 0, 0, std::vector<nano::block_hash>{ block1->hash () }));
	nano::confirm_ack con1{ nano::dev::network_params.network, vote };
	node1.network.inbound (con1, node1.network.udp_channels.create (node1.network.endpoint ()));
}

TEST (receivable_processor, confirm_sufficient_pos)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (nano::dev::genesis->hash ())
				  .destination (0)
				  .balance (0)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (0)
				  .build_shared ();
	node1.work_generate_blocking (*block1);
	ASSERT_EQ (nano::process_result::progress, node1.process (*block1).code);
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	auto vote (std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, 0, 0, std::vector<nano::block_hash>{ block1->hash () }));
	nano::confirm_ack con1{ nano::dev::network_params.network, vote };
	node1.network.inbound (con1, node1.network.udp_channels.create (node1.network.endpoint ()));
}

TEST (receivable_processor, send_with_receive)
{
	std::vector<nano::transport::transport_type> types{ nano::transport::transport_type::tcp, nano::transport::transport_type::udp };
	for (auto & type : types)
	{
		nano::node_flags node_flags;
		if (type == nano::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		nano::test::system system (2, type, node_flags);
		auto & node1 (*system.nodes[0]);
		auto & node2 (*system.nodes[1]);
		auto amount (std::numeric_limits<nano::uint128_t>::max ());
		nano::keypair key2;
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		nano::block_hash latest1 (node1.latest (nano::dev::genesis_key.pub));
		system.wallet (1)->insert_adhoc (key2.prv);
		nano::block_builder builder;
		auto block1 = builder
					  .send ()
					  .previous (latest1)
					  .destination (key2.pub)
					  .balance (amount - node1.config.receive_minimum.number ())
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (latest1))
					  .build_shared ();
		ASSERT_EQ (amount, node1.balance (nano::dev::genesis_key.pub));
		ASSERT_EQ (0, node1.balance (key2.pub));
		ASSERT_EQ (amount, node2.balance (nano::dev::genesis_key.pub));
		ASSERT_EQ (0, node2.balance (key2.pub));
		node1.process_active (block1);
		node1.block_processor.flush ();
		node2.process_active (block1);
		node2.block_processor.flush ();
		ASSERT_EQ (amount - node1.config.receive_minimum.number (), node1.balance (nano::dev::genesis_key.pub));
		ASSERT_EQ (0, node1.balance (key2.pub));
		ASSERT_EQ (amount - node1.config.receive_minimum.number (), node2.balance (nano::dev::genesis_key.pub));
		ASSERT_EQ (0, node2.balance (key2.pub));
		ASSERT_TIMELY (10s, node1.balance (key2.pub) == node1.config.receive_minimum.number () && node2.balance (key2.pub) == node1.config.receive_minimum.number ());
		ASSERT_EQ (amount - node1.config.receive_minimum.number (), node1.balance (nano::dev::genesis_key.pub));
		ASSERT_EQ (node1.config.receive_minimum.number (), node1.balance (key2.pub));
		ASSERT_EQ (amount - node1.config.receive_minimum.number (), node2.balance (nano::dev::genesis_key.pub));
		ASSERT_EQ (node1.config.receive_minimum.number (), node2.balance (key2.pub));
	}
}

TEST (network, receive_weight_change)
{
	nano::test::system system (2);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	{
		auto transaction (system.nodes[1]->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key2.pub);
	}
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<nano::node> const & node_a) { return node_a->weight (key2.pub) == system.nodes[0]->config.receive_minimum.number (); }));
}

TEST (parse_endpoint, valid)
{
	std::string string ("::1:24000");
	nano::endpoint endpoint;
	ASSERT_FALSE (nano::parse_endpoint (string, endpoint));
	ASSERT_EQ (boost::asio::ip::address_v6::loopback (), endpoint.address ());
	ASSERT_EQ (24000, endpoint.port ());
}

TEST (parse_endpoint, invalid_port)
{
	std::string string ("::1:24a00");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, invalid_address)
{
	std::string string ("::q:24000");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_address)
{
	std::string string (":24000");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_port)
{
	std::string string ("::1:");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_colon)
{
	std::string string ("::1");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (network, ipv6)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	ASSERT_TRUE (address.is_v4_mapped ());
	nano::endpoint endpoint1 (address, 16384);
	std::vector<uint8_t> bytes1;
	{
		nano::vectorstream stream (bytes1);
		nano::write (stream, address.to_bytes ());
	}
	ASSERT_EQ (16, bytes1.size ());
	for (auto i (bytes1.begin ()), n (bytes1.begin () + 10); i != n; ++i)
	{
		ASSERT_EQ (0, *i);
	}
	ASSERT_EQ (0xff, bytes1[10]);
	ASSERT_EQ (0xff, bytes1[11]);
	std::array<uint8_t, 16> bytes2;
	nano::bufferstream stream (bytes1.data (), bytes1.size ());
	auto error (nano::try_read (stream, bytes2));
	ASSERT_FALSE (error);
	nano::endpoint endpoint2 (boost::asio::ip::address_v6 (bytes2), 16384);
	ASSERT_EQ (endpoint1, endpoint2);
}

TEST (network, ipv6_from_ipv4)
{
	nano::endpoint endpoint1 (boost::asio::ip::address_v4::loopback (), 16000);
	ASSERT_TRUE (endpoint1.address ().is_v4 ());
	nano::endpoint endpoint2 (boost::asio::ip::address_v6::v4_mapped (endpoint1.address ().to_v4 ()), 16000);
	ASSERT_TRUE (endpoint2.address ().is_v6 ());
}

TEST (network, ipv6_bind_send_ipv4)
{
	boost::asio::io_context io_ctx;
	auto port1 = nano::test::get_available_port ();
	auto port2 = nano::test::get_available_port ();
	nano::endpoint endpoint1 (boost::asio::ip::address_v6::any (), port1);
	nano::endpoint endpoint2 (boost::asio::ip::address_v4::any (), port2);
	std::array<uint8_t, 16> bytes1{};
	auto finish1 (false);
	nano::endpoint endpoint3;
	boost::asio::ip::udp::socket socket1 (io_ctx, endpoint1);
	socket1.async_receive_from (boost::asio::buffer (bytes1.data (), bytes1.size ()), endpoint3, [&finish1] (boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (error);
		ASSERT_EQ (16, size_a);
		finish1 = true;
	});
	boost::asio::ip::udp::socket socket2 (io_ctx, endpoint2);
	nano::endpoint endpoint5 (boost::asio::ip::address_v4::loopback (), socket1.local_endpoint ().port ());
	nano::endpoint endpoint6 (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4::loopback ()), socket2.local_endpoint ().port ());
	socket2.async_send_to (boost::asio::buffer (std::array<uint8_t, 16>{}, 16), endpoint5, [] (boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (error);
		ASSERT_EQ (16, size_a);
	});
	auto iterations (0);
	while (!finish1)
	{
		io_ctx.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	ASSERT_EQ (endpoint6, endpoint3);
	std::array<uint8_t, 16> bytes2;
	nano::endpoint endpoint4;
	socket2.async_receive_from (boost::asio::buffer (bytes2.data (), bytes2.size ()), endpoint4, [] (boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (!error);
		ASSERT_EQ (16, size_a);
	});
	socket1.async_send_to (boost::asio::buffer (std::array<uint8_t, 16>{}, 16), endpoint6, [] (boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (error);
		ASSERT_EQ (16, size_a);
	});
}

TEST (network, endpoint_bad_fd)
{
	nano::test::system system (1);
	system.nodes[0]->stop ();
	auto endpoint (system.nodes[0]->network.endpoint ());
	ASSERT_TRUE (endpoint.address ().is_loopback ());
	// The endpoint is invalidated asynchronously
	ASSERT_TIMELY (10s, system.nodes[0]->network.endpoint ().port () == 0);
}

TEST (network, reserved_address)
{
	nano::test::system system (1);
	// 0 port test
	ASSERT_TRUE (nano::transport::reserved_address (nano::endpoint (boost::asio::ip::make_address_v6 ("2001::"), 0)));
	// Valid address test
	ASSERT_FALSE (nano::transport::reserved_address (nano::endpoint (boost::asio::ip::make_address_v6 ("2001::"), 1)));
	nano::endpoint loopback (boost::asio::ip::make_address_v6 ("::1"), 1);
	ASSERT_FALSE (nano::transport::reserved_address (loopback));
	nano::endpoint private_network_peer (boost::asio::ip::make_address_v6 ("::ffff:10.0.0.0"), 1);
	ASSERT_TRUE (nano::transport::reserved_address (private_network_peer, false));
	ASSERT_FALSE (nano::transport::reserved_address (private_network_peer, true));
}

TEST (network, ipv6_bind_subnetwork)
{
	auto address1 (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713"));
	auto subnet1 (boost::asio::ip::make_network_v6 (address1, 48));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298::"), subnet1.network ());
	auto address1_subnet (nano::transport::ipv4_address_or_ipv6_subnet (address1));
	ASSERT_EQ (subnet1.network (), address1_subnet);
	// Ipv4 should return initial address
	auto address2 (boost::asio::ip::make_address_v6 ("::ffff:192.168.1.1"));
	auto address2_subnet (nano::transport::ipv4_address_or_ipv6_subnet (address2));
	ASSERT_EQ (address2, address2_subnet);
}

TEST (network, network_range_ipv6)
{
	auto address1 (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713"));
	auto subnet1 (boost::asio::ip::make_network_v6 (address1, 58));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298:cf40::"), subnet1.network ());
	auto address2 (boost::asio::ip::make_address_v6 ("520d:2402:3d:5e65:11:f8:7c54:3f"));
	auto subnet2 (boost::asio::ip::make_network_v6 (address2, 33));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("520d:2402:0::"), subnet2.network ());
	// Default settings test
	auto address3 (boost::asio::ip::make_address_v6 ("a719:0f12:536e:d88a:1331:ba53:4598:04e5"));
	auto subnet3 (boost::asio::ip::make_network_v6 (address3, 32));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("a719:0f12::"), subnet3.network ());
	auto address3_subnet (nano::transport::map_address_to_subnetwork (address3));
	ASSERT_EQ (subnet3.network (), address3_subnet);
}

TEST (network, network_range_ipv4)
{
	auto address1 (boost::asio::ip::make_address_v6 ("::ffff:192.168.1.1"));
	auto subnet1 (boost::asio::ip::make_network_v6 (address1, 96 + 16));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("::ffff:192.168.0.0"), subnet1.network ());
	// Default settings test
	auto address2 (boost::asio::ip::make_address_v6 ("::ffff:80.67.148.225"));
	auto subnet2 (boost::asio::ip::make_network_v6 (address2, 96 + 24));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("::ffff:80.67.148.0"), subnet2.network ());
	auto address2_subnet (nano::transport::map_address_to_subnetwork (address2));
	ASSERT_EQ (subnet2.network (), address2_subnet);
}

TEST (node, port_mapping)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	node0->port_mapping.refresh_devices ();
	node0->port_mapping.start ();
	auto end (std::chrono::steady_clock::now () + std::chrono::seconds (500));
	(void)end;
	// while (std::chrono::steady_clock::now () < end)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (message_buffer_manager, one_buffer)
{
	nano::stat stats;
	nano::message_buffer_manager buffer (stats, 512, 1);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.dequeue ());
	ASSERT_EQ (buffer1, buffer2);
	buffer.release (buffer2);
	auto buffer3 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer3);
}

TEST (message_buffer_manager, two_buffers)
{
	nano::stat stats;
	nano::message_buffer_manager buffer (stats, 512, 2);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer2);
	ASSERT_NE (buffer1, buffer2);
	buffer.enqueue (buffer2);
	buffer.enqueue (buffer1);
	auto buffer3 (buffer.dequeue ());
	ASSERT_EQ (buffer2, buffer3);
	auto buffer4 (buffer.dequeue ());
	ASSERT_EQ (buffer1, buffer4);
	buffer.release (buffer3);
	buffer.release (buffer4);
	auto buffer5 (buffer.allocate ());
	ASSERT_EQ (buffer2, buffer5);
	auto buffer6 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer6);
}

TEST (message_buffer_manager, one_overflow)
{
	nano::stat stats;
	nano::message_buffer_manager buffer (stats, 512, 1);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer2);
}

TEST (message_buffer_manager, two_overflow)
{
	nano::stat stats;
	nano::message_buffer_manager buffer (stats, 512, 2);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer2);
	ASSERT_NE (buffer1, buffer2);
	buffer.enqueue (buffer2);
	auto buffer3 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer3);
	auto buffer4 (buffer.allocate ());
	ASSERT_EQ (buffer2, buffer4);
}

TEST (message_buffer_manager, one_buffer_multithreaded)
{
	nano::stat stats;
	nano::message_buffer_manager buffer (stats, 512, 1);
	boost::thread thread ([&buffer] () {
		auto done (false);
		while (!done)
		{
			auto item (buffer.dequeue ());
			done = item == nullptr;
			if (item != nullptr)
			{
				buffer.release (item);
			}
		}
	});
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer2);
	buffer.stop ();
	thread.join ();
}

TEST (message_buffer_manager, many_buffers_multithreaded)
{
	nano::stat stats;
	nano::message_buffer_manager buffer (stats, 512, 16);
	std::vector<boost::thread> threads;
	for (auto i (0); i < 4; ++i)
	{
		threads.push_back (boost::thread ([&buffer] () {
			auto done (false);
			while (!done)
			{
				auto item (buffer.dequeue ());
				done = item == nullptr;
				if (item != nullptr)
				{
					buffer.release (item);
				}
			}
		}));
	}
	std::atomic_int count (0);
	for (auto i (0); i < 4; ++i)
	{
		threads.push_back (boost::thread ([&buffer, &count] () {
			auto done (false);
			for (auto i (0); !done && i < 1000; ++i)
			{
				auto item (buffer.allocate ());
				done = item == nullptr;
				if (item != nullptr)
				{
					buffer.enqueue (item);
					++count;
					if (count > 3000)
					{
						buffer.stop ();
					}
				}
			}
		}));
	}
	buffer.stop ();
	for (auto & i : threads)
	{
		i.join ();
	}
}

TEST (message_buffer_manager, stats)
{
	nano::stat stats;
	nano::message_buffer_manager buffer (stats, 512, 1);
	auto buffer1 (buffer.allocate ());
	buffer.enqueue (buffer1);
	buffer.allocate ();
	ASSERT_EQ (1, stats.count (nano::stat::type::udp, nano::stat::detail::overflow));
}

TEST (tcp_listener, tcp_node_id_handshake)
{
	nano::test::system system (1);
	auto socket (std::make_shared<nano::client_socket> (*system.nodes[0]));
	auto bootstrap_endpoint (system.nodes[0]->tcp_listener.endpoint ());
	auto cookie (system.nodes[0]->network.syn_cookies.assign (nano::transport::map_tcp_to_endpoint (bootstrap_endpoint)));
	nano::node_id_handshake node_id_handshake{ nano::dev::network_params.network, cookie, boost::none };
	auto input (node_id_handshake.to_shared_const_buffer ());
	std::atomic<bool> write_done (false);
	socket->async_connect (bootstrap_endpoint, [&input, socket, &write_done] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		socket->async_write (input, [&input, &write_done] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
			ASSERT_EQ (input.size (), size_a);
			write_done = true;
		});
	});

	ASSERT_TIMELY (5s, write_done);

	boost::optional<std::pair<nano::account, nano::signature>> response_zero (std::make_pair (nano::account{}, nano::signature (0)));
	nano::node_id_handshake node_id_handshake_response{ nano::dev::network_params.network, boost::none, response_zero };
	auto output (node_id_handshake_response.to_bytes ());
	std::atomic<bool> done (false);
	socket->async_read (output, output->size (), [&output, &done] (boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
		ASSERT_EQ (output->size (), size_a);
		done = true;
	});
	ASSERT_TIMELY (5s, done);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3611
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3615
TEST (tcp_listener, DISABLED_tcp_listener_timeout_empty)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	auto socket (std::make_shared<nano::client_socket> (*node0));
	std::atomic<bool> connected (false);
	socket->async_connect (node0->tcp_listener.endpoint (), [&connected] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		connected = true;
	});
	ASSERT_TIMELY (5s, connected);
	bool disconnected (false);
	system.deadline_set (std::chrono::seconds (6));
	while (!disconnected)
	{
		{
			nano::lock_guard<nano::mutex> guard (node0->tcp_listener.mutex);
			disconnected = node0->tcp_listener.connections.empty ();
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (tcp_listener, tcp_listener_timeout_node_id_handshake)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	auto socket (std::make_shared<nano::client_socket> (*node0));
	auto cookie (node0->network.syn_cookies.assign (nano::transport::map_tcp_to_endpoint (node0->tcp_listener.endpoint ())));
	nano::node_id_handshake node_id_handshake{ nano::dev::network_params.network, cookie, boost::none };
	auto channel = std::make_shared<nano::transport::channel_tcp> (*node0, socket);
	socket->async_connect (node0->tcp_listener.endpoint (), [&node_id_handshake, channel] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		channel->send (node_id_handshake, [] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	});
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake) != 0);
	{
		nano::lock_guard<nano::mutex> guard (node0->tcp_listener.mutex);
		ASSERT_EQ (node0->tcp_listener.connections.size (), 1);
	}
	bool disconnected (false);
	system.deadline_set (std::chrono::seconds (20));
	while (!disconnected)
	{
		{
			nano::lock_guard<nano::mutex> guard (node0->tcp_listener.mutex);
			disconnected = node0->tcp_listener.connections.empty ();
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (network, replace_port)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_udp = false;
	node_flags.disable_ongoing_telemetry_requests = true;
	node_flags.disable_initial_telemetry_requests = true;
	nano::node_config node0_config (nano::test::get_available_port (), system.logging);
	node0_config.io_threads = 8;
	auto node0 = system.add_node (node0_config, node_flags);
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work, node_flags));
	node1->start ();
	system.nodes.push_back (node1);
	auto wrong_endpoint = nano::endpoint (node1->network.endpoint ().address (), nano::test_node_port ());
	auto channel0 (node0->network.udp_channels.insert (wrong_endpoint, node1->network_params.network.protocol_version));
	ASSERT_NE (nullptr, channel0);
	node0->network.udp_channels.modify (channel0, [&node1] (std::shared_ptr<nano::transport::channel> const & channel_a) {
		channel_a->set_node_id (node1->node_id.pub);
	});
	auto peers_list (node0->network.list (std::numeric_limits<size_t>::max ()));
	ASSERT_EQ (peers_list[0]->get_node_id (), node1->node_id.pub);
	auto channel1 (std::make_shared<nano::transport::channel_udp> (node0->network.udp_channels, node1->network.endpoint (), node1->network_params.network.protocol_version));
	ASSERT_EQ (node0->network.udp_channels.size (), 1);
	node0->network.send_keepalive (channel1);
	// On handshake, the channel is replaced
	ASSERT_TIMELY (5s, !node0->network.udp_channels.channel (wrong_endpoint) && node0->network.udp_channels.channel (node1->network.endpoint ()));
}

// Test disabled because it's failing repeatedly for Windows + LMDB.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3622
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3621
#ifndef _WIN32
TEST (network, peer_max_tcp_attempts)
{
	// Add nodes that can accept TCP connection, but not node ID handshake
	nano::node_flags node_flags;
	node_flags.disable_connection_cleanup = true;
	nano::test::system system;
	auto node = system.add_node (node_flags);
	for (auto i (0); i < node->network_params.network.max_peers_per_ip; ++i)
	{
		auto node2 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work, node_flags));
		node2->start ();
		system.nodes.push_back (node2);
		// Start TCP attempt
		node->network.merge_peer (node2->network.endpoint ());
	}
	ASSERT_EQ (0, node->network.size ());
	ASSERT_TRUE (node->network.tcp_channels.reachout (nano::endpoint (node->network.endpoint ().address (), nano::test::get_available_port ())));
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_ip, nano::stat::dir::out));
}
#endif

namespace nano
{
namespace transport
{
	TEST (network, peer_max_tcp_attempts_subnetwork)
	{
		nano::node_flags node_flags;
		node_flags.disable_max_peers_per_ip = true;
		nano::test::system system;
		system.add_node (node_flags);
		auto node (system.nodes[0]);
		for (auto i (0); i < node->network_params.network.max_peers_per_subnetwork; ++i)
		{
			auto address (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x7f000001 + i))); // 127.0.0.1 hex
			nano::endpoint endpoint (address, nano::test::get_available_port ());
			ASSERT_FALSE (node->network.tcp_channels.reachout (endpoint));
		}
		ASSERT_EQ (0, node->network.size ());
		ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_subnetwork, nano::stat::dir::out));
		ASSERT_TRUE (node->network.tcp_channels.reachout (nano::endpoint (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"), nano::test::get_available_port ())));
		ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_subnetwork, nano::stat::dir::out));
	}
}
}

TEST (network, duplicate_detection)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_udp = false;
	auto & node0 (*system.add_node (node_flags));
	auto & node1 (*system.add_node (node_flags));
	auto udp_channel (std::make_shared<nano::transport::channel_udp> (node0.network.udp_channels, node1.network.endpoint (), node1.network_params.network.protocol_version));
	nano::publish publish{ nano::dev::network_params.network, nano::dev::genesis };

	// Publish duplicate detection through UDP
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish));
	udp_channel->send (publish);
	udp_channel->send (publish);
	ASSERT_TIMELY (2s, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish) == 1);

	// Publish duplicate detection through TCP
	auto tcp_channel (node0.network.tcp_channels.find_node_id (node1.get_node_id ()));
	ASSERT_NE (nullptr, tcp_channel);
	ASSERT_EQ (1, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish));
	tcp_channel->send (publish);
	ASSERT_TIMELY (2s, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish) == 2);
}

TEST (network, duplicate_revert_publish)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.block_processor_full_size = 0;
	auto & node (*system.add_node (node_flags));
	ASSERT_TRUE (node.block_processor.full ());
	nano::publish publish{ nano::dev::network_params.network, nano::dev::genesis };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		publish.block->serialize (stream);
	}
	// Add to the blocks filter
	// Should be cleared when dropping due to a full block processor, as long as the message has the optional digest attached
	// Test network.duplicate_detection ensures that the digest is attached when deserializing messages
	nano::uint128_t digest;
	ASSERT_FALSE (node.network.publish_filter.apply (bytes.data (), bytes.size (), &digest));
	ASSERT_TRUE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	auto other_node (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work));
	other_node->start ();
	system.nodes.push_back (other_node);
	auto channel = nano::test::establish_tcp (system, *other_node, node.network.endpoint ());
	ASSERT_NE (nullptr, channel);
	ASSERT_EQ (0, publish.digest);
	node.network.inbound (publish, channel);
	ASSERT_TRUE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	publish.digest = digest;
	node.network.inbound (publish, channel);
	ASSERT_FALSE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
}

// The test must be completed in less than 1 second
TEST (network, bandwidth_limiter)
{
	nano::test::system system;
	nano::publish message{ nano::dev::network_params.network, nano::dev::genesis };
	auto message_size = message.to_bytes ()->size ();
	auto message_limit = 4; // must be multiple of the number of channels
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.bandwidth_limit = message_limit * message_size;
	node_config.bandwidth_limit_burst_ratio = 1.0;
	auto & node = *system.add_node (node_config);
	auto channel1 (node.network.udp_channels.create (node.network.endpoint ()));
	auto channel2 (node.network.udp_channels.create (node.network.endpoint ()));
	// Send droppable messages
	for (auto i = 0; i < message_limit; i += 2) // number of channels
	{
		channel1->send (message);
		channel2->send (message);
	}
	// Only sent messages below limit, so we don't expect any drops
	ASSERT_TIMELY (1s, 0 == node.stats.count (nano::stat::type::drop, nano::stat::detail::publish, nano::stat::dir::out));

	// Send droppable message; drop stats should increase by one now
	channel1->send (message);
	ASSERT_TIMELY (1s, 1 == node.stats.count (nano::stat::type::drop, nano::stat::detail::publish, nano::stat::dir::out));

	// Send non-droppable message, i.e. drop stats should not increase
	channel2->send (message, nullptr, nano::buffer_drop_policy::no_limiter_drop);
	ASSERT_TIMELY (1s, 1 == node.stats.count (nano::stat::type::drop, nano::stat::detail::publish, nano::stat::dir::out));

	// change the bandwidth settings, 2 packets will be dropped
	node.set_bandwidth_params (message_size * 2, 1.1);
	channel1->send (message);
	channel2->send (message);
	channel1->send (message);
	channel2->send (message);
	ASSERT_TIMELY (1s, 3 == node.stats.count (nano::stat::type::drop, nano::stat::detail::publish, nano::stat::dir::out));

	// change the bandwidth settings, no packet will be dropped
	node.set_bandwidth_params (message_size, 4);
	channel1->send (message);
	channel2->send (message);
	channel1->send (message);
	channel2->send (message);
	ASSERT_TIMELY (1s, 3 == node.stats.count (nano::stat::type::drop, nano::stat::detail::publish, nano::stat::dir::out));

	node.stop ();
}

namespace nano
{
TEST (peer_exclusion, validate)
{
	nano::peer_exclusion excluded_peers;
	size_t fake_peers_count = 10;
	auto max_size = excluded_peers.limited_size (fake_peers_count);
	for (auto i = 0; i < max_size + 2; ++i)
	{
		nano::tcp_endpoint endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (i)), 0);
		ASSERT_FALSE (excluded_peers.check (endpoint));
		ASSERT_EQ (1, excluded_peers.add (endpoint, fake_peers_count));
		ASSERT_FALSE (excluded_peers.check (endpoint));
	}
	// The oldest one must have been removed
	ASSERT_EQ (max_size + 1, excluded_peers.size ());
	auto & peers_by_endpoint (excluded_peers.peers.get<nano::peer_exclusion::tag_endpoint> ());
	nano::tcp_endpoint oldest (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x0)), 0);
	ASSERT_EQ (peers_by_endpoint.end (), peers_by_endpoint.find (oldest.address ()));

	auto to_seconds = [] (std::chrono::steady_clock::time_point const & timepoint) {
		return static_cast<double> (std::chrono::duration_cast<std::chrono::seconds> (timepoint.time_since_epoch ()).count ());
	};
	nano::tcp_endpoint first (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x1)), 0);
	ASSERT_NE (peers_by_endpoint.end (), peers_by_endpoint.find (first.address ()));
	nano::tcp_endpoint second (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x2)), 0);
	ASSERT_EQ (false, excluded_peers.check (second));
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours), to_seconds (peers_by_endpoint.find (second.address ())->exclude_until), 2);
	ASSERT_EQ (2, excluded_peers.add (second, fake_peers_count));
	ASSERT_EQ (peers_by_endpoint.end (), peers_by_endpoint.find (first.address ()));
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours), to_seconds (peers_by_endpoint.find (second.address ())->exclude_until), 2);
	ASSERT_EQ (3, excluded_peers.add (second, fake_peers_count));
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours * 3 * 2), to_seconds (peers_by_endpoint.find (second.address ())->exclude_until), 2);
	ASSERT_EQ (max_size, excluded_peers.size ());

	// Clear many entries if there are a low number of peers
	ASSERT_EQ (4, excluded_peers.add (second, 0));
	ASSERT_EQ (1, excluded_peers.size ());

	auto component (nano::collect_container_info (excluded_peers, ""));
	auto composite (dynamic_cast<nano::container_info_composite *> (component.get ()));
	ASSERT_NE (nullptr, component);
	auto & children (composite->get_children ());
	ASSERT_EQ (1, children.size ());
	auto child_leaf (dynamic_cast<nano::container_info_leaf *> (children.front ().get ()));
	ASSERT_NE (nullptr, child_leaf);
	auto child_info (child_leaf->get_info ());
	ASSERT_EQ ("peers", child_info.name);
	ASSERT_EQ (1, child_info.count);
	ASSERT_EQ (sizeof (decltype (excluded_peers.peers)::value_type), child_info.sizeof_element);
}
}

TEST (network, tcp_no_connect_excluded_peers)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work));
	node1->start ();
	system.nodes.push_back (node1);
	auto endpoint1_tcp (nano::transport::map_endpoint_to_tcp (node1->network.endpoint ()));
	while (!node0->network.excluded_peers.check (endpoint1_tcp))
	{
		node0->network.excluded_peers.add (endpoint1_tcp, 1);
	}
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_excluded));
	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_excluded) >= 1);
	ASSERT_EQ (nullptr, node0->network.find_node_id (node1->get_node_id ()));

	// Should not actively reachout to excluded peers
	ASSERT_TRUE (node0->network.reachout (node1->network.endpoint (), true));

	// Erasing from excluded peers should allow a connection
	node0->network.excluded_peers.remove (endpoint1_tcp);
	ASSERT_FALSE (node0->network.excluded_peers.check (endpoint1_tcp));

	// Wait until there is a syn_cookie
	ASSERT_TIMELY (5s, node1->network.syn_cookies.cookies_size () != 0);

	// Manually cleanup previous attempt
	node1->network.cleanup (std::chrono::steady_clock::now ());
	node1->network.syn_cookies.purge (std::chrono::steady_clock::now ());

	// Ensure a successful connection
	ASSERT_EQ (0, node0->network.size ());
	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY (5s, node0->network.size () == 1);
}

namespace nano
{
TEST (network, tcp_message_manager)
{
	nano::tcp_message_manager manager (1);
	nano::tcp_message_item item;
	item.node_id = nano::account (100);
	ASSERT_EQ (0, manager.entries.size ());
	manager.put_message (item);
	ASSERT_EQ (1, manager.entries.size ());
	ASSERT_EQ (manager.get_message ().node_id, item.node_id);
	ASSERT_EQ (0, manager.entries.size ());

	// Fill the queue
	manager.entries = decltype (manager.entries) (manager.max_entries, item);
	ASSERT_EQ (manager.entries.size (), manager.max_entries);

	// This task will wait until a message is consumed
	auto future = std::async (std::launch::async, [&] {
		manager.put_message (item);
	});

	// This should give sufficient time to execute put_message
	// and prove that it waits on condition variable
	std::this_thread::sleep_for (CI ? 200ms : 100ms);

	ASSERT_EQ (manager.entries.size (), manager.max_entries);
	ASSERT_EQ (manager.get_message ().node_id, item.node_id);
	ASSERT_NE (std::future_status::timeout, future.wait_for (1s));
	ASSERT_EQ (manager.entries.size (), manager.max_entries);

	nano::tcp_message_manager manager2 (2);
	size_t message_count = 10'000;
	std::vector<std::thread> consumers;
	for (auto i = 0; i < 4; ++i)
	{
		consumers.emplace_back ([&] {
			for (auto i = 0; i < message_count; ++i)
			{
				ASSERT_EQ (manager.get_message ().node_id, item.node_id);
			}
		});
	}
	std::vector<std::thread> producers;
	for (auto i = 0; i < 4; ++i)
	{
		producers.emplace_back ([&] {
			for (auto i = 0; i < message_count; ++i)
			{
				manager.put_message (item);
			}
		});
	}

	for (auto & t : boost::range::join (producers, consumers))
	{
		t.join ();
	}
}
}

TEST (network, cleanup_purge)
{
	auto test_start = std::chrono::steady_clock::now ();

	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);

	auto node2 (std::make_shared<nano::node> (system.io_ctx, nano::test::get_available_port (), nano::unique_path (), system.logging, system.work));
	node2->start ();
	system.nodes.push_back (node2);

	ASSERT_EQ (0, node1.network.size ());
	node1.network.cleanup (test_start);
	ASSERT_EQ (0, node1.network.size ());

	node1.network.udp_channels.insert (node2->network.endpoint (), node1.network_params.network.protocol_version);
	ASSERT_EQ (1, node1.network.size ());
	node1.network.cleanup (test_start);
	ASSERT_EQ (1, node1.network.size ());

	node1.network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (0, node1.network.size ());

	std::weak_ptr<nano::node> node_w = node1.shared ();
	node1.network.tcp_channels.start_tcp (node2->network.endpoint ());

	ASSERT_TIMELY (3s, node1.network.size () == 1);
	node1.network.cleanup (test_start);
	ASSERT_EQ (1, node1.network.size ());

	node1.network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (0, node1.network.size ());
}

TEST (network, loopback_channel)
{
	nano::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	nano::transport::inproc::channel channel1 (node1, node1);
	ASSERT_EQ (channel1.get_type (), nano::transport::transport_type::loopback);
	ASSERT_EQ (channel1.get_endpoint (), node1.network.endpoint ());
	ASSERT_EQ (channel1.get_tcp_endpoint (), nano::transport::map_endpoint_to_tcp (node1.network.endpoint ()));
	ASSERT_EQ (channel1.get_network_version (), node1.network_params.network.protocol_version);
	ASSERT_EQ (channel1.get_node_id (), node1.node_id.pub);
	ASSERT_EQ (channel1.get_node_id_optional ().value_or (0), node1.node_id.pub);
	nano::transport::inproc::channel channel2 (node2, node2);
	ASSERT_TRUE (channel1 == channel1);
	ASSERT_FALSE (channel1 == channel2);
	++node1.network.port;
	ASSERT_NE (channel1.get_endpoint (), node1.network.endpoint ());
}

// Ensure the network filters messages with the incorrect magic number
TEST (network, filter_invalid_network_bytes)
{
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];

	// find the comms channel that goes from node2 to node1
	auto channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// send a keepalive, from node2 to node1, with the wrong network bytes
	nano::keepalive keepalive{ nano::dev::network_params.network };
	const_cast<nano::networks &> (keepalive.header.network) = nano::networks::invalid;
	channel->send (keepalive);

	ASSERT_TIMELY (5s, 1 == node1.stats.count (nano::stat::type::error, nano::stat::detail::invalid_network));
}

// Ensure the network filters messages with the incorrect minimum version
TEST (network, filter_invalid_version_using)
{
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];

	// find the comms channel that goes from node2 to node1
	auto channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// send a keepalive, from node2 to node1, with the wrong version_using
	nano::keepalive keepalive{ nano::dev::network_params.network };
	const_cast<uint8_t &> (keepalive.header.version_using) = nano::dev::network_params.network.protocol_version_min - 1;
	channel->send (keepalive);

	ASSERT_TIMELY (5s, 1 == node1.stats.count (nano::stat::type::error, nano::stat::detail::outdated_version));
}

TEST (network, fill_keepalive_self)
{
	nano::test::system system{ 2 };
	std::array<nano::endpoint, 8> target;
	system.nodes[0]->network.fill_keepalive_self (target);
	ASSERT_TRUE (target[2].port () == system.nodes[1]->network.port);
}

/*
 * Tests that channel and channel container removes channels with dead local sockets
 */
TEST (network, purge_dead_channel_outgoing)
{
	nano::test::system system{};

	nano::node_flags flags;
	// Disable non realtime sockets
	flags.disable_bootstrap_bulk_push_client = true;
	flags.disable_bootstrap_bulk_pull_server = true;
	flags.disable_bootstrap_listener = true;
	flags.disable_lazy_bootstrap = true;
	flags.disable_legacy_bootstrap = true;
	flags.disable_wallet_bootstrap = true;

	auto & node1 = *system.add_node (flags);

	// We expect one incoming and one outgoing connection
	std::shared_ptr<nano::socket> outgoing;
	std::shared_ptr<nano::socket> incoming;

	std::atomic<int> connected_count{ 0 };
	node1.observers.socket_connected.add ([&] (nano::socket & socket) {
		connected_count++;
		outgoing = socket.shared_from_this ();

		std::cout << "connected: " << socket.remote_endpoint () << std::endl;
	});

	std::atomic<int> accepted_count{ 0 };
	node1.observers.socket_accepted.add ([&] (nano::socket & socket) {
		accepted_count++;
		incoming = socket.shared_from_this ();

		std::cout << "accepted: " << socket.remote_endpoint () << std::endl;
	});

	auto & node2 = *system.add_node (flags);

	ASSERT_TIMELY_EQ (5s, connected_count, 1);
	ASSERT_ALWAYS_EQ (1s, connected_count, 1);

	ASSERT_TIMELY_EQ (5s, accepted_count, 1);
	ASSERT_ALWAYS_EQ (1s, accepted_count, 1);

	ASSERT_EQ (node1.network.size (), 1);
	ASSERT_ALWAYS_EQ (1s, node1.network.size (), 1);

	// Store reference to the only channel
	auto channels = node1.network.list ();
	ASSERT_EQ (channels.size (), 1);
	auto channel = channels.front ();
	ASSERT_TRUE (channel);

	// When socket is dead ensure channel knows about that
	ASSERT_TRUE (channel->alive ());
	outgoing->close ();
	ASSERT_TIMELY (5s, !channel->alive ());

	// Shortly after that a new channel should be established
	ASSERT_TIMELY_EQ (5s, connected_count, 2);
	ASSERT_ALWAYS_EQ (1s, connected_count, 2);

	// Check that a new channel is healthy
	auto channels2 = node1.network.list ();
	ASSERT_EQ (channels2.size (), 1);
	auto channel2 = channels2.front ();
	ASSERT_TRUE (channel2);
	ASSERT_TRUE (channel2->alive ());
}

/*
 * Tests that channel and channel container removes channels with dead remote sockets
 */
TEST (network, purge_dead_channel_incoming)
{
	nano::test::system system{};

	nano::node_flags flags;
	// Disable non realtime sockets
	flags.disable_bootstrap_bulk_push_client = true;
	flags.disable_bootstrap_bulk_pull_server = true;
	flags.disable_bootstrap_listener = true;
	flags.disable_lazy_bootstrap = true;
	flags.disable_legacy_bootstrap = true;
	flags.disable_wallet_bootstrap = true;

	auto & node1 = *system.add_node (flags);

	// We expect one incoming and one outgoing connection
	std::shared_ptr<nano::socket> outgoing;
	std::shared_ptr<nano::socket> incoming;

	std::atomic<int> connected_count{ 0 };
	node1.observers.socket_connected.add ([&] (nano::socket & socket) {
		connected_count++;
		outgoing = socket.shared_from_this ();

		std::cout << "connected: " << socket.remote_endpoint () << std::endl;
	});

	std::atomic<int> accepted_count{ 0 };
	node1.observers.socket_accepted.add ([&] (nano::socket & socket) {
		accepted_count++;
		incoming = socket.shared_from_this ();

		std::cout << "accepted: " << socket.remote_endpoint () << std::endl;
	});

	auto & node2 = *system.add_node (flags);

	ASSERT_TIMELY_EQ (5s, connected_count, 1);
	ASSERT_ALWAYS_EQ (1s, connected_count, 1);

	ASSERT_TIMELY_EQ (5s, accepted_count, 1);
	ASSERT_ALWAYS_EQ (1s, accepted_count, 1);

	ASSERT_EQ (node2.network.size (), 1);
	ASSERT_ALWAYS_EQ (1s, node2.network.size (), 1);

	// Store reference to the only channel
	auto channels = node2.network.list ();
	ASSERT_EQ (channels.size (), 1);
	auto channel = channels.front ();
	ASSERT_TRUE (channel);

	// When remote socket is dead ensure channel knows about that
	ASSERT_TRUE (channel->alive ());
	incoming->close ();
	ASSERT_TIMELY (5s, !channel->alive ());

	// Shortly after that a new channel should be established
	ASSERT_TIMELY_EQ (5s, accepted_count, 2);
	ASSERT_ALWAYS_EQ (1s, accepted_count, 2);

	// Check that a new channel is healthy
	auto channels2 = node2.network.list ();
	ASSERT_EQ (channels2.size (), 1);
	auto channel2 = channels2.front ();
	ASSERT_TRUE (channel2);
	ASSERT_TRUE (channel2->alive ());
}