#pragma once

#include <nano/lib/logging.hpp>
#include <nano/lib/utility.hpp>

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <boost/thread.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace nano
{
/**
 * Manages signal handling and allows to register custom handlers for any signal.
 * IMPORTANT NOTE: only one instance of this class should be instantiated per process.
 * IMPORTANT NOTE: this is an add-only class, there is currently no way to remove a handler,
   although that functionality could be easily be added if needed.
 */
class signal_manager final
{
public:
	/** The signal manager expects to have a boost asio context */
	signal_manager ();

	/** stops the signal manager io context and wait for the thread to finish */
	~signal_manager ();

	/** Register a handler for a signal to be called from a safe context.
	 *  The handler will be called from the "ioc" io context.
	 */
	void register_signal_handler (int signum, std::function<void (int)> handler, bool repeat);

private:
	struct signal_descriptor final
	{
		signal_descriptor (std::shared_ptr<boost::asio::signal_set> sigset_a, signal_manager & sigman_a, std::function<void (int)> handler_func_a, bool repeat_a);

		/** a signal set that maps signals to signal handler and provides the connection to boost asio */
		std::shared_ptr<boost::asio::signal_set> sigset;

		/** reference to the signal manager that owns this signal descriptor */
		signal_manager & sigman;

		/** the caller supplied function to call from the base signal handler */
		std::function<void (int)> handler_func;

		/** indicates if the signal handler should continue handling a signal after receiving one */
		bool repeat;
	};

	/**
	 * This is the actual handler that is registered with boost asio.
	 * It calls the caller supplied function (if one is given) and sets the handler to repeat (or not).
	 */
	static void base_handler (nano::signal_manager::signal_descriptor descriptor, boost::system::error_code const & error, int signum);

	nano::logger logger;

	/** boost asio context to use */
	boost::asio::io_context ioc;

	/** work object to make the thread run function live as long as a signal manager */
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work;

	/** a list of descriptors to hold data contexts needed by the asyncronous handlers */
	std::vector<signal_descriptor> descriptor_list;

	/** thread to service the signal manager io context */
	boost::thread smthread;
};

}
