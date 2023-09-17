#pragma once

#include <nano/lib/relaxed_atomic.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>

#include <boost/thread/thread.hpp>

#include <latch>
#include <thread>

namespace boost::asio
{
class thread_pool;
}

namespace nano
{
namespace thread_attributes
{
	boost::thread::attributes get_default ();
}

/**
 * Number of available logical processor cores. Might be overridden by setting `NANO_HARDWARE_CONCURRENCY` environment variable
 */
unsigned int hardware_concurrency ();

/**
 * If thread is joinable joins it, otherwise does nothing
 */
bool join_or_pass (std::thread &);
}
