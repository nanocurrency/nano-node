#include <nano/test_common/ssl.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <unordered_map>

using namespace std::chrono_literals;

namespace
{
void perform_expectations (const std::shared_ptr<nano::test::ssl::server> & server, const std::shared_ptr<nano::test::ssl::client> & client)
{
	std::this_thread::sleep_for (1s);

	const auto & server_sockets = server->get_client_sockets ();
	ASSERT_GE (server_sockets.size (), 1); // TODO: fix me, remove dead peers

	const auto & server_socket = server_sockets.back ();
	EXPECT_TRUE (server_socket->is_connected ());
	EXPECT_EQ (server_socket->get_errors (), std::string{});

	const auto & client_socket = client->get_socket ();
	EXPECT_TRUE (client_socket.is_connected ());
	EXPECT_EQ (client_socket.get_errors (), std::string{});
}

using concurrent_connection_entities_map = std::unordered_map<std::shared_ptr<nano::test::ssl::server>, std::vector<std::shared_ptr<nano::test::ssl::client>>>;
void perform_expectations_concurrently (const concurrent_connection_entities_map & concurrent_connection_entities)
{
	std::this_thread::sleep_for (1s);
	for (const auto & [server, clients] : concurrent_connection_entities)
	{
		const auto & server_sockets = server->get_client_sockets ();
		ASSERT_GE (server_sockets.size (), clients.size ()); // TODO: fix me, remove dead peers

		for (const auto & server_socket : server_sockets)
		{
			// EXPECT_TRUE (server_socket->is_connected ()); // TODO: fix me, remove dead peers
			EXPECT_EQ (server_socket->get_errors (), std::string{});
		}

		for (const auto & client : clients)
		{
			const auto & client_socket = client->get_socket ();
			EXPECT_TRUE (client_socket.is_connected ());
			EXPECT_EQ (client_socket.get_errors (), std::string{});

			client->close ();
		}

		server->close ();
	}
}

void run_one_to_one (const std::shared_ptr<nano::test::ssl::server> & server, const std::shared_ptr<nano::test::ssl::client> & client)
{
	const auto port = nano::test::get_available_port ();
	server->run (port);
	client->run (port);

	perform_expectations (server, client);

	client->close ();
	server->close ();
}

template <typename ServerT, typename ClientT>
void build_and_run_one_to_one (boost::asio::io_context & io_context)
{
	nano::ssl::generatePki (nano::ssl::key_group{ CA_PRIVATE_KEY_HEX_1, CA_PUBLIC_KEY_HEX_1 }, std::filesystem::path{ "test_pki" });
	const auto server = std::make_shared<ServerT> (io_context);
	const auto client = std::make_shared<ClientT> (io_context);
	run_one_to_one (server, client);
}

}

TEST (ssl, one_to_one_secure)
{
	nano::test::ssl::io_context io_context{};
	build_and_run_one_to_one<nano::test::ssl::ssl_server, nano::test::ssl::ssl_client> (*io_context);
}

TEST (ssl, one_to_one_plain)
{
	nano::test::ssl::io_context io_context{};
	build_and_run_one_to_one<nano::test::ssl::plain_server, nano::test::ssl::plain_client> (*io_context);
}

TEST (ssl, one_to_one_secure_server_plain_client)
{
	nano::test::ssl::io_context io_context{};
	build_and_run_one_to_one<nano::test::ssl::ssl_server, nano::test::ssl::plain_client> (*io_context);
}

TEST (ssl, one_to_one_plain_server_secure_client)
{
	nano::test::ssl::io_context io_context{};
	build_and_run_one_to_one<nano::test::ssl::plain_server, nano::test::ssl::ssl_client> (*io_context);
}

TEST (ssl, many_to_many_mixed)
{
	nano::test::ssl::io_context io_context{};

	const auto [servers, clients] = nano::test::ssl::build_mixed_connection_entities (*io_context);
	for (const auto & server : servers)
	{
		for (const auto & client : clients)
		{
			run_one_to_one (server, client);
		}
	}
}

TEST (ssl, many_to_many_mixed_concurrently)
{
	nano::test::ssl::io_context io_context{};
	auto servers = nano::test::ssl::build_mixed_servers (*io_context);

	std::vector<std::uint16_t> ports (servers.size ());
	std::generate (ports.begin (), ports.end (), nano::test::get_available_port);
	auto portItr = ports.begin ();

	for (const auto & server : servers)
	{
		server->run (*portItr++);
	}

	portItr = ports.begin ();
	concurrent_connection_entities_map concurrent_connection_entities{};
	for (auto & server : servers)
	{
		auto clients = nano::test::ssl::build_mixed_clients (*io_context);
		for (const auto & client : clients)
		{
			client->run (*portItr);
		}

		concurrent_connection_entities[std::move (server)] = std::move (clients);
		++portItr;
	}

	perform_expectations_concurrently (concurrent_connection_entities);
}

TEST (ssl, one_to_one_secure_with_bad_certificate)
{
	// TODO: implement
}
