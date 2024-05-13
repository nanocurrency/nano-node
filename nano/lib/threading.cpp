#include <nano/lib/config.hpp>
#include <nano/lib/env.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>

#include <thread>

/*
 * thread_attributes
 */

boost::thread::attributes nano::thread_attributes::get_default ()
{
	boost::thread::attributes attrs;
	attrs.set_stack_size (8000000); // 8MB
	return attrs;
}

unsigned nano::hardware_concurrency ()
{
	static auto const concurrency = [] () {
		if (auto value = nano::env::get<unsigned> ("NANO_HARDWARE_CONCURRENCY"))
		{
			std::cerr << "Hardware concurrency overridden by NANO_HARDWARE_CONCURRENCY environment variable: " << *value << std::endl;
			return *value;
		}
		return std::thread::hardware_concurrency ();
	}();
	release_assert (concurrency > 0, "configured hardware concurrency must be non zero");
	return concurrency;
}

bool nano::join_or_pass (std::thread & thread)
{
	if (thread.joinable ())
	{
		thread.join ();
		return true;
	}
	else
	{
		return false;
	}
}
