#pragma once
#include <nano/boost/asio/deadline_timer.hpp>
#include <nano/boost/asio/executor_work_guard.hpp>
#include <nano/boost/asio/io_context.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>

#include <boost/thread.hpp>

namespace nano
{
class thread_runner final
{
public:
	thread_runner (boost::asio::io_context &, unsigned num_threads, nano::thread_role::name thread_role = nano::thread_role::name::io);
	~thread_runner ();

	/** Tells the IO context to stop processing events.*/
	void stop_event_processing ();
	/** Wait for IO threads to complete */
	void join ();

private:
	nano::thread_role::name const role;
	std::vector<boost::thread> threads;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard;

private:
	void run (boost::asio::io_context &);
};
} // namespace nano
