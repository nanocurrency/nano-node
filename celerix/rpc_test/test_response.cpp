#include <celerix/node/ipc/ipc_server.hpp>
#include <celerix/rpc/rpc_request_processor.hpp>
#include <celerix/rpc_test/test_response.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

celerix::test::test_response::test_response (boost::property_tree::ptree const & request_a, boost::asio::io_context & io_ctx_a) :
	request (request_a),
	sock (io_ctx_a)
{
}

celerix::test::test_response::test_response (boost::property_tree::ptree const & request_a, uint16_t port_a, boost::asio::io_context & io_ctx_a) :
	request (request_a),
	sock (io_ctx_a)
{
	run (port_a);
}

void celerix::test::test_response::run (uint16_t port_a)
{
	sock.async_connect (celerix::tcp_endpoint (boost::asio::ip::address_v6::loopback (), port_a), [this] (boost::system::error_code const & ec) {
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