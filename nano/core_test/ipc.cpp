#include <nano/lib/files.hpp>
#include <nano/lib/ipc_client.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/ipc/ipc_access_config.hpp>
#include <nano/node/ipc/ipc_config.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/rpc/rpc_server.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <memory>
#include <sstream>
#include <vector>

using namespace std::chrono_literals;

TEST (ipc, asynchronous)
{
	nano::test::system system (1);
	system.nodes[0]->config.ipc_config->transport_tcp.enabled = true;
	system.nodes[0]->config.ipc_config->transport_tcp.port = system.get_available_port ();
	nano::node_rpc_config node_rpc_config;
	nano::ipc::ipc_server ipc (*system.nodes[0], node_rpc_config);
	nano::ipc::ipc_client client (system.nodes[0]->io_ctx_shared);

	auto req (nano::ipc::prepare_request (nano::ipc::payload_encoding::json_v1, std::string (R"({"action": "block_count"})")));
	auto res (std::make_shared<std::vector<uint8_t>> ());
	std::atomic<bool> call_completed{ false };
	client.async_connect ("::1", ipc.listening_tcp_port ().value (), [&client, &req, &res, &call_completed] (nano::error err) {
		client.async_write (req, [&client, &req, &res, &call_completed] (nano::error err_a, size_t size_a) {
			ASSERT_NO_ERROR (static_cast<std::error_code> (err_a));
			ASSERT_EQ (size_a, req.size ());
			// Read length
			client.async_read (res, sizeof (uint32_t), [&client, &res, &call_completed] (nano::error err_read_a, size_t size_read_a) {
				ASSERT_NO_ERROR (static_cast<std::error_code> (err_read_a));
				ASSERT_EQ (size_read_a, sizeof (uint32_t));
				uint32_t payload_size_l = boost::endian::big_to_native (*reinterpret_cast<uint32_t *> (res->data ()));
				// Read json payload
				client.async_read (res, payload_size_l, [&res, &call_completed] (nano::error err_read_a, size_t size_read_a) {
					std::string payload (res->begin (), res->end ());
					std::stringstream ss;
					ss << payload;

					// Make sure the response is valid json
					boost::property_tree::ptree blocks;
					boost::property_tree::read_json (ss, blocks);
					ASSERT_EQ (blocks.get<int> ("count"), 1);
					call_completed = true;
				});
			});
		});
	});
	ASSERT_TIMELY (5s, call_completed);
	ipc.stop ();
}

TEST (ipc, synchronous)
{
	nano::test::system system (1);
	system.nodes[0]->config.ipc_config->transport_tcp.enabled = true;
	system.nodes[0]->config.ipc_config->transport_tcp.port = system.get_available_port ();
	nano::node_rpc_config node_rpc_config;
	nano::ipc::ipc_server ipc (*system.nodes[0], node_rpc_config);
	nano::ipc::ipc_client client (system.nodes[0]->io_ctx_shared);

	// Start blocking IPC client in a separate thread
	std::atomic<bool> call_completed{ false };
	std::thread client_thread ([&] () {
		client.connect ("::1", ipc.listening_tcp_port ().value ());
		std::string response (nano::ipc::request (nano::ipc::payload_encoding::json_v1, client, std::string (R"({"action": "block_count"})")));
		std::stringstream ss;
		ss << response;
		// Make sure the response is valid json
		boost::property_tree::ptree blocks;
		boost::property_tree::read_json (ss, blocks);
		ASSERT_EQ (blocks.get<int> ("count"), 1);

		call_completed = true;
	});
	client_thread.detach ();

	ASSERT_TIMELY (5s, call_completed);
	ipc.stop ();
}

TEST (ipc, permissions_default_user)
{
	// Test empty/nonexistent access config. The default user still exists with default permissions.
	std::stringstream ss;
	ss << R"toml(
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);

	nano::ipc::access access;
	access.deserialize_toml (toml);
	ASSERT_TRUE (access.has_access ("", nano::ipc::access_permission::api_account_weight));
}

TEST (ipc, permissions_deny_default)
{
	// All users have api_account_weight permissions by default. This removes the permission for a specific user.
	std::stringstream ss;
	ss << R"toml(
	[[user]]
	id = "user1"
	deny = "api_account_weight"
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);

	nano::ipc::access access;
	access.deserialize_toml (toml);
	ASSERT_FALSE (access.has_access ("user1", nano::ipc::access_permission::api_account_weight));
}

TEST (ipc, permissions_groups)
{
	// Make sure role permissions are adopted by user
	std::stringstream ss;
	ss << R"toml(
	[[role]]
	id = "mywalletadmin"
	allow = "wallet_read, wallet_write"

	[[user]]
	id = "user1"
	roles = "mywalletadmin"
	deny = "api_account_weight"
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);

	nano::ipc::access access;
	access.deserialize_toml (toml);
	ASSERT_FALSE (access.has_access ("user1", nano::ipc::access_permission::api_account_weight));
	ASSERT_TRUE (access.has_access_to_all ("user1", { nano::ipc::access_permission::wallet_read, nano::ipc::access_permission::wallet_write }));
}

TEST (ipc, permissions_oneof)
{
	// Test one of two permissions
	std::stringstream ss;
	ss << R"toml(
	[[user]]
	id = "user1"
	allow = "api_account_weight"
	[[user]]
	id = "user2"
	allow = "api_account_weight, account_query"
	[[user]]
	id = "user3"
	deny = "api_account_weight, account_query"
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);

	nano::ipc::access access;
	access.deserialize_toml (toml);
	ASSERT_TRUE (access.has_access ("user1", nano::ipc::access_permission::api_account_weight));
	ASSERT_TRUE (access.has_access ("user2", nano::ipc::access_permission::api_account_weight));
	ASSERT_FALSE (access.has_access ("user3", nano::ipc::access_permission::api_account_weight));
	ASSERT_TRUE (access.has_access_to_oneof ("user1", { nano::ipc::access_permission::account_query, nano::ipc::access_permission::api_account_weight }));
	ASSERT_TRUE (access.has_access_to_oneof ("user2", { nano::ipc::access_permission::account_query, nano::ipc::access_permission::api_account_weight }));
	ASSERT_FALSE (access.has_access_to_oneof ("user3", { nano::ipc::access_permission::account_query, nano::ipc::access_permission::api_account_weight }));
}

TEST (ipc, permissions_default_user_order)
{
	// If changing the default user, it must come first
	std::stringstream ss;
	ss << R"toml(
	[[user]]
	id = "user1"
	[[user]]
	id = ""
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);

	nano::ipc::access access;
	ASSERT_TRUE (access.deserialize_toml (toml));
}

/** Semantic errors in the access config carry the file name too */
TEST (ipc, access_config_load_invalid)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	{
		std::ofstream file{ path / nano::access_config_filename };
		file << "[[user]]\nid = \"alice\"\nroles = \"auditor\"\n";
	}

	nano::ipc::access access;
	try
	{
		nano::ipc::load_access_config (path, access);
		FAIL () << "expected a config error";
	}
	catch (nano::config_error const & ex)
	{
		ASSERT_STREQ (ex.what (), "config-access.toml: Unknown role: auditor");
	}
}

/** Reloading a broken access config at runtime reports the problem instead of throwing into the server */
TEST (ipc, access_config_invalid)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::node_rpc_config node_rpc_config;
	nano::ipc::ipc_server ipc (node, node_rpc_config);
	{
		std::ofstream file{ node.get_data_path () / nano::access_config_filename };
		file << "[[user]]\nid = \"alice\"\nroles = \"auditor\"\n";
	}

	auto error = ipc.reload_access_config ();
	ASSERT_TRUE (error);
	ASSERT_EQ (error.get_message (), "config-access.toml: Unknown role: auditor");
}

TEST (ipc, invalid_endpoint)
{
	nano::test::system system (1);
	system.nodes[0]->config.ipc_config->transport_tcp.enabled = true;
	system.nodes[0]->config.ipc_config->transport_tcp.port = system.get_available_port ();
	nano::node_rpc_config node_rpc_config;
	nano::ipc::ipc_client client (system.nodes[0]->io_ctx_shared);

	std::atomic<bool> call_completed{ false };
	client.async_connect ("::-1", 24077, [&client, &call_completed] (nano::error err) {
		ASSERT_IS_ERROR (static_cast<std::error_code> (err));
		call_completed = true;
	});
	ASSERT_TIMELY (5s, call_completed);
}