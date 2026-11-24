#include <nano/lib/files.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_channels.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

std::shared_ptr<nano::transport::tcp_channel> nano::test::establish_tcp (nano::test::system & system, nano::node & node, nano::endpoint const & endpoint)
{
	debug_assert (node.network.endpoint () != endpoint && "Establishing TCP to self is not allowed");

	std::shared_ptr<nano::transport::tcp_channel> result;
	debug_assert (!node.flags.disable_tcp_realtime);
	node.network.tcp_channels.start_tcp (endpoint);
	auto error = system.poll_until_true (2s, [&result, &node, &endpoint] {
		result = node.network.tcp_channels.find_channel (nano::transport::map_endpoint_to_tcp (endpoint));
		return result != nullptr;
	});
	return result;
}

// TODO: merge with make_disconnected_node
std::shared_ptr<nano::node> nano::test::add_outer_node (nano::test::system & system_a, nano::node_config const & config_a, nano::node_flags const & flags_a)
{
	auto outer_node = std::make_shared<nano::node> (nano::unique_path (), config_a, system_a.work, flags_a);
	outer_node->start ();
	system_a.disconnected_nodes.push_back (outer_node);
	return outer_node;
}

std::shared_ptr<nano::node> nano::test::add_outer_node (nano::test::system & system_a, nano::node_config const & config_a)
{
	return add_outer_node (system_a, config_a, nano::node_flags{});
}

// TODO: merge with make_disconnected_node
std::shared_ptr<nano::node> nano::test::add_outer_node (nano::test::system & system_a, nano::node_flags const & flags_a)
{
	auto outer_node = std::make_shared<nano::node> (system_a.get_available_port (), nano::unique_path (), system_a.work, flags_a);
	outer_node->start ();
	system_a.disconnected_nodes.push_back (outer_node);
	return outer_node;
}

std::shared_ptr<nano::node> nano::test::add_outer_node (nano::test::system & system_a)
{
	return add_outer_node (system_a, nano::node_flags{});
}

uint16_t nano::test::pick_free_port ()
{
	// No SO_REUSEADDR, so the kernel cannot pick a port another socket holds on a specific address
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor{ io_ctx };
	acceptor.open (boost::asio::ip::tcp::v6 ());
	acceptor.bind ({ boost::asio::ip::tcp::v6 (), 0 });

	auto const port = acceptor.local_endpoint ().port ();
	return port;
}

void nano::test::saturate_network (nano::node & node, nano::transport::traffic_type type)
{
	for (auto const & channel : node.network.list ())
	{
		channel->saturate (type);
	}
}

void nano::test::unsaturate_network (nano::node & node, nano::transport::traffic_type type)
{
	for (auto const & channel : node.network.list ())
	{
		channel->unsaturate (type);
	}
}
