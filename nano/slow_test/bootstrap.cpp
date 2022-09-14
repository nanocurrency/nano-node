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
	//	const auto path_server = "/Users/clemahieu/Library/NanoBeta";

	uint16_t rpc_port = 55000;
	nano::test::system system;
	nano::thread_runner runner{ system.io_ctx, 2 };
	nano::networks network = nano::networks::nano_live_network;
	nano::network_params network_params{ network };

	// Set up client and server nodes
	nano::node_config config_server{ network_params };
	config_server.preconfigured_peers.clear ();
	nano::node_flags flags_server;
	flags_server.disable_legacy_bootstrap = true;
	flags_server.disable_wallet_bootstrap = true;
	flags_server.disable_add_initial_peers = true;
	flags_server.disable_ongoing_bootstrap = true;
	auto server = std::make_shared<nano::node> (system.io_ctx, nano::working_path (network), config_server, system.work);
	system.nodes.push_back (server);
	server->start ();

	nano::node_config config_client{ network_params };
	config_client.preconfigured_peers.clear ();
	nano::node_flags flags_client;
	flags_client.disable_legacy_bootstrap = true;
	flags_client.disable_wallet_bootstrap = true;
	flags_client.disable_add_initial_peers = true;
	flags_client.disable_ongoing_bootstrap = true;
	config_client.ipc_config.transport_tcp.enabled = true;
	auto client = system.add_node (config_client, flags_client);

	// Set up client RPC
	nano::node_rpc_config node_rpc_config;
	nano::rpc_config rpc_config{ network_params.network, rpc_port, true };
	nano::ipc::ipc_server ipc (*client, node_rpc_config);
	nano::ipc_rpc_processor ipc_rpc_processor (system.io_ctx, rpc_config);
	nano::rpc rpc (system.io_ctx, rpc_config, ipc_rpc_processor);
	rpc.start ();

	client->bootstrap_initiator.bootstrap_ascending ();
	//	client->bootstrap_initiator.bootstrap ();

	std::cerr << boost::str (boost::format ("Server: %1%, client: %2%\n") % server->network.port.load () % client->network.port.load ());

	std::cout << "Server: count: " << server->ledger.cache.block_count << std::endl;

	nano::test::rate_observer rate;
	rate.observe ("count", [&] () { return client->ledger.cache.block_count.load (); });
	rate.observe ("unchecked", [&] () { return client->unchecked.count (client->store.tx_begin_read ()); });
	rate.background_print (3s);

	wait_for_key ();

	server->stop ();
	client->stop ();
}
