#include <atomic>
#include <iostream>
#include <rai/lib/debug.hpp>
#include <stdint.h>

std::ostream & rai::debug::internal (rai::debug::subsystem subsystem, rai::debug::level level, const char * function, int line)
{
	static thread_local uint64_t debug_thread_id = -1;
	static std::atomic<uint64_t> debug_thread_id_counter{ 0 };
	std::ostream *out_stream;

	out_stream = &std::cerr;

	if (debug_thread_id == -1) {
		debug_thread_id = debug_thread_id_counter.fetch_add(1, std::memory_order_relaxed);
	}

	/* XXX:TODO: Move this boost log */
	(*out_stream) << "[DEBUG] " << time(NULL) << " [Thread#" << debug_thread_id << "] " << function << ":" << line << "/" << ((int) subsystem) << "." << ((int) level) << ": ";

	return (*out_stream);
}
