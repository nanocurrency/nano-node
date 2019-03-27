#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <nano/lib/config.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_handler.hpp>

nano::rpc_connection::rpc_connection (nano::node & node_a, nano::rpc & rpc_a) :
node (node_a.shared ()),
rpc (rpc_a),
socket (node_a.io_ctx)
{
	responded.clear ();
}

void nano::rpc_connection::parse_connection ()
{
	read ();
}

void nano::rpc_connection::prepare_head (unsigned version, boost::beast::http::status status)
{
	res.version (version);
	res.result (status);
	res.set (boost::beast::http::field::allow, "POST, OPTIONS");
	res.set (boost::beast::http::field::content_type, "application/json");
	res.set (boost::beast::http::field::access_control_allow_origin, "*");
	res.set (boost::beast::http::field::access_control_allow_methods, "POST, OPTIONS");
	res.set (boost::beast::http::field::access_control_allow_headers, "Accept, Accept-Language, Content-Language, Content-Type");
	res.set (boost::beast::http::field::connection, "close");
}

void nano::rpc_connection::write_result (std::string body, unsigned version, boost::beast::http::status status)
{
	if (!responded.test_and_set ())
	{
		prepare_head (version, status);
		res.body () = body;
		res.prepare_payload ();
	}
	else
	{
		assert (false && "RPC already responded and should only respond once");
		// Guards `res' from being clobbered while async_write is being serviced
	}
}

void nano::rpc_connection::read ()
{
	auto this_l (shared_from_this ());
	boost::system::error_code header_error;
	auto header_parser (std::make_shared<boost::beast::http::request_parser<boost::beast::http::empty_body>> ());
	std::promise<size_t> header_available_promise;
	std::future<size_t> header_available = header_available_promise.get_future ();
	header_parser->body_limit (rpc.config.max_request_size);
	if (!node->network_params.network.is_test_network ())
	{
		boost::beast::http::async_read_header (socket, buffer, *header_parser, [this_l, header_parser, &header_available_promise, &header_error](boost::system::error_code const & ec, size_t bytes_transferred) {
			size_t header_response_bytes_written = 0;
			if (!ec)
			{
				if (boost::iequals (header_parser->get ()[boost::beast::http::field::expect], "100-continue"))
				{
					boost::beast::http::response<boost::beast::http::empty_body> continue_response;
					continue_response.version (11);
					continue_response.result (boost::beast::http::status::continue_);
					continue_response.set (boost::beast::http::field::server, "nano");
					auto response_size (boost::beast::http::async_write (this_l->socket, continue_response, boost::asio::use_future));
					header_response_bytes_written = response_size.get ();
				}
			}
			else
			{
				header_error = ec;
				this_l->node->logger.always_log ("RPC header error: ", ec.message ());
			}

			header_available_promise.set_value (header_response_bytes_written);
		});

		// Avait header
		header_available.get ();
	}

	if (!header_error)
	{
		auto body_parser (std::make_shared<boost::beast::http::request_parser<boost::beast::http::string_body>> (std::move (*header_parser)));
		boost::beast::http::async_read (socket, buffer, *body_parser, [this_l, body_parser](boost::system::error_code const & ec, size_t bytes_transferred) {
			if (!ec)
			{
				this_l->node->background ([this_l, body_parser]() {
					auto & req (body_parser->get ());
					auto start (std::chrono::steady_clock::now ());
					auto version (req.version ());
					std::string request_id (boost::str (boost::format ("%1%") % boost::io::group (std::hex, std::showbase, reinterpret_cast<uintptr_t> (this_l.get ()))));
					auto response_handler ([this_l, version, start, request_id](boost::property_tree::ptree const & tree_a) {
						std::stringstream ostream;
						boost::property_tree::write_json (ostream, tree_a);
						ostream.flush ();
						auto body (ostream.str ());
						this_l->write_result (body, version);
						boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
							this_l->write_completion_handler (this_l);
						});

						if (this_l->node->config.logging.log_rpc ())
						{
							this_l->node->logger.always_log (boost::str (boost::format ("RPC request %2% completed in: %1% microseconds") % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () % request_id));
						}
					});
					auto method = req.method ();
					switch (method)
					{
						case boost::beast::http::verb::post:
						{
							auto handler (std::make_shared<nano::rpc_handler> (*this_l->node, this_l->rpc, req.body (), request_id, response_handler));
							handler->process_request ();
							break;
						}
						case boost::beast::http::verb::options:
						{
							this_l->prepare_head (version);
							this_l->res.prepare_payload ();
							boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
								this_l->write_completion_handler (this_l);
							});
							break;
						}
						default:
						{
							error_response (response_handler, "Can only POST requests");
							break;
						}
					}
				});
			}
			else
			{
				this_l->node->logger.always_log ("RPC read error: ", ec.message ());
			}
		});
	}
	else
	{
		// Respond with the reason for the invalid header
		auto response_handler ([this_l](boost::property_tree::ptree const & tree_a) {
			std::stringstream ostream;
			boost::property_tree::write_json (ostream, tree_a);
			ostream.flush ();
			auto body (ostream.str ());
			this_l->write_result (body, 11);
			boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
				this_l->write_completion_handler (this_l);
			});
		});
		error_response (response_handler, std::string ("Invalid header: ") + header_error.message ());
	}
}

void nano::rpc_connection::write_completion_handler (std::shared_ptr<nano::rpc_connection> rpc_connection)
{
	// Intentional no-op
}