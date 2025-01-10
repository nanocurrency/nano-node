#include <celerix/lib/config.hpp>
#include <celerix/lib/env.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/threading.hpp>

#include <thread>

/*
 * thread_attributes
 */

boost::thread::attributes celerix::thread_attributes::get_default ()
{
	boost::thread::attributes attrs;
	attrs.set_stack_size (8000000); // 8MB
	return attrs;
}

unsigned celerix::hardware_concurrency ()
{
	static auto const concurrency = [] () {
		if (auto value = celerix::env::get<unsigned> ("CELERIX_HARDWARE_CONCURRENCY"))
		{
			std::cerr << "Hardware concurrency overridden by CELERIX_HARDWARE_CONCURRENCY environment variable: " << *value << std::endl;
			return *value;
		}
		return std::thread::hardware_concurrency ();
	}();
	release_assert (concurrency > 0, "configured hardware concurrency must be non zero");
	return concurrency;
}

bool celerix::join_or_pass (std::thread & thread)
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
