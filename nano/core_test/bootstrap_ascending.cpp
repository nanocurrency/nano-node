#include <nano/lib/rpcconfig.hpp>
#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <boost/format.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace std::chrono_literals;

TEST (account_sets, construction)
{
	nano::bootstrap::bootstrap_ascending::account_sets sets;
}

TEST (account_sets, empty_blocked)
{
	nano::account account{ 1 };
	nano::bootstrap::bootstrap_ascending::account_sets sets;
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, block)
{
	nano::account account{ 1 };
	nano::bootstrap::bootstrap_ascending::account_sets sets;
	sets.block (account);
	ASSERT_TRUE (sets.blocked (account));
}

TEST (account_sets, unblock)
{
	nano::account account{ 1 };
	nano::bootstrap::bootstrap_ascending::account_sets sets;
	sets.block (account);
	sets.unblock (account);
	ASSERT_FALSE (sets.blocked (account));
}

/**
 * Tests basic construction of a bootstrap_ascending attempt
 */
TEST (bootstrap_ascending, construction)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto attempt = std::make_shared<nano::bootstrap::bootstrap_ascending> (node.shared (), 0, "");
}

/**
 * Tests that bootstrap_ascending attempt can run and complete
 */
TEST (bootstrap_ascending, start_stop)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto attempt = node.bootstrap_initiator.bootstrap_ascending ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate_ascending, nano::stat::dir::out) > 0);
}

/**
 * Tests the base case for returning
 */
TEST (bootstrap_ascending, account_base)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node0.process (*send1).code);
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);
}

/**
 * Tests that bootstrap_ascending will return multiple new blocks in-order
 */
TEST (bootstrap_ascending, account_inductive)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	std::cerr << "Genesis: " << nano::dev::genesis->hash ().to_string () << std::endl;
	std::cerr << "Send1: " << send1->hash ().to_string () << std::endl;
	std::cerr << "Send2: " << send2->hash ().to_string () << std::endl;
	ASSERT_EQ (nano::process_result::progress, node0.process (*send1).code);
	ASSERT_EQ (nano::process_result::progress, node0.process (*send2).code);
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (50s, node1.block (send2->hash ()) != nullptr);
}

/**
 * Tests that bootstrap_ascending will return multiple new blocks in-order
 */
TEST (bootstrap_ascending, trace_base)
{
	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto receive1 = builder.make_block ()
					.account (key.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.link (send1->hash ())
					.balance (1)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build_shared ();
	std::cerr << "Genesis key: " << nano::dev::genesis_key.pub.to_account () << std::endl;
	std::cerr << "Key: " << key.pub.to_account () << std::endl;
	std::cerr << "Genesis: " << nano::dev::genesis->hash ().to_string () << std::endl;
	std::cerr << "send1: " << send1->hash ().to_string () << std::endl;
	std::cerr << "receive1: " << receive1->hash ().to_string () << std::endl;
	auto & node1 = *system.add_node ();
	std::cerr << "--------------- Start ---------------\n";
	ASSERT_EQ (nano::process_result::progress, node0.process (*send1).code);
	ASSERT_EQ (nano::process_result::progress, node0.process (*receive1).code);
	ASSERT_EQ (node1.store.pending.begin (node1.store.tx_begin_read (), nano::pending_key{ key.pub, 0 }), node1.store.pending.end ());
	std::cerr << "node0: " << node0.network.endpoint () << std::endl;
	std::cerr << "node1: " << node1.network.endpoint () << std::endl;
	ASSERT_TIMELY (10s, node1.block (receive1->hash ()) != nullptr);
}

TEST (bootstrap_ascending, profile)
{
	auto path_server = "/Users/clemahieu/Library/NanoBeta";
	auto path_client = "/Users/clemahieu/NanoBeta2";
	uint16_t rpc_port = 55000;
	nano::test::system system;
	nano::thread_runner runner{ system.io_ctx, 2 };
	nano::network_params params_beta { nano::networks::nano_beta_network };

	// Set up client and server nodes
	nano::node_config config_server{ params_beta };
	config_server.preconfigured_peers.clear ();
	nano::node_flags flags_server;
	flags_server.disable_legacy_bootstrap = true;
	flags_server.disable_wallet_bootstrap = true;
	flags_server.disable_add_initial_peers = true;
	flags_server.disable_ongoing_bootstrap = true;
	auto server = std::make_shared<nano::node> (system.io_ctx, path_server, config_server, system.work);
	system.nodes.push_back (server);
	server->start ();
	nano::node_config config_client{ params_beta };
	config_client.preconfigured_peers.clear ();
	nano::node_flags flags_client;
	flags_client.disable_legacy_bootstrap = true;
	flags_client.disable_wallet_bootstrap = true;
	flags_client.disable_add_initial_peers = true;
	flags_client.disable_ongoing_bootstrap = true;
	config_client.ipc_config.transport_tcp.enabled = true;
	auto client = std::make_shared<nano::node> (system.io_ctx, path_client, config_client, system.work);
	system.nodes.push_back (client);
	client->start ();

	// Set up client RPC
	nano::node_rpc_config node_rpc_config;
	nano::rpc_config rpc_config{ params_beta.network, rpc_port, true };
	nano::ipc::ipc_server ipc (*client, node_rpc_config);
	nano::ipc_rpc_processor ipc_rpc_processor (system.io_ctx, rpc_config);
	nano::rpc rpc (system.io_ctx, rpc_config, ipc_rpc_processor);
	rpc.start ();

	nano::test::establish_tcp (system, *client, server->network.endpoint ());
	//client->bootstrap_initiator.bootstrap_ascending ();
	client->bootstrap_initiator.bootstrap ();
	
	std::cerr << boost::str (boost::format ("Server: %1%, client: %2%\n") % server->network.port.load () % client->network.port.load ());
	int junk;
	std::cin >> junk;
	server->stop ();
	client->stop ();
}
