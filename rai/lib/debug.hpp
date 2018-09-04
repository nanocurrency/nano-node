#pragma once

#ifndef RAI_DEBUG_SUBSYSTEM
#define RAI_DEBUG_SUBSYSTEM unknown
#endif

namespace rai {
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

		std::ostream & internal (rai::debug::subsystem subsystem, rai::debug::level level, const char * function, int line);
	}
}

#ifndef NDEBUG
#define rai_debug(levelName, ostreamInput) { rai::debug::internal (rai::debug::subsystem::RAI_DEBUG_SUBSYSTEM, rai::debug::level::levelName, __func__, __LINE__) << ostreamInput << std::endl; }
#else
#define rai_debug(levelName, ostreamInput) /**/
#endif
#define rai_debug_trace_enter rai_debug(trace, "Entered")
#define rai_debug_trace_exit  rai_debug(trace, "Exit")
