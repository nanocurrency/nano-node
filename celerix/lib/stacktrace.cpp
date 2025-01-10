#include <celerix/lib/stacktrace.hpp>

#include <boost/stacktrace.hpp>

#include <sstream>

void celerix::dump_crash_stacktrace ()
{
	boost::stacktrace::safe_dump_to ("celerix_node_backtrace.dump");
}

std::string celerix::generate_stacktrace ()
{
	auto stacktrace = boost::stacktrace::stacktrace ();
	std::stringstream ss;
	ss << stacktrace;
	return ss.str ();
}
