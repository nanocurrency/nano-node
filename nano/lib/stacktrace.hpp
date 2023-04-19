#pragma once

#include <string>

namespace nano
{
/**
 * Dumps a stacktrace file which can be read using the --debug_output_last_backtrace_dump CLI command
 */
void dump_crash_stacktrace ();

/**
 * Generates the current stacktrace
 */
std::string generate_stacktrace ();
}