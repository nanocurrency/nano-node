#include <nano/lib/utility.hpp>
#include <nano/secure/working.hpp>

#include <boost/filesystem.hpp>

#include <pwd.h>
#include <sys/types.h>

namespace nano
{
boost::filesystem::path app_path ()
{
	auto entry (getpwuid (getuid ()));
	debug_assert (entry != nullptr);
	boost::filesystem::path result (entry->pw_dir);
	return result;
}
}
