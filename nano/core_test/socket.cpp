#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/boost/asio/ip/network_v6.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/node/inactive_node.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/read.hpp>

#include <future>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

TEST (socket, max_connections)
{
	nano::test::system system;

	nano::node_flags node_flags;
	nano::node_config node_config = system.default_config ();
	node_config.tcp.max_inbound_connections = 2;
	auto node = system.add_node (node_config, node_flags);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start 3 clients, 2 will persist but 1 will be dropped
	auto client1 = std::make_shared<nano::transport::tcp_socket> (*node);
	client1->async_connect (node->network.endpoint (), connect_handler);

	auto client2 = std::make_shared<nano::transport::tcp_socket> (*node);
	client2->async_connect (node->network.endpoint (), connect_handler);

	auto client3 = std::make_shared<nano::transport::tcp_socket> (*node);
	client3->async_connect (node->network.endpoint (), connect_handler);

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 2);
	ASSERT_ALWAYS_EQ (1s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 2);
	ASSERT_TIMELY_EQ (5s, connection_attempts, 3);

	// create space for one socket and fill the connections table again
	{
		auto sockets1 = node->tcp_listener.sockets ();
		ASSERT_EQ (sockets1.size (), 2);
		sockets1[0]->close ();
	}
	ASSERT_TIMELY_EQ (10s, node->tcp_listener.sockets ().size (), 1);

	auto client4 = std::make_shared<nano::transport::tcp_socket> (*node);
	client4->async_connect (node->network.endpoint (), connect_handler);

	auto client5 = std::make_shared<nano::transport::tcp_socket> (*node);
	client5->async_connect (node->network.endpoint (), connect_handler);

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 3);
	ASSERT_ALWAYS_EQ (1s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 3);
	ASSERT_TIMELY_EQ (5s, connection_attempts, 5);

	// close all existing sockets and fill the connections table again
	{
		auto sockets2 = node->tcp_listener.sockets ();
		ASSERT_EQ (sockets2.size (), 2);
		sockets2[0]->close ();
		sockets2[1]->close ();
	}
	ASSERT_TIMELY_EQ (10s, node->tcp_listener.sockets ().size (), 0);

	auto client6 = std::make_shared<nano::transport::tcp_socket> (*node);
	client6->async_connect (node->network.endpoint (), connect_handler);

	auto client7 = std::make_shared<nano::transport::tcp_socket> (*node);
	client7->async_connect (node->network.endpoint (), connect_handler);

	auto client8 = std::make_shared<nano::transport::tcp_socket> (*node);
	client8->async_connect (node->network.endpoint (), connect_handler);

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 5);
	ASSERT_ALWAYS_EQ (1s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 5);
	ASSERT_TIMELY_EQ (5s, connection_attempts, 8); // connections initiated by the client
}

TEST (socket, max_connections_per_ip)
{
	nano::test::system system;

	nano::node_flags node_flags;
	nano::node_config node_config = system.default_config ();
	node_config.network.max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);
	ASSERT_FALSE (node->flags.disable_max_peers_per_ip);

	auto server_port = system.get_available_port ();

	const auto max_ip_connections = node->config.network.max_peers_per_ip;
	ASSERT_GE (max_ip_connections, 1);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_ip_connections
	std::vector<std::shared_ptr<nano::transport::tcp_socket>> client_list;
	client_list.reserve (max_ip_connections + 1);

	for (auto idx = 0; idx < max_ip_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), connect_handler);
		client_list.push_back (client);
	}

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), max_ip_connections);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip), 1);
	ASSERT_TIMELY_EQ (5s, connection_attempts, max_ip_connections + 1);
}

TEST (socket, limited_subnet_address)
{
	auto address = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713");
	auto network = nano::transport::socket_functions::get_ipv6_subnet_address (address.to_v6 (), 32); // network prefix = 32.
	ASSERT_EQ ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713/32", network.to_string ());
	ASSERT_EQ ("a41d:b7b2::/32", network.canonical ().to_string ());
}

TEST (socket, first_ipv6_subnet_address)
{
	auto address = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713");
	auto first_address = nano::transport::socket_functions::first_ipv6_subnet_address (address.to_v6 (), 32); // network prefix = 32.
	ASSERT_EQ ("a41d:b7b2::", first_address.to_string ());
}

TEST (socket, last_ipv6_subnet_address)
{
	auto address = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713");
	auto last_address = nano::transport::socket_functions::last_ipv6_subnet_address (address.to_v6 (), 32); // network prefix = 32.
	ASSERT_EQ ("a41d:b7b2:ffff:ffff:ffff:ffff:ffff:ffff", last_address.to_string ());
}

TEST (socket, count_subnetwork_connections)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto address0 = boost::asio::ip::make_address ("a41d:b7b1:ffff:ffff:ffff:ffff:ffff:ffff"); // out of network prefix
	auto address1 = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713"); // referece address
	auto address2 = boost::asio::ip::make_address ("a41d:b7b2::"); // start of the network range
	auto address3 = boost::asio::ip::make_address ("a41d:b7b2::1");
	auto address4 = boost::asio::ip::make_address ("a41d:b7b2:ffff:ffff:ffff:ffff:ffff:ffff"); // end of the network range
	auto address5 = boost::asio::ip::make_address ("a41d:b7b3::"); // out of the network prefix
	auto address6 = boost::asio::ip::make_address ("a41d:b7b3::1"); // out of the network prefix

	auto connection0 = std::make_shared<nano::transport::tcp_socket> (*node);
	auto connection1 = std::make_shared<nano::transport::tcp_socket> (*node);
	auto connection2 = std::make_shared<nano::transport::tcp_socket> (*node);
	auto connection3 = std::make_shared<nano::transport::tcp_socket> (*node);
	auto connection4 = std::make_shared<nano::transport::tcp_socket> (*node);
	auto connection5 = std::make_shared<nano::transport::tcp_socket> (*node);
	auto connection6 = std::make_shared<nano::transport::tcp_socket> (*node);

	nano::transport::address_socket_mmap connections_per_address;
	connections_per_address.emplace (address0, connection0);
	connections_per_address.emplace (address1, connection1);
	connections_per_address.emplace (address2, connection2);
	connections_per_address.emplace (address3, connection3);
	connections_per_address.emplace (address4, connection4);
	connections_per_address.emplace (address5, connection5);
	connections_per_address.emplace (address6, connection6);

	// Asserts it counts only the connections for the specified address and its network prefix.
	ASSERT_EQ (4, nano::transport::socket_functions::count_subnetwork_connections (connections_per_address, address1.to_v6 (), 32));
}

TEST (socket, max_connections_per_subnetwork)
{
	nano::test::system system;

	nano::node_flags node_flags;
	// disabling IP limit because it will be used the same IP address to check they come from the same subnetwork.
	node_flags.disable_max_peers_per_ip = true;
	node_flags.disable_max_peers_per_subnetwork = false;
	nano::node_config node_config = system.default_config ();
	node_config.network.max_peers_per_subnetwork = 3;
	auto node = system.add_node (node_config, node_flags);

	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);
	ASSERT_FALSE (node->flags.disable_max_peers_per_subnetwork);

	const auto max_subnetwork_connections = node->config.network.max_peers_per_subnetwork;
	ASSERT_GE (max_subnetwork_connections, 1);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_subnetwork_connections
	std::vector<std::shared_ptr<nano::transport::tcp_socket>> client_list;
	client_list.reserve (max_subnetwork_connections + 1);

	for (auto idx = 0; idx < max_subnetwork_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), connect_handler);
		client_list.push_back (client);
	}

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), max_subnetwork_connections);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_subnetwork), 1);
	ASSERT_TIMELY_EQ (5s, connection_attempts, max_subnetwork_connections + 1);
}

TEST (socket, disabled_max_peers_per_ip)
{
	nano::test::system system;

	nano::node_flags node_flags;
	node_flags.disable_max_peers_per_ip = true;
	nano::node_config node_config = system.default_config ();
	node_config.network.max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);

	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);

	auto server_port = system.get_available_port ();

	const auto max_ip_connections = node->config.network.max_peers_per_ip;
	ASSERT_GE (max_ip_connections, 1);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_ip_connections
	std::vector<std::shared_ptr<nano::transport::tcp_socket>> client_list;
	client_list.reserve (max_ip_connections + 1);

	for (auto idx = 0; idx < max_ip_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), connect_handler);
		client_list.push_back (client);
	}

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), max_ip_connections + 1);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip), 0);
	ASSERT_TIMELY_EQ (5s, connection_attempts, max_ip_connections + 1);
}

TEST (socket, disconnection_of_silent_connections)
{
	nano::test::system system;

	nano::node_config config;
	// Increasing the timer timeout, so we don't let the connection to timeout due to the timer checker.
	config.tcp_io_timeout = std::chrono::seconds::max ();
	config.network_params.network.idle_timeout = std::chrono::seconds::max ();
	// Silent connections are connections open by external peers that don't contribute with any data.
	config.network_params.network.silent_connection_tolerance_time = std::chrono::seconds{ 5 };
	auto node = system.add_node (config);

	// On a connection, a server data socket is created. The shared pointer guarantees the object's lifecycle until the end of this test.
	std::promise<std::shared_ptr<nano::transport::tcp_socket>> server_data_socket_promise;
	std::future<std::shared_ptr<nano::transport::tcp_socket>> server_data_socket_future = server_data_socket_promise.get_future ();
	node->tcp_listener.connection_accepted.add ([&server_data_socket_promise] (auto const & socket, auto const & server) {
		server_data_socket_promise.set_value (socket);
	});

	boost::asio::ip::tcp::endpoint dst_endpoint{ boost::asio::ip::address_v6::loopback (), node->tcp_listener.endpoint ().port () };

	// Instantiates a client to simulate an incoming connection.
	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> connected{ false };
	// Opening a connection that will be closed because it remains silent during the tolerance time.
	client_socket->async_connect (dst_endpoint, [client_socket, &connected] (boost::system::error_code const & ec_a) {
		ASSERT_FALSE (ec_a);
		connected = true;
	});
	ASSERT_TIMELY (4s, connected);

	// Checking the connection was closed.
	ASSERT_TIMELY (10s, server_data_socket_future.wait_for (0s) == std::future_status::ready);
	auto server_data_socket = server_data_socket_future.get ();
	ASSERT_TIMELY (10s, server_data_socket->is_closed ());

	// Just to ensure the disconnection wasn't due to the timer timeout.
	ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in));
	// Asserts the silent checker worked.
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_silent_connection_drop, nano::stat::dir::in));
}

TEST (socket, drop_policy)
{
	nano::test::system system;

	auto node_flags = nano::inactive_node_flag_defaults ();
	node_flags.read_only = false;
	nano::inactive_node inactivenode (nano::unique_path (), node_flags);
	auto node = inactivenode.node;

	std::vector<std::shared_ptr<nano::transport::tcp_socket>> connections;

	auto func = [&] (size_t total_message_count, nano::transport::buffer_drop_policy drop_policy) {
		boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
		boost::asio::ip::tcp::acceptor acceptor (node->io_ctx);
		acceptor.open (endpoint.protocol ());
		acceptor.bind (endpoint);
		acceptor.listen (boost::asio::socket_base::max_listen_connections);

		boost::asio::ip::tcp::socket newsock (*system.io_ctx);
		acceptor.async_accept (newsock, [] (boost::system::error_code const & ec) {
			EXPECT_FALSE (ec);
		});

		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		auto channel = std::make_shared<nano::transport::tcp_channel> (node, client);

		std::atomic completed_writes{ 0 };

		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), acceptor.local_endpoint ().port ()),
		[&channel, total_message_count, node, &completed_writes, &drop_policy, client] (boost::system::error_code const & ec_a) mutable {
			for (int i = 0; i < total_message_count; i++)
			{
				std::vector<uint8_t> buff (1);
				channel->send_buffer (
				nano::shared_const_buffer (std::move (buff)), [&completed_writes, client] (boost::system::error_code const & ec, size_t size_a) mutable {
					client.reset ();
					++completed_writes;
				},
				drop_policy);
			}
		});

		ASSERT_TIMELY_EQ (5s, completed_writes, total_message_count);
		ASSERT_EQ (1, client.use_count ());
	};

	// We're going to write twice the queue size + 1, and the server isn't reading
	// The total number of drops should thus be 1 (the socket allows doubling the queue size for no_socket_drop)
	func (nano::transport::tcp_socket::default_max_queue_size * 2 + 1, nano::transport::buffer_drop_policy::no_socket_drop);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_no_socket_drop, nano::stat::dir::out));
	ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_drop, nano::stat::dir::out));

	func (nano::transport::tcp_socket::default_max_queue_size + 1, nano::transport::buffer_drop_policy::limiter);
	// The stats are accumulated from before
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_no_socket_drop, nano::stat::dir::out));
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_drop, nano::stat::dir::out));
}

// This is abusing the socket class, it's interfering with the normal node lifetimes and as a result deadlocks
// TEST (socket, DISABLED_concurrent_writes)
TEST (socket, concurrent_writes)
{
	nano::test::system system;

	auto node_flags = nano::inactive_node_flag_defaults ();
	node_flags.read_only = false;
	node_flags.disable_max_peers_per_ip = true;
	node_flags.disable_max_peers_per_subnetwork = true;
	nano::inactive_node inactivenode (nano::unique_path (), node_flags);
	auto node = inactivenode.node;

	constexpr size_t max_connections = 4;
	constexpr size_t client_count = max_connections;
	constexpr size_t message_count = 4;
	constexpr size_t total_message_count = client_count * message_count;

	// We're expecting client_count*4 messages
	std::atomic completed_reads{ 0 };

	using reader_callback_t = std::function<void (std::shared_ptr<nano::transport::tcp_socket> const &)>;
	reader_callback_t reader = [&completed_reads, &total_message_count, &reader] (std::shared_ptr<nano::transport::tcp_socket> const & socket_a) {
		auto buff (std::make_shared<std::vector<uint8_t>> ());
		buff->resize (1);
		socket_a->async_read (buff, 1, [&completed_reads, &reader, &total_message_count, socket_a, buff] (boost::system::error_code const & ec, size_t size_a) {
			if (!ec)
			{
				if (completed_reads++ < total_message_count)
				{
					reader (socket_a);
				}
			}
			else if (ec != boost::asio::error::eof)
			{
				std::cerr << "async_read: " << ec.message () << std::endl;
			}
		});
	};

	std::vector<std::shared_ptr<nano::transport::tcp_socket>> connections;

	auto server_port (system.get_available_port ());
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::any (), server_port);
	boost::asio::ip::tcp::acceptor acceptor (node->io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	using accept_callback_t = std::function<void (boost::system::error_code const &, boost::asio::ip::tcp::socket)>;
	accept_callback_t accept_callback = [&] (boost::system::error_code const & ec, boost::asio::ip::tcp::socket socket) {
		if (!ec)
		{
			auto new_connection = std::make_shared<nano::transport::tcp_socket> (*node, std::move (socket), socket.remote_endpoint (), socket.local_endpoint ());
			connections.push_back (new_connection);
			reader (new_connection);

			acceptor.async_accept (accept_callback);
		}
		else
		{
			std::cerr << "async_accept: " << ec.message () << std::endl;
		}
	};
	acceptor.async_accept (accept_callback);

	std::atomic completed_connections{ 0 };

	std::vector<std::shared_ptr<nano::transport::tcp_socket>> clients;
	for (unsigned i = 0; i < client_count; i++)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		clients.push_back (client);
		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), acceptor.local_endpoint ().port ()),
		[&completed_connections] (boost::system::error_code const & ec_a) {
			if (ec_a)
			{
				std::cerr << "async_connect: " << ec_a.message () << std::endl;
			}
			else
			{
				++completed_connections;
			}
		});
	}

	ASSERT_TIMELY_EQ (10s, completed_connections, client_count);

	// Execute overlapping writes from multiple threads
	auto client (clients[0]);
	std::vector<std::thread> client_threads;
	for (int i = 0; i < client_count; i++)
	{
		client_threads.emplace_back ([&client, &message_count] () {
			for (int i = 0; i < message_count; i++)
			{
				std::vector<uint8_t> buff;
				buff.push_back ('A' + i);
				client->async_write (nano::shared_const_buffer (std::move (buff)));
			}
		});
	}

	ASSERT_TIMELY_EQ (10s, completed_reads, total_message_count);

	for (auto & t : client_threads)
	{
		t.join ();
	}
}

/**
 * Check that the socket correctly handles a tcp_io_timeout during tcp connect
 * Steps:
 *   set timeout to one second
 *   do a tcp connect that will block for at least a few seconds at the tcp level
 *   check that the connect returns error and that the correct counters have been incremented
 *
 *   NOTE: it is possible that the O/S has tried to access the IP address 10.255.254.253 before
 *   and has it marked in the routing table as unroutable. In that case this test case will fail.
 *   If this test is run repeadetly the tests fails for this reason because the connection fails
 *   with "No route to host" error instead of a timeout.
 */
TEST (socket_timeout, connect)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (1);

	// try to connect to an IP address that most likely does not exist and will not reply
	// we want the tcp stack to not receive a negative reply, we want it to see silence and to keep trying
	// I use the un-routable IP address 10.255.254.253, which is likely to not exist
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::make_address_v6 ("::ffff:10.255.254.253"), 1234);

	// create a client socket and try to connect to the IP address that wil not respond
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (endpoint, [&ec, &done] (boost::system::error_code const & ec_a) {
		if (ec_a)
		{
			ec = ec_a;
			done = true;
		}
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY_EQ (6s, done, true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_connect_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	// NOTE: this assert is not guaranteed to be always true, it is only likely that it will be true, we can also get "No route to host"
	// if this test is run repeatedly or in parallel then it is guaranteed to fail due to "No route to host" instead of timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, read)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (*system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and create a newsock and do not send any data
	boost::asio::ip::tcp::socket newsock (*system.io_ctx);
	acceptor.async_accept (newsock, [] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);
	});

	// create a client socket to connect and call async_read, which should time out
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (acceptor.local_endpoint (), [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		auto buffer = std::make_shared<std::vector<uint8_t>> (1);
		socket->async_read (buffer, 1, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
			if (ec_a)
			{
				ec = ec_a;
				done = true;
			}
		});
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY_EQ (10s, done, true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, write)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (*system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and create a newsock and do not receive any data
	boost::asio::ip::tcp::socket newsock (*system.io_ctx);
	acceptor.async_accept (newsock, [] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);
	});

	// create a client socket and send lots of data to fill the socket queue on the local and remote side
	// eventually, the all tcp queues should fill up and async_write will not be able to progress
	// and the timeout should kick in and close the socket, which will cause the async_write to return an error
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node, nano::transport::socket_endpoint::client, 1024 * 64); // socket with a max queue size much larger than OS buffers
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (acceptor.local_endpoint (), [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		auto buffer = std::make_shared<std::vector<uint8_t>> (128 * 1024);
		for (auto i = 0; i < 1024; ++i)
		{
			socket->async_write (nano::shared_const_buffer{ buffer }, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
				if (ec_a)
				{
					ec = ec_a;
					done = true;
				}
			});
		}
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY_EQ (10s, done, true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, read_overlapped)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (*system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and send one byte only
	boost::asio::ip::tcp::socket newsock (*system.io_ctx);
	acceptor.async_accept (newsock, [&newsock] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		auto buffer = std::make_shared<std::vector<uint8_t>> (1);
		nano::async_write (newsock, nano::shared_const_buffer (buffer), [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (!ec_a);
			debug_assert (size_a == 1);
		});
	});

	// create a client socket to connect and call async_read twice, the second call should time out
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (acceptor.local_endpoint (), [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		auto buffer = std::make_shared<std::vector<uint8_t>> (1);

		socket->async_read (buffer, 1, [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 1);
		});

		socket->async_read (buffer, 1, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 0);
			if (ec_a)
			{
				ec = ec_a;
				done = true;
			}
		});
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY_EQ (10s, done, true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, write_overlapped)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (*system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and read 2 bytes only
	boost::asio::ip::tcp::socket newsock (*system.io_ctx);
	auto buffer = std::make_shared<std::vector<uint8_t>> (1);
	acceptor.async_accept (newsock, [&newsock, &buffer] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		boost::asio::async_read (newsock, boost::asio::buffer (buffer->data (), buffer->size ()), [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 1);
		});
	});

	// create a client socket and send lots of data to fill the socket queue on the local and remote side
	// eventually, the all tcp queues should fill up and async_write will not be able to progress
	// and the timeout should kick in and close the socket, which will cause the async_write to return an error
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node, nano::transport::socket_endpoint::client, 1024 * 64); // socket with a max queue size much larger than OS buffers
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (acceptor.local_endpoint (), [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		auto buffer1 = std::make_shared<std::vector<uint8_t>> (1);
		auto buffer2 = std::make_shared<std::vector<uint8_t>> (128 * 1024);
		socket->async_write (nano::shared_const_buffer{ buffer1 }, [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 1);
		});
		for (auto i = 0; i < 1024; ++i)
		{
			socket->async_write (nano::shared_const_buffer{ buffer2 }, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
				if (ec_a)
				{
					ec = ec_a;
					done = true;
				}
			});
		}
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY_EQ (10s, done, true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}
