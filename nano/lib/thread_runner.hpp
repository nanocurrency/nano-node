#pragma once
#include <nano/boost/asio/deadline_timer.hpp>
#include <nano/boost/asio/executor_work_guard.hpp>
#include <nano/boost/asio/io_context.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>

#include <boost/thread.hpp>

namespace nano
{
namespace asio = boost::asio;

class thread_runner final
{
	nano::logger logger;

public:
	thread_runner (std::shared_ptr<asio::io_context>, unsigned num_threads = nano::hardware_concurrency (), nano::thread_role::name thread_role = nano::thread_role::name::io);
	~thread_runner ();

	/** Wait for IO threads to complete */
	void join ();

	/** Tells the IO context to stop processing events.
	 *  NOTE: This shouldn't really be used, node should stop gracefully by cancelling any outstanding async operations and calling join() */
	void abort ();

private:
	void start ();

	unsigned const num_threads;
	nano::thread_role::name const role;
	std::shared_ptr<asio::io_context> io_ctx;
	asio::executor_work_guard<asio::io_context::executor_type> io_guard;
	std::vector<boost::thread> threads;

private:
	void run ();
};

constexpr unsigned asio_handler_tracking_threshold ()
{
#if NANO_ASIO_HANDLER_TRACKING == 0
	return 0;
#else
	return NANO_ASIO_HANDLER_TRACKING;
#endif
}
} // namespace nano
