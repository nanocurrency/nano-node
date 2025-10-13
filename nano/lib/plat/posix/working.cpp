#include <nano/lib/files.hpp>
#include <nano/lib/utility.hpp>

#include <pwd.h>
#include <sys/types.h>

namespace nano
{
std::filesystem::path app_path_impl ()
{
	auto entry (getpwuid (getuid ()));
	debug_assert (entry != nullptr);
	std::filesystem::path result (entry->pw_dir);
	return result;
}
}
