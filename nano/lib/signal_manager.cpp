#include <nano/lib/signal_manager.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/format.hpp>

#include <iostream>

nano::signal_manager::signal_manager () :
	work (boost::asio::make_work_guard (ioc))
{
	thread = std::thread ([&ioc = ioc] () {
		nano::thread_role::set (nano::thread_role::name::signal_manager);
		ioc.run ();
	});
}

nano::signal_manager::~signal_manager ()
{
	/// Indicate that we have finished with the private io_context. Its
	/// io_context::run() function will exit once all other work has completed.
	work.reset ();
	ioc.stop ();
	thread.join ();
}

nano::signal_manager::signal_descriptor::signal_descriptor (std::shared_ptr<boost::asio::signal_set> sigset_a, signal_manager & sigman_a, std::function<void (int)> handler_func_a, bool repeat_a) :
	sigset (sigset_a), sigman (sigman_a), handler_func (handler_func_a), repeat (repeat_a)
{
}

void nano::signal_manager::register_signal_handler (int signum, std::function<void (int)> handler, bool repeat)
{
	// create a signal set to hold the mapping between signals and signal handlers
	auto sigset = std::make_shared<boost::asio::signal_set> (ioc, signum);

	// a signal descriptor holds all the data needed by the base handler including the signal set
	// working with copies of a descriptor is OK
	signal_descriptor descriptor (sigset, *this, handler, repeat);

	// ensure the signal set and descriptors live long enough
	descriptor_list.push_back (descriptor);

	// asynchronously listen for signals from this signal set
	sigset->async_wait ([descriptor] (boost::system::error_code const & error, int signum) {
		nano::signal_manager::base_handler (descriptor, error, signum);
	});

	log (boost::str (boost::format ("Registered signal handler for signal %d") % signum));
}

void nano::signal_manager::base_handler (nano::signal_manager::signal_descriptor descriptor, boost::system::error_code const & error, int signum)
{
	if (!error)
	{
		descriptor.sigman.log (boost::str (boost::format ("Signal received: %d") % signum));

		// call the user supplied function, if one is provided
		if (descriptor.handler_func)
		{
			descriptor.handler_func (signum);
		}

		// continue asynchronously listening for signals from this signal set
		if (descriptor.repeat)
		{
			descriptor.sigset->async_wait ([descriptor] (boost::system::error_code const & error, int signum) {
				nano::signal_manager::base_handler (descriptor, error, signum);
			});
		}
		else
		{
			descriptor.sigman.log (boost::str (boost::format ("Signal handler %d will not repeat") % signum));
			descriptor.sigset->clear ();
		}

		descriptor.sigman.log (boost::str (boost::format ("Signal processed: %d") % signum));
	}
	else
	{
		descriptor.sigman.log (boost::str (boost::format ("Signal error: %d (%s)") % error.value () % error.message ()));
	}
}
