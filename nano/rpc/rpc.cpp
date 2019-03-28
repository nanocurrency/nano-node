#include <boost/format.hpp>
#include <nano/rpc/rpc.hpp>

#ifdef NANO_SECURE_RPC
#include <nano/rpc/rpc_secure.hpp>
#endif

nano::rpc::rpc (boost::asio::io_context & io_ctx_a, nano::rpc_config const & config_a) :
config (config_a),
acceptor (io_ctx_a),
logger (std::chrono::milliseconds (0)),
io_ctx (io_ctx_a),
rpc_request_processor (io_ctx, config, [this]() {
	this->stop ();
})
{
}

nano::rpc::~rpc ()
{
	if (!stopped)
	{
		stop ();
	}
}

void nano::rpc::start ()
{
	auto endpoint (boost::asio::ip::tcp::endpoint (config.address, config.port));
	acceptor.open (endpoint.protocol ());

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
	auto connection (std::make_shared<nano::rpc_connection> (config, network_constants, io_ctx, logger, rpc_request_processor));
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
	stopped = true;
	acceptor.close ();
	rpc_request_processor.stop ();
}

std::unique_ptr<nano::rpc> nano::get_rpc (boost::asio::io_context & io_ctx_a, nano::rpc_config const & config_a)
{
	std::unique_ptr<rpc> impl;

	if (config_a.secure.enable)
	{
#ifdef NANO_SECURE_RPC
		impl = std::make_unique<rpc_secure> (io_ctx_a, config_a);
#else
		std::cerr << "RPC configured for TLS, but the node is not compiled with TLS support" << std::endl;
#endif
	}
	else
	{
		impl = std::make_unique<rpc> (io_ctx_a, config_a);
	}

	return impl;
}