#include <nano/node/node.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

std::shared_ptr<nano::transport::channel_tcp> nano::test::establish_tcp (nano::test::system & system, nano::node & node, nano::endpoint const & endpoint)
{
	debug_assert (node.network.endpoint () != endpoint && "Establishing TCP to self is not allowed");

	std::shared_ptr<nano::transport::channel_tcp> result;
	debug_assert (!node.flags.disable_tcp_realtime);
	node.network.tcp_channels.start_tcp (endpoint);
	auto error = system.poll_until_true (2s, [&result, &node, &endpoint] {
		result = node.network.tcp_channels.find_channel (nano::transport::map_endpoint_to_tcp (endpoint));
		return result != nullptr;
	});
	return result;
}

std::shared_ptr<nano::node> nano::test::add_outer_node (nano::test::system & system_a, nano::node_flags flags_a)
{
	auto outer_node = std::make_shared<nano::node> (system_a.io_ctx, system_a.get_available_port (), nano::unique_path (), system_a.work, flags_a);
	outer_node->start ();
	system_a.nodes.push_back (outer_node);
	return outer_node;
}

// Note: this is not guaranteed to work, it is speculative
uint16_t nano::test::speculatively_choose_a_free_tcp_bind_port ()
{
	/*
	 * This works because the kernel doesn't seem to reuse port numbers until it absolutely has to.
	 * Subsequent binds to port 0 will allocate a different port number.
	 */
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor{ io_ctx };
	boost::asio::ip::tcp::tcp::endpoint endpoint{ boost::asio::ip::tcp::v4 (), 0 };
	acceptor.open (endpoint.protocol ());

	boost::asio::socket_base::reuse_address option{ true };
	acceptor.set_option (option); // set SO_REUSEADDR option

	acceptor.bind (endpoint);

	auto actual_endpoint = acceptor.local_endpoint ();
	auto port = actual_endpoint.port ();

	acceptor.close ();

	return port;
}