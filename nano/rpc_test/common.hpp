#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/property_tree/ptree.hpp>

#include <memory>

using namespace std::chrono_literals;

namespace nano
{
class ipc_rpc_processor;
class node;
class node_config;
class node_flags;
class node_rpc_config;
class public_key;
class rpc;
namespace store
{
	class component;
}

using account = public_key;
namespace ipc
{
	class ipc_server;
}
namespace test
{
	class system;
	class test_response
	{
	public:
		test_response (boost::property_tree::ptree const & request_a, boost::asio::io_context & io_ctx_a);
		test_response (boost::property_tree::ptree const & request_a, uint16_t port_a, boost::asio::io_context & io_ctx_a);
		void run (uint16_t port_a);
		boost::property_tree::ptree const & request;
		boost::asio::ip::tcp::socket sock;
		boost::property_tree::ptree json;
		boost::beast::flat_buffer sb;
		boost::beast::http::request<boost::beast::http::string_body> req;
		boost::beast::http::response<boost::beast::http::string_body> resp;
		std::atomic<int> status{ 0 };
	};
	class rpc_context
	{
	public:
		rpc_context (std::shared_ptr<nano::rpc> & rpc_a, std::unique_ptr<nano::ipc::ipc_server> & ipc_server_a, std::unique_ptr<nano::ipc_rpc_processor> & ipc_rpc_processor_a, std::unique_ptr<nano::node_rpc_config> & node_rpc_config_a);

		std::shared_ptr<nano::rpc> rpc;
		std::unique_ptr<nano::ipc::ipc_server> ipc_server;
		std::unique_ptr<nano::ipc_rpc_processor> ipc_rpc_processor;
		std::unique_ptr<nano::node_rpc_config> node_rpc_config;
	};

	std::shared_ptr<nano::node> add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config, nano::node_flags const & node_flags);
	std::shared_ptr<nano::node> add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config);
	std::shared_ptr<nano::node> add_ipc_enabled_node (nano::test::system & system);
	void reset_confirmation_height (nano::store::component & store, nano::account const & account);
	void wait_response_impl (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time, boost::property_tree::ptree & response_json);
	boost::property_tree::ptree wait_response (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time = 5s);
	bool check_block_response_count (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count);
	rpc_context add_rpc (nano::test::system & system, std::shared_ptr<nano::node> const & node_a);
}
}
