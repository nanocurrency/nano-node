#include <rai/home.hpp>

#include <shlobj.h>

namespace rai
{
boost::filesystem::path home_path ()
{
	boost::filesystem::path result;
	WCHAR path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path)))
	{
		result = boost::filesystem::path (path);
		result /= "RaiBlocks";
	}
	else
	{
		assert (false);
	}
}
}