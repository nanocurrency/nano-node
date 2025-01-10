#include <celerix/lib/rpcconfig.hpp>
#include <celerix/lib/thread_runner.hpp>
#include <celerix/node/bootstrap/bootstrap_server.hpp>
#include <celerix/node/bootstrap/bootstrap_service.hpp>
#include <celerix/node/ipc/ipc_server.hpp>
#include <celerix/node/json_handler.hpp>
#include <celerix/node/transport/transport.hpp>
#include <celerix/rpc/rpc.hpp>
#include <celerix/rpc/rpc_request_processor.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/rate_observer.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace std::chrono_literals;

namespace
{
void wait_for_key ()
{
	int junk;
	std::cin >> junk;
}

class rpc_wrapper
{
public:
	rpc_wrapper (celerix::test::system & system, celerix::node & node, uint16_t port) :
		node_rpc_config{},
		rpc_config{ node.network_params.network, port, true },
		ipc{ node, node_rpc_config },
		ipc_rpc_processor{ *system.io_ctx, rpc_config },
		rpc{ system.io_ctx, rpc_config, ipc_rpc_processor }
	{
	}

	void start ()
	{
		rpc.start ();
	}

public:
	celerix::node_rpc_config node_rpc_config;
	celerix::rpc_config rpc_config;
	celerix::ipc::ipc_server ipc;
	celerix::ipc_rpc_processor ipc_rpc_processor;
	celerix::rpc rpc;
};

std::unique_ptr<rpc_wrapper> start_rpc (celerix::test::system & system, celerix::node & node, uint16_t port)
{
	auto rpc = std::make_unique<rpc_wrapper> (system, node, port);
	rpc->start ();
	return rpc;
}
}

TEST (bootstrap, profile)
{
	celerix::test::system system;
	celerix::thread_runner runner{ system.io_ctx, system.logger, 2 };
	celerix::networks network = celerix::networks::celerix_beta_network;
	celerix::network_params network_params{ network };

	// Set up client and server nodes
	celerix::node_config config_server{ network_params };
	config_server.preconfigured_peers.clear ();
	config_server.bandwidth_limit = 0; // Unlimited server bandwidth
	config_server.bootstrap.enable = false;
	celerix::node_flags flags_server;
	flags_server.disable_legacy_bootstrap = true;
	flags_server.disable_wallet_bootstrap = true;
	flags_server.disable_add_initial_peers = true;
	flags_server.disable_ongoing_bootstrap = true;
	auto data_path_server = celerix::working_path (network);
	// auto data_path_server = "";
	auto server = std::make_shared<celerix::node> (system.io_ctx, data_path_server, config_server, system.work, flags_server);
	system.nodes.push_back (server);
	server->start ();

	celerix::node_config config_client{ network_params };
	config_client.preconfigured_peers.clear ();
	config_client.bandwidth_limit = 0; // Unlimited server bandwidth
	celerix::node_flags flags_client;
	flags_client.disable_legacy_bootstrap = true;
	flags_client.disable_wallet_bootstrap = true;
	flags_client.disable_add_initial_peers = true;
	flags_client.disable_ongoing_bootstrap = true;
	config_client.ipc_config.transport_tcp.enabled = true;
	// Disable database integrity safety for higher throughput
	config_client.lmdb_config.sync = celerix::lmdb_config::sync_strategy::nosync_unsafe;
	// auto client = system.add_node (config_client, flags_client);

	// macos 16GB RAM disk:  diskutil erasevolume HFS+ "RAMDisk" `hdiutil attach -nomount ram://33554432`
	// auto data_path_client = "/Volumes/RAMDisk";
	auto data_path_client = celerix::unique_path ();
	auto client = std::make_shared<celerix::node> (system.io_ctx, data_path_client, config_client, system.work, flags_client);
	system.nodes.push_back (client);
	client->start ();

	// Set up RPC
	auto client_rpc = start_rpc (system, *server, 55000);
	auto server_rpc = start_rpc (system, *client, 55001);

	celerix::mutex mutex;

	std::cout << "server count: " << server->ledger.block_count () << std::endl;

	celerix::test::rate_observer rate;
	rate.observe ("count", [&] () { return client->ledger.block_count (); });
	rate.observe ("unchecked", [&] () { return client->unchecked.count (); });
	rate.observe ("block_processor", [&] () { return client->block_processor.size (); });
	rate.observe ("priority", [&] () { return client->bootstrap.priority_size (); });
	rate.observe ("blocking", [&] () { return client->bootstrap.blocked_size (); });
	rate.observe (*client, celerix::stat::type::bootstrap, celerix::stat::detail::request, celerix::stat::dir::out);
	rate.observe (*client, celerix::stat::type::bootstrap, celerix::stat::detail::reply, celerix::stat::dir::in);
	rate.observe (*client, celerix::stat::type::bootstrap, celerix::stat::detail::blocks, celerix::stat::dir::in);
	rate.observe (*server, celerix::stat::type::bootstrap_server, celerix::stat::detail::blocks, celerix::stat::dir::out);
	rate.observe (*client, celerix::stat::type::ledger, celerix::stat::detail::old, celerix::stat::dir::in);
	rate.observe (*client, celerix::stat::type::ledger, celerix::stat::detail::gap_epoch_open_pending, celerix::stat::dir::in);
	rate.observe (*client, celerix::stat::type::ledger, celerix::stat::detail::gap_source, celerix::stat::dir::in);
	rate.observe (*client, celerix::stat::type::ledger, celerix::stat::detail::gap_previous, celerix::stat::dir::in);
	rate.background_print (3s);

	// wait_for_key ();
	while (true)
	{
		celerix::test::establish_tcp (system, *client, server->network.endpoint ());
		std::this_thread::sleep_for (10s);
	}

	server->stop ();
	client->stop ();
}
