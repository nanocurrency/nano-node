#include <nano/lib/config.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_handler.hpp>

#include <boost/format.hpp>

nano::rpc_connection::rpc_connection (nano::rpc_config const & rpc_config, boost::asio::io_context & io_ctx, nano::logger_mt & logger, nano::rpc_handler_interface & rpc_handler_interface) :
socket (io_ctx),
strand (io_ctx.get_executor ()),
io_ctx (io_ctx),
logger (logger),
rpc_config (rpc_config),
rpc_handler_interface (rpc_handler_interface)
{
	responded.clear ();
}

void nano::rpc_connection::parse_connection ()
{
	read (socket);
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
	}
}

void nano::rpc_connection::write_completion_handler (std::shared_ptr<nano::rpc_connection> rpc_connection)
{
	// Intentional no-op
}
