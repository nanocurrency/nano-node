#include <nano/lib/utility.hpp>

// clang-format off
// Keep windows.h header at the top
#include <windows.h>
#include <io.h>
#include <processthreadsapi.h>
#include <sys/stat.h>
// clang-format on

void nano::set_umask ()
{
	int oldMode;

	auto result (_umask_s (_S_IWRITE | _S_IREAD, &oldMode));
	debug_assert (result == 0);
}

void nano::set_secure_perm_directory (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_all);
}

void nano::set_secure_perm_directory (std::filesystem::path const & path, std::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_all, ec);
}

void nano::set_secure_perm_file (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

void nano::set_secure_perm_file (std::filesystem::path const & path, std::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
}

bool nano::is_windows_elevated ()
{
	bool is_elevated = false;
	HANDLE h_token = nullptr;
	if (OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &h_token))
	{
		TOKEN_ELEVATION elevation;
		DWORD cb_size = sizeof (TOKEN_ELEVATION);
		if (GetTokenInformation (h_token, TokenElevation, &elevation, sizeof (elevation), &cb_size))
		{
			is_elevated = elevation.TokenIsElevated;
		}
	}
	if (h_token)
	{
		CloseHandle (h_token);
	}
	return is_elevated;
}
