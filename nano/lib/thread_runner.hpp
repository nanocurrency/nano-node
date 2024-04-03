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
class thread_runner final
{
	nano::logger logger;

public:
	thread_runner (std::shared_ptr<boost::asio::io_context>, unsigned num_threads = nano::hardware_concurrency (), nano::thread_role::name thread_role = nano::thread_role::name::io);
	~thread_runner ();

	/** Tells the IO context to stop processing events.*/
	void stop_event_processing ();

	/** Wait for IO threads to complete */
	void join ();

private:
	std::shared_ptr<boost::asio::io_context> io_ctx;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard;
	nano::thread_role::name const role;
	std::vector<boost::thread> threads;

private:
	void run (boost::asio::io_context &);
};
} // namespace nano
