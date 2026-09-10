#include <nano/test_common/test_response.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <sstream>

std::shared_ptr<nano::test::test_response> nano::test::test_response::prepare (boost::property_tree::ptree const & request, boost::asio::io_context & io_ctx)
{
	return std::make_shared<test_response> (private_tag{}, request, io_ctx);
}

std::shared_ptr<nano::test::test_response> nano::test::test_response::send (boost::property_tree::ptree const & request, uint16_t port, boost::asio::io_context & io_ctx)
{
	auto result = prepare (request, io_ctx);
	result->run (port);
	return result;
}

nano::test::test_response::test_response (private_tag, boost::property_tree::ptree const & request, boost::asio::io_context & io_ctx) :
	request{ request },
	sock{ io_ctx }
{
}

void nano::test::test_response::run (uint16_t port)
{
	sock.async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), port), [this, /* lifetime guard */ this_s = shared_from_this ()] (boost::system::error_code const & ec) {
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
			boost::beast::http::async_write (sock, req, [this, /* lifetime guard */ this_s = shared_from_this ()] (boost::system::error_code const & ec, size_t bytes_transferred) {
				if (!ec)
				{
					boost::beast::http::async_read (sock, sb, resp, [this, /* lifetime guard */ this_s = shared_from_this ()] (boost::system::error_code const & ec, size_t bytes_transferred) {
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
