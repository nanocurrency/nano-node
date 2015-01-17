#include <rai/working.hpp>

#include <sys/types.h>
#include <pwd.h>

namespace rai
{
boost::filesystem::path working_path ()
{
	auto entry (getpwuid (getuid ()));
	assert (entry != nullptr);
	boost::filesystem::path result (entry->pw_dir);
	result /= "RaiBlocks";
	return result;
}
}
