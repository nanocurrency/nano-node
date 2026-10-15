/*
 * Benchmarks of what sending a message costs the thread that sends it: serializing it, handing it to the channels of a flood and picking those channels.
 * Every benchmark reports the time and the heap allocations per message or per flood, so a change can be judged on both.
 *
 * The fixture is a dev network node. A flood sends to fake channels, which serialize the message and drop it, so the cost of a flood is the sender's own work.
 * The channels of a flood are picked among `channel_count` TCP channels to local connections that carry nothing, so only the selection is measured.
 */

#include <nano/benchmarks/allocations.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/tcp_channels.hpp>
#include <nano/node/transport/tcp_server.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/system.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <deque>
#include <map>
#include <memory>
#include <mutex>

#include <benchmark/benchmark.h>

namespace
{
constexpr size_t fanout_count = 8; // Channels a flood sends to, about the fanout on the live network
constexpr size_t channel_count = 256; // Channels a flood picks from, about the peer count on the live network

nano::block_hash random_hash ()
{
	nano::block_hash hash;
	nano::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
	return hash;
}

// The benchmarks' main leaves the network alone, a node must never start on another one from here
struct dev_network
{
	dev_network ()
	{
		nano::force_nano_dev_network ();
	}
};

// Connections to a local acceptor, held open from the far side, so the node has channels that carry no traffic at all
class local_connections final
{
public:
	explicit local_connections (boost::asio::io_context & io_ctx) :
		acceptor{ io_ctx, boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), 0 } }
	{
	}

	// Opens one connection and returns the node's end of it, which sees a distinct remote port per connection
	boost::asio::ip::tcp::socket connect ()
	{
		auto & client = clients.emplace_back (acceptor.get_executor ());
		client.connect (acceptor.local_endpoint ());
		return acceptor.accept ();
	}

private:
	boost::asio::ip::tcp::acceptor acceptor;
	std::deque<boost::asio::ip::tcp::socket> clients; // Closing a client would close the node's end too
};

class sending_fixture
{
public:
	sending_fixture ()
	{
		nano::node_flags flags;
		flags.disable_connection_cleanup = true; // The sink channels never send anything, which would get them purged
		node = system.add_node (system.default_config (), flags);

		for (size_t i = 0; i < fanout_count; ++i)
		{
			fake_channels.push_back (std::make_shared<nano::transport::fake::channel> (*node));
		}

		connections = std::make_unique<local_connections> (node->io_ctx);
		for (size_t i = 0; i < channel_count; ++i)
		{
			auto socket = std::make_shared<nano::transport::tcp_socket> (*node, connections->connect (), nano::transport::socket_endpoint::server);
			auto server = std::make_shared<nano::transport::tcp_server> (*node, socket);
			server->start ();
			// Every channel claims its own node id, a shared one would make it a duplicate of the first
			nano::transport::peer_info const peer{ .node_id = nano::keypair{}.pub, .protocol_version = node->network_params.network.protocol_version };
			auto const created = node->network.tcp_channels.create (socket, server, peer);
			release_assert (created.channel != nullptr);
		}
		release_assert (node->network.size () == channel_count);

		nano::state_block_builder builder;
		block = builder.make_block ()
				.account (rep.pub)
				.previous (random_hash ())
				.representative (rep.pub)
				.balance (1)
				.link (random_hash ())
				.sign (rep.prv, rep.pub)
				.work (0)
				.build ();
	}

	// A vote on `hashes` random hashes, built once per size
	std::shared_ptr<nano::vote> const & vote (size_t hashes)
	{
		std::lock_guard guard{ mutex };
		auto & vote = votes[hashes];
		if (!vote)
		{
			std::vector<nano::block_hash> hashes_l;
			for (size_t i = 0; i < hashes; ++i)
			{
				hashes_l.push_back (random_hash ());
			}
			vote = std::make_shared<nano::vote> (rep.pub, rep.prv, 1, 0, hashes_l);
		}
		return vote;
	}

public:
	dev_network network;
	nano::test::system system;
	std::shared_ptr<nano::node> node;
	std::vector<std::shared_ptr<nano::transport::channel>> fake_channels;
	std::shared_ptr<nano::block> block;

private:
	nano::keypair rep;
	std::unique_ptr<local_connections> connections;
	std::mutex mutex; // Benchmark threads ask for their vote at the same time, it is built once under this lock
	std::map<size_t, std::shared_ptr<nano::vote>> votes;
};

// Built on first use, connecting the channels takes a moment
sending_fixture & shared_fixture ()
{
	static sending_fixture instance;
	return instance;
}
}

// Serializing a vote into the buffer a channel sends
static void BM_message_serialize (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	nano::messages::confirm_ack const message{ fixture.node->network_params.network, fixture.vote (state.range (0)) };
	nano::benchmarks::allocation_counter allocations{ "message" };
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (message.to_shared_const_buffer ());
	}
	allocations.report (state);
}

// Serializing a block into the buffer a channel sends
static void BM_message_serialize_block (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	nano::messages::publish const message{ fixture.node->network_params.network, fixture.block };
	nano::benchmarks::allocation_counter allocations{ "message" };
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (message.to_shared_const_buffer ());
	}
	allocations.report (state);
}

// Handing a vote to the channels of a flood, which serialize it and drop it
static void BM_flood_send (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	nano::messages::confirm_ack const message{ fixture.node->network_params.network, fixture.vote (state.range (0)) };
	nano::benchmarks::allocation_counter allocations{ "flood" };
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (fixture.node->network.send_to (message, nano::transport::traffic_type::vote, fixture.fake_channels));
	}
	allocations.report (state);
}

// Picking the channels of a flood among all peers
static void BM_flood_targets (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto & network = fixture.node->network;
	auto const type = nano::transport::traffic_type::vote;
	nano::benchmarks::allocation_counter allocations{ "flood" };
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (network.sample (network.fanout (), [type] (auto const & channel) {
			return !channel->max (type);
		}));
	}
	allocations.report (state);
}

BENCHMARK (BM_message_serialize)->ArgName ("hashes")->Arg (1)->Arg (16)->Arg (255)->Threads (1)->Threads (4)->UseRealTime ();
BENCHMARK (BM_message_serialize_block)->Threads (1)->Threads (4)->UseRealTime ();
BENCHMARK (BM_flood_send)->ArgName ("hashes")->Arg (1)->Arg (16)->Arg (255)->Threads (1)->Threads (4)->UseRealTime ();
BENCHMARK (BM_flood_targets)->Threads (1)->Threads (4)->UseRealTime ();
