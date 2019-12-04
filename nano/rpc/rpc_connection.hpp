#pragma once

#include <nano/lib/json_error_response.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/rpc/rpc_handler.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast.hpp>

#include <atomic>

/* Boost v1.70 introduced breaking changes; the conditional compilation allows 1.6x to be supported as well. */
#if BOOST_VERSION < 107000
using socket_type = boost::asio::ip::tcp::socket;
#else
using socket_type = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
#endif

namespace nano
{
class logger_mt;
class rpc_config;
class rpc_handler_interface;

class rpc_connection : public std::enable_shared_from_this<nano::rpc_connection>
{
public:
	rpc_connection (nano::rpc_config const & rpc_config, boost::asio::io_context & io_ctx, nano::logger_mt & logger, nano::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc_connection () = default;
	virtual void parse_connection ();
	virtual void write_completion_handler (std::shared_ptr<nano::rpc_connection> rpc_connection);
	void prepare_head (unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void write_result (std::string body, unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);

	socket_type socket;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> res;
	boost::asio::strand<boost::asio::io_context::executor_type> strand;
	std::atomic_flag responded;
	boost::asio::io_context & io_ctx;
	nano::logger_mt & logger;
	nano::rpc_config const & rpc_config;
	nano::rpc_handler_interface & rpc_handler_interface;

protected:
	template <typename STREAM_TYPE>
	void read (STREAM_TYPE & stream)
	{
		auto this_l (shared_from_this ());
		auto header_parser (std::make_shared<boost::beast::http::request_parser<boost::beast::http::empty_body>> ());
		header_parser->body_limit (rpc_config.max_request_size);

		boost::beast::http::async_read_header (stream, buffer, *header_parser, boost::asio::bind_executor (strand, [this_l, &stream, header_parser](boost::system::error_code const & ec, size_t bytes_transferred) {
			if (!ec)
			{
				if (boost::iequals (header_parser->get ()[boost::beast::http::field::expect], "100-continue"))
				{
					auto continue_response (std::make_shared<boost::beast::http::response<boost::beast::http::empty_body>> ());
					continue_response->version (11);
					continue_response->result (boost::beast::http::status::continue_);
					continue_response->set (boost::beast::http::field::server, "nano");
					boost::beast::http::async_write (stream, *continue_response, boost::asio::bind_executor (this_l->strand, [this_l, continue_response](boost::system::error_code const & ec, size_t bytes_transferred) {}));
				}

				this_l->parse_request (stream, header_parser);
			}
			else
			{
				this_l->logger.always_log ("RPC header error: ", ec.message ());

				// Respond with the reason for the invalid header
				auto response_handler ([this_l, &stream](std::string const & tree_a) {
					this_l->write_result (tree_a, 11);
					boost::beast::http::async_write (stream, this_l->res, boost::asio::bind_executor (this_l->strand, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
						this_l->write_completion_handler (this_l);
					}));
				});
				nano::json_error_response (response_handler, std::string ("Invalid header: ") + ec.message ());
			}
		}));
	}

	template <typename STREAM_TYPE>
	void parse_request (STREAM_TYPE & stream, std::shared_ptr<boost::beast::http::request_parser<boost::beast::http::empty_body>> header_parser)
	{
		auto this_l (shared_from_this ());
		auto body_parser (std::make_shared<boost::beast::http::request_parser<boost::beast::http::string_body>> (std::move (*header_parser)));
		boost::beast::http::async_read (stream, buffer, *body_parser, boost::asio::bind_executor (strand, [this_l, body_parser, &stream](boost::system::error_code const & ec, size_t bytes_transferred) {
			if (!ec)
			{
				this_l->io_ctx.post ([this_l, body_parser, &stream]() {
					auto & req (body_parser->get ());
					auto start (std::chrono::steady_clock::now ());
					auto version (req.version ());
					std::stringstream ss;
					ss << std::hex << std::showbase << reinterpret_cast<uintptr_t> (this_l.get ());
					auto request_id = ss.str ();
					auto response_handler ([this_l, version, start, request_id, &stream](std::string const & tree_a) {
						auto body = tree_a;
						this_l->write_result (body, version);
						boost::beast::http::async_write (stream, this_l->res, boost::asio::bind_executor (this_l->strand, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
							this_l->write_completion_handler (this_l);
						}));

						std::stringstream ss;
						ss << "RPC request " << request_id << " completed in: " << std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () << " microseconds";
						this_l->logger.always_log (ss.str ().c_str ());
					});
					auto method = req.method ();
					switch (method)
					{
						case boost::beast::http::verb::post:
						{
							auto handler (std::make_shared<nano::rpc_handler> (this_l->rpc_config, req.body (), request_id, response_handler, this_l->rpc_handler_interface, this_l->logger));
							handler->process_request ();
							break;
						}
						case boost::beast::http::verb::options:
						{
							this_l->prepare_head (version);
							this_l->res.prepare_payload ();
							boost::beast::http::async_write (stream, this_l->res, boost::asio::bind_executor (this_l->strand, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
								this_l->write_completion_handler (this_l);
							}));
							break;
						}
						default:
						{
							nano::json_error_response (response_handler, "Can only POST requests");
							break;
						}
					}
				});
			}
			else
			{
				this_l->logger.always_log ("RPC read error: ", ec.message ());
			}
		}));
	}
};
}
