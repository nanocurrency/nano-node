#pragma once

#include <nano/boost/asio/executor_work_guard.hpp>
#include <nano/boost/asio/io_context.hpp>
#include <nano/lib/utility.hpp>

#include <boost/thread/thread.hpp>

namespace nano
{
/*
 * Functions for understanding the role of the current thread
 */
namespace thread_role
{
	enum class name
	{
		unknown,
		io,
		work,
		packet_processing,
		alarm,
		vote_processing,
		block_processing,
		request_loop,
		wallet_actions,
		bootstrap_initiator,
		voting,
		signature_checking,
		rpc_request_processor,
		rpc_process_container,
		work_watcher,
		confirmation_height_processing,
		worker,
		request_aggregator
	};
	/*
	 * Get/Set the identifier for the current thread
	 */
	nano::thread_role::name get ();
	void set (nano::thread_role::name);

	/*
	 * Get the thread name as a string from enum
	 */
	std::string get_string (nano::thread_role::name);

	/*
	 * Get the current thread's role as a string
	 */
	std::string get_string ();

	/*
	 * Internal only, should not be called directly
	 */
	void set_os_name (std::string const &);
}

namespace thread_attributes
{
	void set (boost::thread::attributes &);
}

class thread_runner final
{
public:
	thread_runner (boost::asio::io_context &, unsigned);
	~thread_runner ();
	/** Tells the IO context to stop processing events.*/
	void stop_event_processing ();
	/** Wait for IO threads to complete */
	void join ();
	std::vector<boost::thread> threads;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard;
};
}