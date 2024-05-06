#include <nano/secure/working.hpp>

#include <shlobj.h>

namespace nano
{
std::filesystem::path app_path_impl ()
{
	std::filesystem::path result;
	WCHAR path[MAX_PATH];
	if (SUCCEEDED (SHGetFolderPathW (NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path)))
	{
		result = std::filesystem::path (path);
	}
	else
	{
		debug_assert (false);
	}
	return result;
}
}