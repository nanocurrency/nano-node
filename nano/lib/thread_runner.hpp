#pragma once
#include <celerix/boost/asio/deadline_timer.hpp>
#include <celerix/boost/asio/executor_work_guard.hpp>
#include <celerix/boost/asio/io_context.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/threading.hpp>

#include <boost/thread.hpp>

namespace celerix
{
namespace asio = boost::asio;

class thread_runner final
{
public:
	thread_runner (std::shared_ptr<asio::io_context>, celerix::logger &, unsigned num_threads = celerix::hardware_concurrency (), celerix::thread_role::name thread_role = celerix::thread_role::name::io);
	~thread_runner ();

	// Wait for IO threads to complete
	void join ();

	// Tells the IO context to stop processing events.
	// TODO: Ideally this shouldn't be needed, node should stop gracefully by cancelling any outstanding async operations and calling join()
	void abort ();

private:
	void start ();

	unsigned const num_threads;
	celerix::thread_role::name const role;
	celerix::logger & logger;
	std::shared_ptr<asio::io_context> io_ctx;
	asio::executor_work_guard<asio::io_context::executor_type> io_guard;
	std::vector<boost::thread> threads;

private:
	void run ();
};

constexpr unsigned asio_handler_tracking_threshold ()
{
#if CELERIX_ASIO_HANDLER_TRACKING == 0
	return 0;
#else
	return CELERIX_ASIO_HANDLER_TRACKING;
#endif
}
} // namespace celerix
