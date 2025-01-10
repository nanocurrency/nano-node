#include <celerix/node/node.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

std::shared_ptr<celerix::transport::tcp_channel> celerix::test::establish_tcp (celerix::test::system & system, celerix::node & node, celerix::endpoint const & endpoint)
{
	debug_assert (node.network.endpoint () != endpoint && "Establishing TCP to self is not allowed");

	std::shared_ptr<celerix::transport::tcp_channel> result;
	debug_assert (!node.flags.disable_tcp_realtime);
	node.network.tcp_channels.start_tcp (endpoint);
	auto error = system.poll_until_true (2s, [&result, &node, &endpoint] {
		result = node.network.tcp_channels.find_channel (celerix::transport::map_endpoint_to_tcp (endpoint));
		return result != nullptr;
	});
	return result;
}

// TODO: merge with make_disconnected_node
std::shared_ptr<celerix::node> celerix::test::add_outer_node (celerix::test::system & system_a, celerix::node_config const & config_a, celerix::node_flags flags_a)
{
	auto outer_node = std::make_shared<celerix::node> (system_a.io_ctx, celerix::unique_path (), config_a, system_a.work, flags_a);
	outer_node->start ();
	system_a.disconnected_nodes.push_back (outer_node);
	return outer_node;
}

// TODO: merge with make_disconnected_node
std::shared_ptr<celerix::node> celerix::test::add_outer_node (celerix::test::system & system_a, celerix::node_flags flags_a)
{
	auto outer_node = std::make_shared<celerix::node> (system_a.io_ctx, system_a.get_available_port (), celerix::unique_path (), system_a.work, flags_a);
	outer_node->start ();
	system_a.disconnected_nodes.push_back (outer_node);
	return outer_node;
}

// Note: this is not guaranteed to work, it is speculative
uint16_t celerix::test::speculatively_choose_a_free_tcp_bind_port ()
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
