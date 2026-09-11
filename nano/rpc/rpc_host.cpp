#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/rpc/rpc_host.hpp>
#include <nano/rpc/rpc_server.hpp>

nano::rpc_host::rpc_host (nano::rpc_config config_a) :
	config{ std::move (config_a) },
	io_ctx{ std::make_shared<boost::asio::io_context> () }
{
}

nano::rpc_host::~rpc_host ()
{
	stop ();
}

std::shared_ptr<boost::asio::io_context> const & nano::rpc_host::io_context () const
{
	return io_ctx;
}

void nano::rpc_host::start (std::unique_ptr<nano::rpc_handler_interface> backend_a)
{
	debug_assert (!stopped);
	debug_assert (!server);
	release_assert (backend_a);

	backend = std::move (backend_a);
	server = std::make_unique<nano::rpc_server> (io_ctx, config, *backend);
	runner = std::make_unique<nano::thread_runner> (io_ctx, logger, config.rpc_process.io_threads, nano::thread_role::name::io_rpc);
	server->start ();
}

void nano::rpc_host::stop ()
{
	if (stopped.exchange (true))
	{
		return;
	}
	if (server)
	{
		server->stop (); // Every response that was started is written before this returns
	}
	if (runner)
	{
		runner->abort (); // Whatever the backend still has queued is dropped
		runner->join ();
	}
	backend.reset ();
}

std::uint16_t nano::rpc_host::listening_port () const
{
	release_assert (server);
	return server->listening_port ();
}
