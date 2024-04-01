#include <nano/lib/rpcconfig.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/rate_observer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

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
	rpc_wrapper (nano::test::system & system, nano::node & node, uint16_t port) :
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
	nano::node_rpc_config node_rpc_config;
	nano::rpc_config rpc_config;
	nano::ipc::ipc_server ipc;
	nano::ipc_rpc_processor ipc_rpc_processor;
	nano::rpc rpc;
};

std::unique_ptr<rpc_wrapper> start_rpc (nano::test::system & system, nano::node & node, uint16_t port)
{
	auto rpc = std::make_unique<rpc_wrapper> (system, node, port);
	rpc->start ();
	return rpc;
}
}

TEST (bootstrap_ascending, profile)
{
	nano::test::system system;
	nano::thread_runner runner{ system.io_ctx, 2 };
	nano::networks network = nano::networks::nano_beta_network;
	nano::network_params network_params{ network };

	// Set up client and server nodes
	nano::node_config config_server{ network_params };
	config_server.preconfigured_peers.clear ();
	config_server.bandwidth_limit = 0; // Unlimited server bandwidth
	nano::node_flags flags_server;
	flags_server.disable_legacy_bootstrap = true;
	flags_server.disable_wallet_bootstrap = true;
	flags_server.disable_add_initial_peers = true;
	flags_server.disable_ongoing_bootstrap = true;
	flags_server.disable_ascending_bootstrap = true;
	auto data_path_server = nano::working_path (network);
	// auto data_path_server = "";
	auto server = std::make_shared<nano::node> (system.io_ctx, data_path_server, config_server, system.work, flags_server);
	system.nodes.push_back (server);
	server->start ();

	nano::node_config config_client{ network_params };
	config_client.preconfigured_peers.clear ();
	config_client.bandwidth_limit = 0; // Unlimited server bandwidth
	nano::node_flags flags_client;
	flags_client.disable_legacy_bootstrap = true;
	flags_client.disable_wallet_bootstrap = true;
	flags_client.disable_add_initial_peers = true;
	flags_client.disable_ongoing_bootstrap = true;
	config_client.ipc_config.transport_tcp.enabled = true;
	// Disable database integrity safety for higher throughput
	config_client.lmdb_config.sync = nano::lmdb_config::sync_strategy::nosync_unsafe;
	// auto client = system.add_node (config_client, flags_client);

	// macos 16GB RAM disk:  diskutil erasevolume HFS+ "RAMDisk" `hdiutil attach -nomount ram://33554432`
	// auto data_path_client = "/Volumes/RAMDisk";
	auto data_path_client = nano::unique_path ();
	auto client = std::make_shared<nano::node> (system.io_ctx, data_path_client, config_client, system.work, flags_client);
	system.nodes.push_back (client);
	client->start ();

	// Set up RPC
	auto client_rpc = start_rpc (system, *server, 55000);
	auto server_rpc = start_rpc (system, *client, 55001);

	struct entry
	{
		nano::bootstrap_ascending::service::async_tag tag;
		std::shared_ptr<nano::transport::channel> request_channel;
		std::shared_ptr<nano::transport::channel> reply_channel;

		bool replied{ false };
		bool received{ false };
	};

	nano::mutex mutex;
	std::unordered_map<uint64_t, entry> requests;

	server->bootstrap_server.on_response.add ([&] (auto & response, auto & channel) {
		nano::lock_guard<nano::mutex> lock{ mutex };

		if (requests.count (response.id))
		{
			requests[response.id].replied = true;
			requests[response.id].reply_channel = channel;
		}
		else
		{
			std::cerr << "unknown response: " << response.id << std::endl;
		}
	});

	client->ascendboot.on_request.add ([&] (auto & tag, auto & channel) {
		nano::lock_guard<nano::mutex> lock{ mutex };

		requests[tag.id] = { tag, channel };
	});

	client->ascendboot.on_reply.add ([&] (auto & tag) {
		nano::lock_guard<nano::mutex> lock{ mutex };

		requests[tag.id].received = true;
	});

	/*client->ascendboot.on_timeout.add ([&] (auto & tag) {
		nano::lock_guard<nano::mutex> lock{ mutex };

		if (requests.count (tag.id))
		{
			auto entry = requests[tag.id];

			std::cerr << "timeout: "
					  << "replied: " << entry.replied
					  << " | "
					  << "recevied: " << entry.received
					  << " | "
					  << "request: " << entry.request_channel->to_string ()
					  << " ||| "
					  << "reply: " << (entry.reply_channel ? entry.reply_channel->to_string () : "null")
					  << std::endl;
		}
		else
		{
			std::cerr << "unknown timeout: " << tag.id << std::endl;
		}
	});*/

	std::cout << "server count: " << server->ledger.block_count () << std::endl;

	nano::test::rate_observer rate;
	rate.observe ("count", [&] () { return client->ledger.block_count (); });
	rate.observe ("unchecked", [&] () { return client->unchecked.count (); });
	rate.observe ("block_processor", [&] () { return client->block_processor.size (); });
	rate.observe ("priority", [&] () { return client->ascendboot.priority_size (); });
	rate.observe ("blocking", [&] () { return client->ascendboot.blocked_size (); });
	rate.observe (*client, nano::stat::type::bootstrap_ascending, nano::stat::detail::request, nano::stat::dir::out);
	rate.observe (*client, nano::stat::type::bootstrap_ascending, nano::stat::detail::reply, nano::stat::dir::in);
	rate.observe (*client, nano::stat::type::bootstrap_ascending, nano::stat::detail::blocks, nano::stat::dir::in);
	rate.observe (*server, nano::stat::type::bootstrap_server, nano::stat::detail::blocks, nano::stat::dir::out);
	rate.observe (*client, nano::stat::type::ledger, nano::stat::detail::old, nano::stat::dir::in);
	rate.observe (*client, nano::stat::type::ledger, nano::stat::detail::gap_epoch_open_pending, nano::stat::dir::in);
	rate.observe (*client, nano::stat::type::ledger, nano::stat::detail::gap_source, nano::stat::dir::in);
	rate.observe (*client, nano::stat::type::ledger, nano::stat::detail::gap_previous, nano::stat::dir::in);
	rate.background_print (3s);

	// wait_for_key ();
	while (true)
	{
		nano::test::establish_tcp (system, *client, server->network.endpoint ());
		std::this_thread::sleep_for (10s);
	}

	server->stop ();
	client->stop ();
}
