#include <boost/format.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_handler.hpp>

nano::rpc_connection::rpc_connection (nano::ipc::ipc_client & ipc_client, std::function<void()> stop_callback, boost::asio::io_context & io_ctx, nano::logger_mt & logger) :
socket (io_ctx),
io_ctx (io_ctx),
logger (logger),
ipc_client (ipc_client),
stop_callback (stop_callback)
{
	responded.clear ();
}

void nano::rpc_connection::parse_connection (unsigned max_json_depth, bool enable_control)
{
	read (max_json_depth, enable_control);
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

void nano::rpc_connection::read (unsigned max_json_depth, bool enable_control)
{
	auto this_l (shared_from_this ());
	boost::beast::http::async_read (socket, buffer, request, [this_l, max_json_depth, enable_control] (boost::system::error_code const & ec, size_t bytes_transferred) {
		if (!ec)
		{
			// equivalent to background
			this_l->io_ctx.post ([this_l, max_json_depth, enable_control] () {
				auto start (std::chrono::steady_clock::now ());
				auto version (this_l->request.version ());
				std::string request_id (boost::str (boost::format ("%1%") % boost::io::group (std::hex, std::showbase, reinterpret_cast<uintptr_t> (this_l.get ()))));
				auto response_handler ([this_l, version, start, request_id] (std::string const & tree_a) {
					auto body = tree_a; // Check if this is correct
					this_l->write_result (body, version);
					boost::beast::http::async_write (this_l->socket, this_l->res, [this_l] (boost::system::error_code const & ec, size_t bytes_transferred) {
					});

					this_l->logger.always_log (boost::str (boost::format ("RPC request %2% completed in: %1% microseconds") % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () % request_id));
				});
				auto method = this_l->request.method ();
				switch (method)
				{
					case boost::beast::http::verb::post:
					{
						auto handler (std::make_shared<nano::rpc_handler> (this_l->ipc_client, this_l->stop_callback, this_l->request.body (), request_id, response_handler));
						handler->process_request (max_json_depth, enable_control);
						break;
					}
					case boost::beast::http::verb::options:
					{
						this_l->prepare_head (version);
						this_l->res.prepare_payload ();
						boost::beast::http::async_write (this_l->socket, this_l->res, [this_l] (boost::system::error_code const & ec, size_t bytes_transferred) {
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
			this_l->logger.always_log ("RPC read error: ", ec.message ());
		}
	});
}
