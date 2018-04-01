#include <banano/node/working.hpp>

#include <shlobj.h>

namespace rai
{
boost::filesystem::path app_path ()
{
	boost::filesystem::path result;
	WCHAR path[MAX_PATH];
	if (SUCCEEDED (SHGetFolderPathW (NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path)))
	{
		result = boost::filesystem::path (path);
	}
	else
	{
		assert (false);
	}
	return result;
}
}