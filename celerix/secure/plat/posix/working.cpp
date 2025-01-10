#include <celerix/lib/utility.hpp>
#include <celerix/secure/working.hpp>

#include <pwd.h>
#include <sys/types.h>

namespace celerix
{
std::filesystem::path app_path_impl ()
{
	auto entry (getpwuid (getuid ()));
	debug_assert (entry != nullptr);
	std::filesystem::path result (entry->pw_dir);
	return result;
}
}
