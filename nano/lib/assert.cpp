#include <nano/lib/assert.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stacktrace.hpp>

#include <boost/dll/runtime_symbol_info.hpp>

#include <fstream>
#include <iostream>

/*
 * Backing code for "release_assert" & "debug_assert", which are macros
 */
void assert_internal (char const * check_expr, char const * func, char const * file, unsigned int line, bool is_release_assert, std::string_view error_msg)
{
	std::stringstream ss;
	ss << "Assertion `" << check_expr << "` failed";
	if (!error_msg.empty ())
	{
		ss << ": " << error_msg;
	}
	ss << "\n";
	ss << file << ":" << line << " [" << func << "]"
	   << "'\n";

	// Output stack trace
	auto backtrace_str = nano::generate_stacktrace ();
	ss << backtrace_str;

	// Output both to standard error and the default logger, so that the error info is persisted in the nano specific log directory
	auto error_str = ss.str ();
	std::cerr << error_str << std::endl;
	nano::default_logger ().critical (nano::log::type::assert, "{}", error_str);

	// "abort" at the end of this function will go into any signal handlers (the daemon ones will generate a stack trace and load memory address files on non-Windows systems).
	// As there is no async-signal-safe way to generate stacktraces on Windows it must be done before aborting
#ifdef _WIN32
	{
		// Try construct the stacktrace dump in the same folder as the running executable, otherwise use the current directory.
		boost::system::error_code err;
		auto running_executable_filepath = boost::dll::program_location (err);
		std::string filename = is_release_assert ? "nano_node_backtrace_release_assert.txt" : "nano_node_backtrace_assert.txt";
		std::string filepath = filename;
		if (!err)
		{
			filepath = (running_executable_filepath.parent_path () / filename).string ();
		}

		std::ofstream file (filepath);
		nano::set_secure_perm_file (filepath);
		file << backtrace_str;
	}
#endif

	abort ();
}