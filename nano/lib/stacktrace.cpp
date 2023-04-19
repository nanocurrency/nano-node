#include <nano/lib/stacktrace.hpp>

#include <boost/stacktrace.hpp>

#include <sstream>

void nano::dump_crash_stacktrace ()
{
	boost::stacktrace::safe_dump_to ("nano_node_backtrace.dump");
}

std::string nano::generate_stacktrace ()
{
	auto stacktrace = boost::stacktrace::stacktrace ();
	std::stringstream ss;
	ss << stacktrace;
	return ss.str ();
}
