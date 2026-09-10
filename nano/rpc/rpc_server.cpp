#include <nano/lib/network_formatting.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_server.hpp>

#include <stdexcept>

using namespace std::chrono_literals;

nano::rpc_server::rpc_server (std::shared_ptr<boost::asio::io_context> io_ctx_a, nano::rpc_config config_a, nano::rpc_handler_interface & handler_a) :
	config{ std::move (config_a) },
	handler{ handler_a },
	io_ctx{ std::move (io_ctx_a) },
	strand{ io_ctx->get_executor () },
	acceptor{ *io_ctx },
	task{ strand }
{
}

nano::rpc_server::~rpc_server ()
{
	// Owners stop the server themselves, this only covers a server that was never started
	stop ();
}

void nano::rpc_server::start ()
{
	debug_assert (!stopped);
	debug_assert (!task.joinable ());

	boost::asio::ip::tcp::endpoint const endpoint{ boost::asio::ip::make_address_v6 (config.address), config.port };

	bool const is_loopback = (endpoint.address ().is_loopback () || (endpoint.address ().to_v6 ().is_v4_mapped () && boost::asio::ip::make_address_v4 (boost::asio::ip::v4_mapped, endpoint.address ().to_v6 ()).is_loopback ()));
	if (!is_loopback && config.enable_control)
	{
		logger.warn (nano::log::type::rpc, "WARNING: Control-level RPCs are enabled on non-local address {}, potentially allowing wallet access outside local computer", endpoint.address ());
	}

	try
	{
		acceptor.open (endpoint.protocol ());
		acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
		acceptor.bind (endpoint);
		acceptor.listen ();
	}
	catch (boost::system::system_error const & ex)
	{
		logger.critical (nano::log::type::rpc, "Error while binding for RPC on port: {} ({})", endpoint.port (), ex.code ().message ());
		throw std::runtime_error (ex.code ().message ());
	}

	port = acceptor.local_endpoint ().port ();
	logger.info (nano::log::type::rpc, "RPC listening address: {}", acceptor.local_endpoint ());

	task = nano::async::task (strand, run ());
}

void nano::rpc_server::stop ()
{
	if (stopped.exchange (true))
	{
		return;
	}
	// Joining the accept task from a thread that runs this io_context could deadlock, owners stop from their main thread
	debug_assert (!io_ctx->get_executor ().running_in_this_thread ());

	if (task.joinable ())
	{
		task.cancel ();
		task.join ();
	}

	boost::system::error_code ec;
	acceptor.close (ec);
	if (ec)
	{
		logger.error (nano::log::type::rpc, "Error while closing RPC acceptor: {}", ec.message ());
	}

	logger.debug (nano::log::type::rpc, "Stopped");
}

std::uint16_t nano::rpc_server::listening_port () const
{
	return port;
}

asio::awaitable<void> nano::rpc_server::run ()
{
	debug_assert (strand.running_in_this_thread ());

	while (!stopped)
	{
		auto connection = std::make_shared<nano::rpc_connection> (config, *io_ctx, logger, handler);

		boost::system::error_code ec;
		co_await acceptor.async_accept (connection->socket, asio::redirect_error (asio::use_awaitable, ec));
		debug_assert (strand.running_in_this_thread ());

		if (stopped || ec == asio::error::operation_aborted)
		{
			break;
		}
		if (ec)
		{
			logger.error (nano::log::type::rpc, "Error accepting RPC connection: {}", ec.message ());
			co_await nano::async::sleep_for (10ms); // Prevent a busy loop on persistent errors
			continue;
		}

		connection->parse_connection ();
	}
}
