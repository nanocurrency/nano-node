#pragma once

#ifndef NANO_DEBUG_SUBSYSTEM
#define NANO_DEBUG_SUBSYSTEM unknown
#endif

namespace nano {
	namespace debug {
		enum class level {
			trace   = 0, /* Trace message, entered/exited a function or branch */
			comment = 1, /* Comment about section of code */
			debug   = 2, /* Helpful messages for debuggers */
			note    = 3  /* Helpful notes for high-level status */
		};

		enum class subsystem {
			unknown,
			ledger,
			vote,
			network,
			bootstrap
		};

		std::ostream & internal (nano::debug::subsystem subsystem, nano::debug::level level, const char * function, int line);
	}
}

#ifndef NDEBUG
#define nano_debug (levelName, ostreamInput) { nano::debug::internal (nano::debug::subsystem::NANO_DEBUG_SUBSYSTEM, nano::debug::level::levelName, __func__, __LINE__) << ostreamInput << std::endl; }
#else
#define nano_debug (levelName, ostreamInput) /**/
#endif
#define nano_debug_trace_enter nano_debug(trace, "Entered")
#define nano_debug_trace_exit  nano_debug(trace, "Exit")
