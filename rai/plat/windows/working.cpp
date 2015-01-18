#include <rai/working.hpp>

#include <shlobj.h>

namespace rai
{
boost::filesystem::path working_path ()
{
	boost::filesystem::path result;
	WCHAR path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path)))
	{
		result = boost::filesystem::path (path);
		result /= "RaiBlocks";
	}
	else
	{
		assert (false);
	}
	return result;
}
}