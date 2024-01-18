#include <nano/node/ipc/ipc_server.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/rpc_test/test_response.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

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