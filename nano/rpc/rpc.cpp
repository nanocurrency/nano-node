#include <boost/format.hpp>
#include <nano/rpc/rpc.hpp>

nano::rpc::rpc (boost::asio::io_context & io_ctx_a, nano::rpc_config const & config_a, nano::ipc::ipc_client & ipc_client) :
config (config_a),
ipc_client (ipc_client),
acceptor (io_ctx_a),
logger (std::chrono::milliseconds (0)),
io_ctx (io_ctx_a)
{
}

void nano::rpc::start ()
{
	auto endpoint (boost::asio::ip::tcp::endpoint (config.address, config.port));
	acceptor.open (endpoint.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

	boost::system::error_code ec;
	acceptor.bind (endpoint, ec);
	if (ec)
	{
		logger.always_log (boost::str (boost::format ("Error while binding for RPC on port %1%: %2%") % endpoint.port () % ec.message ()));
		throw std::runtime_error (ec.message ());
	}
	acceptor.listen ();
	accept ();
}

void nano::rpc::accept ()
{
	auto stop_callback = [this]() {
		this->stop ();
	};

	auto connection (std::make_shared<nano::rpc_connection> (ipc_client, config, network_constants, stop_callback, io_ctx, logger));
	acceptor.async_accept (connection->socket, [this, connection](boost::system::error_code const & ec) {
		if (ec != boost::asio::error::operation_aborted && acceptor.is_open ())
		{
			accept ();
		}
		if (!ec)
		{
			connection->parse_connection ();
		}
		else
		{
			logger.always_log (boost::str (boost::format ("Error accepting RPC connections: %1% (%2%)") % ec.message () % ec.value ()));
		}
	});
}

void nano::rpc::stop ()
{
	acceptor.close ();
}
