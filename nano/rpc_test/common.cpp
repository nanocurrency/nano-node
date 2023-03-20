#include <nano/lib/threading.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/rpc_test/common.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

namespace nano
{
class rpc;

nano::test::test_response::test_response (boost::property_tree::ptree const & request_a, boost::asio::io_context & io_ctx_a) :
	request (request_a),
	sock (io_ctx_a)
{
}

nano::test::test_response::test_response (boost::property_tree::ptree const & request_a, uint16_t port_a, boost::asio::io_context & io_ctx_a) :
	request (request_a),
	sock (io_ctx_a)
{
	run (port_a);
}

void nano::test::test_response::run (uint16_t port_a)
{
	sock.async_connect (nano::tcp_endpoint (boost::asio::ip::address_v6::loopback (), port_a), [this] (boost::system::error_code const & ec) {
		if (!ec)
		{
			std::stringstream ostream;
			boost::property_tree::write_json (ostream, request);
			req.method (boost::beast::http::verb::post);
			req.target ("/");
			req.version (11);
			ostream.flush ();
			req.body () = ostream.str ();
			req.prepare_payload ();
			boost::beast::http::async_write (sock, req, [this] (boost::system::error_code const & ec, size_t bytes_transferred) {
				if (!ec)
				{
					boost::beast::http::async_read (sock, sb, resp, [this] (boost::system::error_code const & ec, size_t bytes_transferred) {
						if (!ec)
						{
							std::stringstream body (resp.body ());
							try
							{
								boost::property_tree::read_json (body, json);
								status = 200;
							}
							catch (std::exception &)
							{
								status = 500;
							}
						}
						else
						{
							status = 400;
						}
					});
				}
				else
				{
					status = 600;
				}
			});
		}
		else
		{
			status = 400;
		}
	});
}

nano::test::rpc_context::rpc_context (std::shared_ptr<nano::rpc> & rpc_a, std::unique_ptr<nano::ipc::ipc_server> & ipc_server_a, std::unique_ptr<nano::ipc_rpc_processor> & ipc_rpc_processor_a, std::unique_ptr<nano::node_rpc_config> & node_rpc_config_a)
{
	rpc = std::move (rpc_a);
	ipc_server = std::move (ipc_server_a);
	ipc_rpc_processor = std::move (ipc_rpc_processor_a);
	node_rpc_config = std::move (node_rpc_config_a);
}

std::shared_ptr<nano::node> nano::test::add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config, nano::node_flags const & node_flags)
{
	node_config.ipc_config.transport_tcp.enabled = true;
	node_config.ipc_config.transport_tcp.port = nano::test::get_available_port ();
	return system.add_node (node_config, node_flags);
}

std::shared_ptr<nano::node> nano::test::add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config)
{
	return add_ipc_enabled_node (system, node_config, nano::node_flags ());
}

std::shared_ptr<nano::node> nano::test::add_ipc_enabled_node (nano::test::system & system)
{
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	return add_ipc_enabled_node (system, node_config);
}

void nano::test::reset_confirmation_height (nano::store & store, nano::account const & account)
{
	auto transaction = store.tx_begin_write ();
	nano::confirmation_height_info confirmation_height_info;
	if (!store.confirmation_height.get (transaction, account, confirmation_height_info))
	{
		store.confirmation_height.clear (transaction, account);
	}
}

void nano::test::wait_response_impl (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time, boost::property_tree::ptree & response_json)
{
	test_response response (request, rpc_ctx.rpc->listening_port (), system.io_ctx);
	ASSERT_TIMELY (time, response.status != 0);
	ASSERT_EQ (200, response.status);
	response_json = response.json;
}

boost::property_tree::ptree nano::test::wait_response (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time)
{
	boost::property_tree::ptree response_json;
	wait_response_impl (system, rpc_ctx, request, time, response_json);
	return response_json;
}

bool nano::test::check_block_response_count (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count)
{
	auto response (wait_response (system, rpc_ctx, request));
	auto & blocks = response.get_child ("blocks");
	return size_count == blocks.size ();
}

nano::test::rpc_context nano::test::add_rpc (nano::test::system & system, std::shared_ptr<nano::node> const & node_a)
{
	auto node_rpc_config (std::make_unique<nano::node_rpc_config> ());
	auto ipc_server (std::make_unique<nano::ipc::ipc_server> (*node_a, *node_rpc_config));
	nano::rpc_config rpc_config (node_a->network_params.network, nano::test::get_available_port (), true);
	const auto ipc_tcp_port = ipc_server->listening_tcp_port ();
	debug_assert (ipc_tcp_port.has_value ());
	auto ipc_rpc_processor (std::make_unique<nano::ipc_rpc_processor> (system.io_ctx, rpc_config, ipc_tcp_port.value ()));
	auto rpc (std::make_shared<nano::rpc> (system.io_ctx, rpc_config, *ipc_rpc_processor));
	rpc->start ();

	return rpc_context{ rpc, ipc_server, ipc_rpc_processor, node_rpc_config };
}
}
