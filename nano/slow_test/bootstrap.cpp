#include <nano/lib/rpcconfig.hpp>
#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/rate_observer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/format.hpp>

#include <thread>

using namespace std::chrono_literals;

namespace
{
void wait_for_key ()
{
	int junk;
	std::cin >> junk;
}
}

TEST (bootstrap_ascending, profile)
{
	// this test does not do cementing, so we need to disable waiting for the confirmed frontier to catch up
	nano::bootstrap::bootstrap_ascending::optimistic_pulling = true;

	uint16_t rpc_port = 55000;
	nano::test::system system;
	nano::thread_runner runner{ system.io_ctx, 2 };
	auto net_string = nano::get_env_or_default ("NANO_ASCENDBOOT_NETWORK", "live");
	nano::networks network = nano::network_constants::network_string_to_enum (net_string);
	ASSERT_NE (network, nano::networks::invalid);
	nano::network_params network_params{ network };

	// Select a ledger for the server node, default to ledger from standard network location
	auto path_server = nano::get_env_or_default ("NANO_ASCENDBOOT_LEDGER", nano::working_path (network).string ());

	// Set up client and server nodes
	nano::node_config config_server{ network_params };
	config_server.preconfigured_peers.clear ();
	config_server.disable_legacy_bootstrap = true;
	config_server.disable_wallet_bootstrap = true;
	config_server.disable_ongoing_bootstrap = true;
	nano::node_flags flags_server;
	flags_server.disable_add_initial_peers = true;
	auto server = std::make_shared<nano::node> (system.io_ctx, path_server, config_server, system.work, flags_server);
	system.nodes.push_back (server);
	server->start ();

	nano::node_config config_client{ network_params };
	config_client.preconfigured_peers.clear ();
	config_client.disable_legacy_bootstrap = true;
	config_client.disable_wallet_bootstrap = true;
	config_client.disable_legacy_bootstrap = true;
	config_client.ipc_config.transport_tcp.enabled = true;
	config_client.lmdb_config.sync = nano::lmdb_config::sync_strategy::nosync_unsafe;
	nano::node_flags flags_client;
	flags_client.disable_add_initial_peers = true;
	auto client = system.add_node (config_client, flags_client);

	// Set up client RPC
	nano::node_rpc_config node_rpc_config;
	nano::rpc_config rpc_config{ network_params.network, rpc_port, true };
	nano::ipc::ipc_server ipc (*client, node_rpc_config);
	nano::ipc_rpc_processor ipc_rpc_processor (system.io_ctx, rpc_config);
	nano::rpc rpc (system.io_ctx, rpc_config, ipc_rpc_processor);
	rpc.start ();

	//client->bootstrap_initiator.bootstrap_ascending ();
	//	client->bootstrap_initiator.bootstrap ();

	std::cerr << boost::str (boost::format ("Server: %1%, client: %2%\n") % server->network.port.load () % client->network.port.load ());

	std::cout << "Server: count: " << server->ledger.cache.block_count << std::endl;

	nano::test::rate_observer rate;
	rate.observe ("count", [&] () { return client->ledger.cache.block_count.load (); });
	rate.observe ("unchecked", [&] () { return client->unchecked.count (); });
	rate.observe ("block_processor", [&] () { return client->block_processor.size (); });
	rate.background_print (3s);

	while (true)
		std::this_thread::sleep_for (1s);
	wait_for_key ();

	server->stop ();
	client->stop ();
}
