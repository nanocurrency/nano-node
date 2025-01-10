#include <celerix/lib/blocks.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/transport/fake.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/node/transport/tcp_listener.hpp>
#include <celerix/node/transport/tcp_socket.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/iostreams/stream_buffer.hpp>
#include <boost/range/join.hpp>
#include <boost/thread.hpp>

using namespace std::chrono_literals;

TEST (network, tcp_connection)
{
	celerix::test::system system;
	boost::asio::ip::tcp::acceptor acceptor (*system.io_ctx);
	auto port = system.get_available_port ();
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), port);
	acceptor.open (endpoint.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
	acceptor.bind (endpoint);
	acceptor.listen ();
	boost::asio::ip::tcp::socket incoming (*system.io_ctx);
	std::atomic<bool> done1 (false);
	std::string message1;
	acceptor.async_accept (incoming, [&done1, &message1] (boost::system::error_code const & ec_a) {
		if (ec_a)
		{
			message1 = ec_a.message ();
			std::cerr << message1;
		}
		done1 = true;
	});
	boost::asio::ip::tcp::socket connector (*system.io_ctx);
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
	ASSERT_TIMELY (5s, done1 && done2);
	ASSERT_EQ (0, message1.size ());
	ASSERT_EQ (0, message2.size ());
}

TEST (network, construction_with_specified_port)
{
	celerix::test::system system{};
	auto const port = celerix::test::speculatively_choose_a_free_tcp_bind_port ();
	ASSERT_NE (port, 0);
	auto const node = system.add_node (celerix::node_config{ port });
	EXPECT_EQ (port, node->network.port);
	EXPECT_EQ (port, node->network.endpoint ().port ());
	EXPECT_EQ (port, node->tcp_listener.endpoint ().port ());
}

TEST (network, construction_without_specified_port)
{
	celerix::test::system system{};
	auto const node = system.add_node ();
	auto const port = node->network.port.load ();
	EXPECT_NE (0, port);
	EXPECT_EQ (port, node->network.endpoint ().port ());
	EXPECT_EQ (port, node->tcp_listener.endpoint ().port ());
}

TEST (network, send_node_id_handshake_tcp)
{
	celerix::test::system system (1);
	auto node0 (system.nodes[0]);
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
	node1->start ();
	system.nodes.push_back (node1);
	auto initial (node0->stats.count (celerix::stat::type::message, celerix::stat::detail::node_id_handshake, celerix::stat::dir::in));
	auto initial_node1 (node1->stats.count (celerix::stat::type::message, celerix::stat::detail::node_id_handshake, celerix::stat::dir::in));
	auto initial_keepalive (node0->stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive, celerix::stat::dir::in));
	std::weak_ptr<celerix::node> node_w (node0);
	node0->network.tcp_channels.start_tcp (node1->network.endpoint ());
	ASSERT_EQ (0, node0->network.size ());
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_TIMELY (10s, node0->stats.count (celerix::stat::type::message, celerix::stat::detail::node_id_handshake, celerix::stat::dir::in) >= initial + 2);
	ASSERT_TIMELY (5s, node1->stats.count (celerix::stat::type::message, celerix::stat::detail::node_id_handshake, celerix::stat::dir::in) >= initial_node1 + 2);
	ASSERT_TIMELY (5s, node0->stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive, celerix::stat::dir::in) >= initial_keepalive + 2);
	ASSERT_TIMELY (5s, node1->stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive, celerix::stat::dir::in) >= initial_keepalive + 2);
	ASSERT_EQ (1, node0->network.size ());
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node0->network.list (1));
	ASSERT_EQ (celerix::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (node1->get_node_id (), list1[0]->get_node_id ());
	auto list2 (node1->network.list (1));
	ASSERT_EQ (celerix::transport::transport_type::tcp, list2[0]->get_type ());
	ASSERT_EQ (node0->get_node_id (), list2[0]->get_node_id ());
}

TEST (network, last_contacted)
{
	celerix::test::system system (1);

	auto node0 = system.nodes[0];
	ASSERT_EQ (0, node0->network.size ());

	celerix::node_config node1_config = system.default_config ();
	node1_config.tcp.max_inbound_connections = 0; // Prevent ephemeral node1->node0 channel repacement with incoming connection
	auto node1 (std::make_shared<celerix::node> (system.io_ctx, celerix::unique_path (), node1_config, system.work));
	node1->start ();
	system.nodes.push_back (node1);

	auto channel1 = celerix::test::establish_tcp (system, *node1, node0->network.endpoint ());
	ASSERT_NE (nullptr, channel1);
	ASSERT_TIMELY_EQ (3s, node0->network.size (), 1);

	// channel0 is the other side of channel1, same connection different endpoint
	auto channel0 = node0->network.tcp_channels.find_node_id (node1->node_id.pub);
	ASSERT_NE (nullptr, channel0);

	{
		// check that the endpoints are part of the same connection
		std::shared_ptr<celerix::transport::tcp_socket> sock0 = channel0->socket;
		std::shared_ptr<celerix::transport::tcp_socket> sock1 = channel1->socket;
		ASSERT_EQ (sock0->local_endpoint (), sock1->remote_endpoint ());
		ASSERT_EQ (sock1->local_endpoint (), sock0->remote_endpoint ());
	}

	// capture the state before and ensure the clock ticks at least once
	auto timestamp_before_keepalive = channel0->get_last_packet_received ();
	auto keepalive_count = node0->stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive, celerix::stat::dir::in);
	ASSERT_TIMELY (3s, std::chrono::steady_clock::now () > timestamp_before_keepalive);

	// send 3 keepalives
	// we need an extra keepalive to handle the race condition between the timestamp set and the counter increment
	// and we need one more keepalive to handle the possibility that there is a keepalive already in flight when we start the crucial part of the test
	// it is possible that there could be multiple keepalives in flight but we assume here that there will be no more than one in flight for the purposes of this test
	node1->network.send_keepalive (channel1);
	node1->network.send_keepalive (channel1);
	node1->network.send_keepalive (channel1);

	ASSERT_TIMELY (3s, node0->stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive, celerix::stat::dir::in) >= keepalive_count + 3);
	ASSERT_EQ (node0->network.size (), 1);
	auto timestamp_after_keepalive = channel0->get_last_packet_received ();
	ASSERT_GT (timestamp_after_keepalive, timestamp_before_keepalive);
}

TEST (network, multi_keepalive)
{
	celerix::test::system system (1);
	auto node0 = system.nodes[0];
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
	ASSERT_FALSE (node1->init_error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_EQ (0, node0->network.size ());
	node1->network.tcp_channels.start_tcp (node0->network.endpoint ());
	ASSERT_TIMELY (10s, node0->network.size () == 1 && node0->stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive) >= 1);
	auto node2 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
	ASSERT_FALSE (node2->init_error ());
	node2->start ();
	system.nodes.push_back (node2);
	node2->network.tcp_channels.start_tcp (node0->network.endpoint ());
	ASSERT_TIMELY (10s, node1->network.size () == 2 && node0->network.size () == 2 && node2->network.size () == 2 && node0->stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive) >= 2);
}

TEST (network, send_discarded_publish)
{
	celerix::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (celerix::keypair ().prv, 4)
				 .work (*system.work.generate (celerix::root (1)))
				 .build ();
	{
		auto transaction = node1.ledger.tx_begin_read ();
		node1.network.flood_block (block, celerix::transport::traffic_type::test);
		ASSERT_EQ (celerix::dev::genesis->hash (), node1.ledger.any.account_head (transaction, celerix::dev::genesis_key.pub));
		ASSERT_EQ (celerix::dev::genesis->hash (), node2.latest (celerix::dev::genesis_key.pub));
	}
	ASSERT_TIMELY (10s, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in) != 0);
	auto transaction = node1.ledger.tx_begin_read ();
	ASSERT_EQ (celerix::dev::genesis->hash (), node1.ledger.any.account_head (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::genesis->hash (), node2.latest (celerix::dev::genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
	celerix::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (20)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::root (1)))
				 .build ();
	{
		auto transaction = node1.ledger.tx_begin_read ();
		node1.network.flood_block (block, celerix::transport::traffic_type::test);
		ASSERT_EQ (celerix::dev::genesis->hash (), node1.ledger.any.account_head (transaction, celerix::dev::genesis_key.pub));
		ASSERT_EQ (celerix::dev::genesis->hash (), node2.latest (celerix::dev::genesis_key.pub));
	}
	ASSERT_TIMELY (10s, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in) != 0);
	auto transaction = node1.ledger.tx_begin_read ();
	ASSERT_EQ (celerix::dev::genesis->hash (), node1.ledger.any.account_head (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::genesis->hash (), node2.latest (celerix::dev::genesis_key.pub));
}

TEST (network, send_valid_confirm_ack)
{
	auto type = celerix::transport::transport_type::tcp;
	celerix::node_flags node_flags;
	celerix::test::system system (2, type, node_flags);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	celerix::block_hash latest1 (node1.latest (celerix::dev::genesis_key.pub));
	celerix::block_builder builder;
	auto block2 = builder
				  .send ()
				  .previous (latest1)
				  .destination (key2.pub)
				  .balance (50)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (latest1))
				  .build ();
	celerix::block_hash latest2 (node2.latest (celerix::dev::genesis_key.pub));
	node1.process_active (std::make_shared<celerix::send_block> (*block2));
	// Keep polling until latest block changes
	ASSERT_TIMELY (10s, node2.latest (celerix::dev::genesis_key.pub) != latest2);
	// Make sure the balance has decreased after processing the block.
	ASSERT_EQ (50, node2.balance (celerix::dev::genesis_key.pub));
}

TEST (network, send_valid_publish)
{
	auto type = celerix::transport::transport_type::tcp;
	celerix::node_flags node_flags;
	celerix::test::system system (2, type, node_flags);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	celerix::block_hash latest1 (node1.latest (celerix::dev::genesis_key.pub));
	celerix::block_builder builder;
	auto block2 = builder
				  .send ()
				  .previous (latest1)
				  .destination (key2.pub)
				  .balance (50)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (latest1))
				  .build ();
	auto hash2 (block2->hash ());
	celerix::block_hash latest2 (node2.latest (celerix::dev::genesis_key.pub));
	node2.process_active (std::make_shared<celerix::send_block> (*block2));
	ASSERT_TIMELY (10s, node1.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in) != 0);
	ASSERT_NE (hash2, latest2);
	ASSERT_TIMELY (10s, node2.latest (celerix::dev::genesis_key.pub) != latest2);
	ASSERT_EQ (50, node2.balance (celerix::dev::genesis_key.pub));
}

TEST (network, send_insufficient_work)
{
	celerix::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	// Block zero work
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (0)
				  .destination (1)
				  .balance (20)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	celerix::publish publish1{ celerix::dev::network_params.network, block1 };
	auto tcp_channel (node1.network.tcp_channels.find_node_id (node2.get_node_id ()));
	ASSERT_NE (nullptr, tcp_channel);
	tcp_channel->send (publish1, celerix::transport::traffic_type::test);
	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::error, celerix::stat::detail::insufficient_work));
	ASSERT_TIMELY (10s, node2.stats.count (celerix::stat::type::error, celerix::stat::detail::insufficient_work) != 0);
	ASSERT_EQ (1, node2.stats.count (celerix::stat::type::error, celerix::stat::detail::insufficient_work));
	// Legacy block work between epoch_2_recieve & epoch_1
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (1)
				  .balance (20)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (system.work_generate_limited (block1->hash (), node1.network_params.work.epoch_2_receive, node1.network_params.work.epoch_1 - 1))
				  .build ();
	celerix::publish publish2{ celerix::dev::network_params.network, block2 };
	tcp_channel->send (publish2, celerix::transport::traffic_type::test);
	ASSERT_TIMELY (10s, node2.stats.count (celerix::stat::type::error, celerix::stat::detail::insufficient_work) != 1);
	ASSERT_EQ (2, node2.stats.count (celerix::stat::type::error, celerix::stat::detail::insufficient_work));
	// Legacy block work epoch_1
	auto block3 = builder
				  .send ()
				  .previous (block2->hash ())
				  .destination (1)
				  .balance (20)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (block2->hash (), node1.network_params.work.epoch_2))
				  .build ();
	celerix::publish publish3{ celerix::dev::network_params.network, block3 };
	tcp_channel->send (publish3, celerix::transport::traffic_type::test);
	ASSERT_EQ (0, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in));
	ASSERT_TIMELY (10s, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in) != 0);
	ASSERT_EQ (1, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in));
	// State block work epoch_2_recieve
	auto block4 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (block1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (20)
				  .link (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (system.work_generate_limited (block1->hash (), node1.network_params.work.epoch_2_receive, node1.network_params.work.epoch_1 - 1))
				  .build ();
	celerix::publish publish4{ celerix::dev::network_params.network, block4 };
	tcp_channel->send (publish4, celerix::transport::traffic_type::test);
	ASSERT_TIMELY (10s, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in) != 0);
	ASSERT_EQ (1, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::in));
	ASSERT_EQ (2, node2.stats.count (celerix::stat::type::error, celerix::stat::detail::insufficient_work));
}

TEST (receivable_processor, confirm_insufficient_pos)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (0)
				  .balance (celerix::dev::constants.genesis_amount - 1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	node1.work_generate_blocking (*block1);
	ASSERT_EQ (celerix::block_status::progress, node1.process (block1));
	auto election = celerix::test::start_election (system, node1, block1->hash ());
	celerix::keypair key1;
	auto vote = celerix::test::make_final_vote (key1, { block1 });
	celerix::confirm_ack con1{ celerix::dev::network_params.network, vote };
	auto channel1 = std::make_shared<celerix::transport::inproc::channel> (node1, node1);
	ASSERT_EQ (1, election->votes ().size ());
	node1.inbound (con1, channel1);
	ASSERT_TIMELY_EQ (5s, 2, election->votes ().size ())
	ASSERT_FALSE (election->confirmed ());
}

TEST (receivable_processor, confirm_sufficient_pos)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (0)
				  .balance (celerix::dev::constants.genesis_amount - 1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	node1.work_generate_blocking (*block1);
	ASSERT_EQ (celerix::block_status::progress, node1.process (block1));
	auto election = celerix::test::start_election (system, node1, block1->hash ());
	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { block1 });
	celerix::confirm_ack con1{ celerix::dev::network_params.network, vote };
	auto channel1 = std::make_shared<celerix::transport::inproc::channel> (node1, node1);
	ASSERT_EQ (1, election->votes ().size ());
	node1.inbound (con1, channel1);
	ASSERT_TIMELY_EQ (5s, 2, election->votes ().size ())
	ASSERT_TRUE (election->confirmed ());
}

TEST (receivable_processor, send_with_receive)
{
	auto type = celerix::transport::transport_type::tcp;
	celerix::node_flags node_flags;
	celerix::test::system system (2, type, node_flags);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto amount (std::numeric_limits<celerix::uint128_t>::max ());
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_hash latest1 (node1.latest (celerix::dev::genesis_key.pub));
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (latest1)
				  .destination (key2.pub)
				  .balance (amount - node1.config.receive_minimum.number ())
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (latest1))
				  .build ();
	ASSERT_EQ (amount, node1.balance (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, node1.balance (key2.pub));
	ASSERT_EQ (amount, node2.balance (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, node2.balance (key2.pub));
	node1.process_active (block1);
	ASSERT_TIMELY (5s, celerix::test::exists (node1, { block1 }));
	node2.process_active (block1);
	ASSERT_TIMELY (5s, celerix::test::exists (node2, { block1 }));
	ASSERT_EQ (amount - node1.config.receive_minimum.number (), node1.balance (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, node1.balance (key2.pub));
	ASSERT_EQ (amount - node1.config.receive_minimum.number (), node2.balance (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, node2.balance (key2.pub));
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_TIMELY (10s, node1.balance (key2.pub) == node1.config.receive_minimum.number () && node2.balance (key2.pub) == node1.config.receive_minimum.number ());
	ASSERT_EQ (amount - node1.config.receive_minimum.number (), node1.balance (celerix::dev::genesis_key.pub));
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.balance (key2.pub));
	ASSERT_EQ (amount - node1.config.receive_minimum.number (), node2.balance (celerix::dev::genesis_key.pub));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.balance (key2.pub));
}

TEST (network, receive_weight_change)
{
	celerix::test::system system (2);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	{
		auto transaction (system.nodes[1]->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key2.pub);
	}
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<celerix::node> const & node_a) { return node_a->weight (key2.pub) == system.nodes[0]->config.receive_minimum.number (); }));
}

TEST (parse_endpoint, valid)
{
	std::string string ("::1:24000");
	celerix::endpoint endpoint;
	ASSERT_FALSE (celerix::parse_endpoint (string, endpoint));
	ASSERT_EQ (boost::asio::ip::address_v6::loopback (), endpoint.address ());
	ASSERT_EQ (24000, endpoint.port ());
}

TEST (parse_endpoint, invalid_port)
{
	std::string string ("::1:24a00");
	celerix::endpoint endpoint;
	ASSERT_TRUE (celerix::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, invalid_address)
{
	std::string string ("::q:24000");
	celerix::endpoint endpoint;
	ASSERT_TRUE (celerix::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_address)
{
	std::string string (":24000");
	celerix::endpoint endpoint;
	ASSERT_TRUE (celerix::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_port)
{
	std::string string ("::1:");
	celerix::endpoint endpoint;
	ASSERT_TRUE (celerix::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_colon)
{
	std::string string ("::1");
	celerix::endpoint endpoint;
	ASSERT_TRUE (celerix::parse_endpoint (string, endpoint));
}

TEST (network, ipv6)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	ASSERT_TRUE (address.is_v4_mapped ());
	celerix::endpoint endpoint1 (address, 16384);
	std::vector<uint8_t> bytes1;
	{
		celerix::vectorstream stream (bytes1);
		celerix::write (stream, address.to_bytes ());
	}
	ASSERT_EQ (16, bytes1.size ());
	for (auto i (bytes1.begin ()), n (bytes1.begin () + 10); i != n; ++i)
	{
		ASSERT_EQ (0, *i);
	}
	ASSERT_EQ (0xff, bytes1[10]);
	ASSERT_EQ (0xff, bytes1[11]);
	std::array<uint8_t, 16> bytes2;
	celerix::bufferstream stream (bytes1.data (), bytes1.size ());
	auto error (celerix::try_read (stream, bytes2));
	ASSERT_FALSE (error);
	celerix::endpoint endpoint2 (boost::asio::ip::address_v6 (bytes2), 16384);
	ASSERT_EQ (endpoint1, endpoint2);
}

TEST (network, ipv6_from_ipv4)
{
	celerix::endpoint endpoint1 (boost::asio::ip::address_v4::loopback (), 16000);
	ASSERT_TRUE (endpoint1.address ().is_v4 ());
	celerix::endpoint endpoint2 (boost::asio::ip::address_v6::v4_mapped (endpoint1.address ().to_v4 ()), 16000);
	ASSERT_TRUE (endpoint2.address ().is_v6 ());
}

TEST (network, endpoint_bad_fd)
{
	celerix::test::system system (1);
	system.stop_node (*system.nodes[0]);
	auto endpoint (system.nodes[0]->network.endpoint ());
	ASSERT_TRUE (endpoint.address ().is_loopback ());
	// The endpoint is invalidated asynchronously
	ASSERT_TIMELY_EQ (10s, system.nodes[0]->network.endpoint ().port (), 0);
}

// Test disabled because it's failing repeatedly for Windows + LMDB.
// PR in which it got disabled: https://github.com/celerixcurrency/celerix-node/pull/3622
// Issue for investigating it: https://github.com/celerixcurrency/celerix-node/issues/3621
#ifndef _WIN32
TEST (network, peer_max_tcp_attempts)
{
	celerix::test::system system;

	// Add nodes that can accept TCP connection, but not node ID handshake
	celerix::node_flags node_flags;
	node_flags.disable_connection_cleanup = true;
	celerix::node_config node_config = system.default_config ();
	node_config.network.max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);

	for (auto i = 0; i < node_config.network.max_peers_per_ip; ++i)
	{
		// Disable reachout from temporary nodes to avoid mixing outbound and inbound connections
		celerix::node_config temp_config = system.default_config ();
		temp_config.network.peer_reachout = {};
		temp_config.network.cached_peer_reachout = {};
		auto temp_node = system.make_disconnected_node (temp_config, node_flags);
		ASSERT_TRUE (node->network.merge_peer (temp_node->network.endpoint ()));
	}

	ASSERT_TIMELY_EQ (15s, node->network.size (), node_config.network.max_peers_per_ip);
	ASSERT_FALSE (node->network.tcp_channels.track_reachout (celerix::endpoint (node->network.endpoint ().address (), system.get_available_port ())));
	ASSERT_LE (1, node->stats.count (celerix::stat::type::tcp, celerix::stat::detail::max_per_ip, celerix::stat::dir::out));
}
#endif

TEST (network, peer_max_tcp_attempts_subnetwork)
{
	celerix::test::system system;

	celerix::node_flags node_flags;
	node_flags.disable_max_peers_per_ip = true;
	celerix::node_config node_config = system.default_config ();
	node_config.network.max_peers_per_subnetwork = 3;
	auto node = system.add_node (node_config, node_flags);

	for (auto i (0); i < node->config.network.max_peers_per_subnetwork; ++i)
	{
		auto address (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x7f000001 + i))); // 127.0.0.1 hex
		celerix::endpoint endpoint (address, system.get_available_port ());
		ASSERT_TRUE (node->network.tcp_channels.track_reachout (endpoint));
	}

	ASSERT_EQ (0, node->network.size ());
	ASSERT_EQ (0, node->stats.count (celerix::stat::type::tcp, celerix::stat::detail::max_per_subnetwork, celerix::stat::dir::out));
	ASSERT_FALSE (node->network.tcp_channels.track_reachout (celerix::endpoint (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"), system.get_available_port ())));
	ASSERT_EQ (1, node->stats.count (celerix::stat::type::tcp, celerix::stat::detail::max_per_subnetwork, celerix::stat::dir::out));
}

namespace
{
// Skip the first 8 bytes of the message header, which is the common header for all messages
std::vector<uint8_t> message_payload_to_bytes (celerix::message const & message)
{
	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream stream (bytes);
		message.serialize (stream);
	}
	debug_assert (bytes.size () > celerix::message_header::size);
	return std::vector<uint8_t> (bytes.begin () + celerix::message_header::size, bytes.end ());
}
}

// Send two publish messages and asserts that the duplication is detected.
TEST (network, duplicate_detection)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	auto & node0 = *system.add_node (node_flags);
	auto & node1 = *system.add_node (node_flags);
	celerix::publish publish{ celerix::dev::network_params.network, celerix::dev::genesis };

	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_publish_message));

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_publish_message));
	tcp_channel->send (publish, celerix::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_publish_message), 0);
	tcp_channel->send (publish, celerix::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (2s, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_publish_message), 1);
}

TEST (network, duplicate_revert_publish)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.block_processor.max_peer_queue = 0;
	auto & node (*system.add_node (node_config));
	celerix::publish publish{ celerix::dev::network_params.network, celerix::dev::genesis };
	std::vector<uint8_t> bytes = message_payload_to_bytes (publish);
	// Add to the blocks filter
	// Should be cleared when dropping due to a full block processor, as long as the message has the optional digest attached
	// Test network.duplicate_detection ensures that the digest is attached when deserializing messages
	celerix::uint128_t digest;
	ASSERT_FALSE (node.network.filter.apply (bytes.data (), bytes.size (), &digest));
	ASSERT_TRUE (node.network.filter.apply (bytes.data (), bytes.size ()));
	auto other_node (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
	other_node->start ();
	system.nodes.push_back (other_node);
	auto channel = celerix::test::establish_tcp (system, *other_node, node.network.endpoint ());
	ASSERT_NE (nullptr, channel);
	ASSERT_EQ (0, publish.digest);
	node.inbound (publish, celerix::test::fake_channel (node));
	ASSERT_TRUE (node.network.filter.apply (bytes.data (), bytes.size ()));
	publish.digest = digest;
	node.inbound (publish, celerix::test::fake_channel (node));
	ASSERT_FALSE (node.network.filter.apply (bytes.data (), bytes.size ()));
}

TEST (network, duplicate_vote_detection)
{
	celerix::test::system system;
	auto & node0 = *system.add_node ();
	auto & node1 = *system.add_node ();

	auto vote = celerix::test::make_vote (celerix::dev::genesis_key, { celerix::dev::genesis->hash () });
	celerix::confirm_ack message{ celerix::dev::network_params.network, vote };

	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message));

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message));
	tcp_channel->send (message, celerix::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message), 0);
	tcp_channel->send (message, celerix::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (2s, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message), 1);
}

// Ensures that the filter doesn't filter out votes that could not be queued for processing
TEST (network, duplicate_revert_vote)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.vote_processor.enable = false; // Do not drain queued votes
	node_config.vote_processor.max_non_pr_queue = 1;
	node_config.vote_processor.max_pr_queue = 1;
	auto & node0 = *system.add_node (node_config);
	auto & node1 = *system.add_node (node_config);

	auto vote1 = celerix::test::make_vote (celerix::dev::genesis_key, { celerix::dev::genesis->hash () }, 1);
	celerix::confirm_ack message1{ celerix::dev::network_params.network, vote1 };
	auto bytes1 = message_payload_to_bytes (message1);

	auto vote2 = celerix::test::make_vote (celerix::dev::genesis_key, { celerix::dev::genesis->hash () }, 2);
	celerix::confirm_ack message2{ celerix::dev::network_params.network, vote2 };
	auto bytes2 = message_payload_to_bytes (message2);

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	// First vote should be processed
	tcp_channel->send (message1, celerix::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message), 0);
	ASSERT_TIMELY (5s, node1.network.filter.check (bytes1.data (), bytes1.size ()));

	// Second vote should get dropped from processor queue
	tcp_channel->send (message2, celerix::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message), 0);
	// And the filter should not have it
	WAIT (500ms); // Give the node time to process the vote
	ASSERT_TIMELY (5s, !node1.network.filter.check (bytes2.data (), bytes2.size ()));
}

TEST (network, expire_duplicate_filter)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.network.duplicate_filter_cutoff = 3; // Expire after 3 seconds
	auto & node0 = *system.add_node (node_config);
	auto & node1 = *system.add_node (node_config);

	auto vote = celerix::test::make_vote (celerix::dev::genesis_key, { celerix::dev::genesis->hash () });
	celerix::confirm_ack message{ celerix::dev::network_params.network, vote };
	auto bytes = message_payload_to_bytes (message);

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	// Send a vote
	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message));
	tcp_channel->send (message, celerix::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message), 0);
	tcp_channel->send (message, celerix::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (2s, node1.stats.count (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message), 1);

	// The filter should expire the vote after some time
	ASSERT_TRUE (node1.network.filter.check (bytes.data (), bytes.size ()));
	ASSERT_TIMELY (10s, !node1.network.filter.check (bytes.data (), bytes.size ()));
}

// The test must be completed in less than 1 second
TEST (network, DISABLED_bandwidth_limiter_4_messages)
{
	celerix::test::system system;
	celerix::publish message{ celerix::dev::network_params.network, celerix::dev::genesis };
	auto message_size = message.to_bytes ()->size ();
	auto message_limit = 4; // must be multiple of the number of channels
	celerix::node_config node_config = system.default_config ();
	node_config.bandwidth_limit = message_limit * message_size;
	node_config.bandwidth_limit_burst_ratio = 1.0;
	auto & node = *system.add_node (node_config);
	celerix::transport::inproc::channel channel1{ node, node };
	celerix::transport::inproc::channel channel2{ node, node };
	// Send droppable messages
	for (auto i = 0; i < message_limit; i += 2) // number of channels
	{
		channel1.send (message, celerix::transport::traffic_type::test);
		channel2.send (message, celerix::transport::traffic_type::test);
	}
	// Only sent messages below limit, so we don't expect any drops
	ASSERT_TIMELY_EQ (1s, 0, node.stats.count (celerix::stat::type::drop, celerix::stat::detail::publish, celerix::stat::dir::out));

	// Send droppable message; drop stats should increase by one now
	channel1.send (message, celerix::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (1s, 1, node.stats.count (celerix::stat::type::drop, celerix::stat::detail::publish, celerix::stat::dir::out));

	// Send non-droppable message, i.e. drop stats should not increase
	channel2.send (message, celerix::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (1s, 1, node.stats.count (celerix::stat::type::drop, celerix::stat::detail::publish, celerix::stat::dir::out));
}

TEST (network, DISABLED_bandwidth_limiter_2_messages)
{
	celerix::test::system system;
	celerix::publish message{ celerix::dev::network_params.network, celerix::dev::genesis };
	auto message_size = message.to_bytes ()->size ();
	auto message_limit = 2; // must be multiple of the number of channels
	celerix::node_config node_config = system.default_config ();
	node_config.bandwidth_limit = message_limit * message_size;
	node_config.bandwidth_limit_burst_ratio = 1.0;
	auto & node = *system.add_node (node_config);
	celerix::transport::inproc::channel channel1{ node, node };
	celerix::transport::inproc::channel channel2{ node, node };
	// change the bandwidth settings, 2 packets will be dropped
	channel1.send (message, celerix::transport::traffic_type::test);
	channel2.send (message, celerix::transport::traffic_type::test);
	channel1.send (message, celerix::transport::traffic_type::test);
	channel2.send (message, celerix::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (1s, 2, node.stats.count (celerix::stat::type::drop, celerix::stat::detail::publish, celerix::stat::dir::out));
}

TEST (network, bandwidth_limiter_with_burst)
{
	celerix::test::system system;
	celerix::publish message{ celerix::dev::network_params.network, celerix::dev::genesis };
	auto message_size = message.to_bytes ()->size ();
	auto message_limit = 2; // must be multiple of the number of channels
	celerix::node_config node_config = system.default_config ();
	node_config.bandwidth_limit = message_limit * message_size;
	node_config.bandwidth_limit_burst_ratio = 4.0; // High burst
	auto & node = *system.add_node (node_config);
	celerix::transport::inproc::channel channel1{ node, node };
	celerix::transport::inproc::channel channel2{ node, node };
	// change the bandwidth settings, no packet will be dropped
	channel1.send (message, celerix::transport::traffic_type::test);
	channel2.send (message, celerix::transport::traffic_type::test);
	channel1.send (message, celerix::transport::traffic_type::test);
	channel2.send (message, celerix::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (1s, 0, node.stats.count (celerix::stat::type::drop, celerix::stat::detail::publish, celerix::stat::dir::out));
}

namespace celerix
{
TEST (peer_exclusion, validate)
{
	std::size_t max_size = 10;

	celerix::peer_exclusion excluded_peers{ max_size };

	for (auto i = 0; i < max_size + 1; ++i)
	{
		celerix::tcp_endpoint endpoint{ boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (i)), 0 };
		ASSERT_FALSE (excluded_peers.check (endpoint));
		ASSERT_EQ (1, excluded_peers.add (endpoint));
		ASSERT_FALSE (excluded_peers.check (endpoint));
	}

	// The oldest entry must have been removed, because we just overfilled the container
	ASSERT_EQ (max_size, excluded_peers.size ());
	celerix::tcp_endpoint oldest{ boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x0)), 0 };
	ASSERT_EQ (excluded_peers.score (oldest), 0);

	auto to_seconds = [] (std::chrono::steady_clock::time_point const & timepoint) {
		return static_cast<double> (std::chrono::duration_cast<std::chrono::seconds> (timepoint.time_since_epoch ()).count ());
	};

	// However, the rest of the entries should be present
	celerix::tcp_endpoint first{ boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x1)), 0 };
	ASSERT_NE (excluded_peers.score (first), 0);

	celerix::tcp_endpoint second{ boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x2)), 0 };
	ASSERT_NE (excluded_peers.score (second), 0);

	// Check exclusion times
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours), to_seconds (excluded_peers.until (second)), 2);
	ASSERT_EQ (2, excluded_peers.add (second));
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours), to_seconds (excluded_peers.until (second)), 2);
	ASSERT_EQ (3, excluded_peers.add (second));
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours * 3 * 2), to_seconds (excluded_peers.until (second)), 2);
	ASSERT_EQ (max_size, excluded_peers.size ());
}
}

TEST (network, tcp_no_accept_excluded_peers)
{
	celerix::test::system system (1);
	auto node0 (system.nodes[0]);
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
	node1->start ();
	system.nodes.push_back (node1);
	auto endpoint1_tcp (celerix::transport::map_endpoint_to_tcp (node1->network.endpoint ()));
	while (!node0->network.excluded_peers.check (endpoint1_tcp))
	{
		node0->network.excluded_peers.add (endpoint1_tcp);
	}
	ASSERT_EQ (0, node0->stats.count (celerix::stat::type::tcp_listener_rejected, celerix::stat::detail::excluded));
	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY (5s, node0->stats.count (celerix::stat::type::tcp_listener_rejected, celerix::stat::detail::excluded) >= 1);
	ASSERT_EQ (nullptr, node0->network.find_node_id (node1->get_node_id ()));

	// Should not actively reachout to excluded peers
	ASSERT_FALSE (node0->network.track_reachout (node1->network.endpoint ()));

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
	ASSERT_TIMELY_EQ (5s, node0->network.size (), 1);
}

TEST (network, cleanup_purge)
{
	auto test_start = std::chrono::steady_clock::now ();

	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);

	auto node2 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
	node2->start ();
	system.nodes.push_back (node2);

	ASSERT_EQ (0, node1.network.size ());
	node1.network.cleanup (test_start);
	ASSERT_EQ (0, node1.network.size ());

	node1.network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (0, node1.network.size ());

	node1.network.merge_peer (node2->network.endpoint ());

	ASSERT_TIMELY_EQ (5s, node1.network.size (), 1);

	node1.network.cleanup (test_start);
	ASSERT_EQ (1, node1.network.size ());
	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::tcp_channels_purge));

	node1.network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (1, node1.stats.count (celerix::stat::type::tcp_channels_purge, celerix::stat::detail::idle));
}

TEST (network, loopback_channel)
{
	celerix::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	celerix::transport::inproc::channel channel1 (node1, node1);
	ASSERT_EQ (channel1.get_type (), celerix::transport::transport_type::loopback);
	ASSERT_EQ (channel1.get_remote_endpoint (), node1.network.endpoint ());
	ASSERT_EQ (channel1.get_network_version (), node1.network_params.network.protocol_version);
	ASSERT_EQ (channel1.get_node_id (), node1.node_id.pub);
	ASSERT_EQ (channel1.get_node_id_optional ().value_or (0), node1.node_id.pub);
	celerix::transport::inproc::channel channel2 (node2, node2);
	++node1.network.port;
	ASSERT_NE (channel1.get_remote_endpoint (), node1.network.endpoint ());
}

// Ensure the network filters messages with the incorrect magic number
TEST (network, filter_invalid_network_bytes)
{
	celerix::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];

	// find the comms channel that goes from node2 to node1
	auto channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// send a keepalive, from node2 to node1, with the wrong network bytes
	celerix::keepalive keepalive{ celerix::dev::network_params.network };
	const_cast<celerix::networks &> (keepalive.header.network) = celerix::networks::invalid;
	channel->send (keepalive, celerix::transport::traffic_type::test);

	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (celerix::stat::type::error, celerix::stat::detail::invalid_network));
}

// Ensure the network filters messages with the incorrect minimum version
TEST (network, filter_invalid_version_using)
{
	celerix::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];

	// find the comms channel that goes from node2 to node1
	auto channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// send a keepalive, from node2 to node1, with the wrong version_using
	celerix::keepalive keepalive{ celerix::dev::network_params.network };
	const_cast<uint8_t &> (keepalive.header.version_using) = celerix::dev::network_params.network.protocol_version_min - 1;
	channel->send (keepalive, celerix::transport::traffic_type::test);

	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (celerix::stat::type::error, celerix::stat::detail::outdated_version));
}

TEST (network, fill_keepalive_self)
{
	celerix::test::system system{ 2 };

	auto get_keepalive = [&system] (celerix::node & node) {
		std::array<celerix::endpoint, 8> target;
		node.network.fill_keepalive_self (target);
		return target;
	};

	ASSERT_TIMELY_EQ (5s, get_keepalive (system.node (0))[2].port (), system.nodes[1]->network.port);
}

TEST (network, reconnect_cached)
{
	celerix::test::system system;

	celerix::node_flags flags;
	// Disable non realtime sockets
	flags.disable_bootstrap_bulk_push_client = true;
	flags.disable_bootstrap_bulk_pull_server = true;
	flags.disable_bootstrap_listener = true;
	flags.disable_lazy_bootstrap = true;
	flags.disable_legacy_bootstrap = true;
	flags.disable_wallet_bootstrap = true;

	auto & node1 = *system.add_node (flags);
	auto & node2 = *system.add_node (flags);

	ASSERT_EQ (node1.network.size (), 1);
	ASSERT_EQ (node2.network.size (), 1);

	auto channels1 = node1.network.list ();
	auto channels2 = node2.network.list ();
	ASSERT_EQ (channels1.size (), 1);
	ASSERT_EQ (channels2.size (), 1);
	auto channel1 = channels1.front ();
	auto channel2 = channels2.front ();

	// Enusre current peers are cached
	node1.peer_history.trigger ();
	node2.peer_history.trigger ();
	ASSERT_TIMELY_EQ (5s, node1.peer_history.size (), 1);
	ASSERT_TIMELY_EQ (5s, node2.peer_history.size (), 1);

	// Kill channels
	channel1->close ();
	channel2->close ();

	auto channel_exists = [] (auto & node, auto & channel) {
		auto channels = node.network.list ();
		return std::find (channels.begin (), channels.end (), channel) != channels.end ();
	};

	ASSERT_TIMELY (5s, !channel_exists (node1, channel1));
	ASSERT_TIMELY (5s, !channel_exists (node2, channel2));

	// Peers should reconnect after a while
	ASSERT_TIMELY_EQ (5s, node1.network.size (), 1);
	ASSERT_TIMELY_EQ (5s, node2.network.size (), 1);
	ASSERT_TRUE (node1.network.find_node_id (node2.node_id.pub));
	ASSERT_TRUE (node2.network.find_node_id (node1.node_id.pub));
}

/*
 * Tests that channel and channel container removes channels with dead local sockets
 */
TEST (network, purge_dead_channel)
{
	celerix::test::system system;

	celerix::node_flags flags;
	// Disable non realtime sockets
	flags.disable_bootstrap_bulk_push_client = true;
	flags.disable_bootstrap_bulk_pull_server = true;
	flags.disable_bootstrap_listener = true;
	flags.disable_lazy_bootstrap = true;
	flags.disable_legacy_bootstrap = true;
	flags.disable_wallet_bootstrap = true;

	auto & node1 = *system.add_node (flags);

	node1.observers.socket_connected.add ([&] (auto const & socket) {
		system.logger.debug (celerix::log::type::test, "Connected socket: {}", celerix::streamed (socket));
	});

	auto & node2 = *system.add_node (flags);

	ASSERT_EQ (node1.network.size (), 1);
	ASSERT_ALWAYS_EQ (500ms, node1.network.size (), 1);

	// Store reference to the only channel
	auto channels = node1.network.list ();
	ASSERT_EQ (channels.size (), 1);
	auto channel = channels.front ();
	ASSERT_TRUE (channel);

	auto sockets = node1.tcp_listener.sockets ();
	ASSERT_EQ (sockets.size (), 1);
	auto socket = sockets.front ();
	ASSERT_TRUE (socket);

	// When socket is dead ensure channel knows about that
	ASSERT_TRUE (channel->alive ());
	socket->close ();
	ASSERT_TIMELY (10s, !channel->alive ());

	auto channel_exists = [] (auto & node, auto & channel) {
		auto channels = node.network.list ();
		return std::find (channels.begin (), channels.end (), channel) != channels.end ();
	};
	ASSERT_TIMELY (5s, !channel_exists (node1, channel));
}

/*
 * Tests that channel and channel container removes channels with dead remote sockets
 */
TEST (network, purge_dead_channel_remote)
{
	celerix::test::system system;

	celerix::node_flags flags;
	// Disable non realtime sockets
	flags.disable_bootstrap_bulk_push_client = true;
	flags.disable_bootstrap_bulk_pull_server = true;
	flags.disable_bootstrap_listener = true;
	flags.disable_lazy_bootstrap = true;
	flags.disable_legacy_bootstrap = true;
	flags.disable_wallet_bootstrap = true;

	auto & node1 = *system.add_node (flags);
	auto & node2 = *system.add_node (flags);

	node2.observers.socket_connected.add ([&] (auto const & socket) {
		system.logger.debug (celerix::log::type::test, "Connected socket: {}", celerix::streamed (socket));
	});

	ASSERT_EQ (node1.network.size (), 1);
	ASSERT_EQ (node2.network.size (), 1);
	ASSERT_ALWAYS_EQ (500ms, std::min (node1.network.size (), node2.network.size ()), 1);

	// Store reference to the only channel
	auto channels = node2.network.list ();
	ASSERT_EQ (channels.size (), 1);
	auto channel = channels.front ();
	ASSERT_TRUE (channel);

	auto sockets = node1.tcp_listener.sockets ();
	ASSERT_EQ (sockets.size (), 1);
	auto socket = sockets.front ();
	ASSERT_TRUE (socket);

	// When remote socket is dead ensure channel knows about that
	ASSERT_TRUE (channel->alive ());
	socket->close ();
	ASSERT_TIMELY (5s, !channel->alive ());

	auto channel_exists = [] (auto & node, auto & channel) {
		auto channels = node.network.list ();
		return std::find (channels.begin (), channels.end (), channel) != channels.end ();
	};
	ASSERT_TIMELY (5s, !channel_exists (node2, channel));
}
