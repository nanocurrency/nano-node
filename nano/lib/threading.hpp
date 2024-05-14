#pragma once

#include <nano/lib/utility.hpp>

#include <boost/thread/thread.hpp>

#include <thread>

namespace nano
{
namespace thread_attributes
{
	boost::thread::attributes get_default ();
} // namespace thread_attributes

/**
 * Number of available logical processor cores. Might be overridden by setting `NANO_HARDWARE_CONCURRENCY` environment variable
 */
unsigned hardware_concurrency ();

/**
 * If thread is joinable joins it, otherwise does nothing
 * Returns thread.joinable()
 */
bool join_or_pass (std::thread &);
} // namespace nano
