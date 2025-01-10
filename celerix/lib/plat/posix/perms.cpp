#include <celerix/lib/files.hpp>
#include <celerix/lib/utility.hpp>

#include <sys/stat.h>
#include <sys/types.h>

void celerix::set_umask ()
{
	umask (077);
}

void celerix::set_secure_perm_directory (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_all);
}

void celerix::set_secure_perm_directory (std::filesystem::path const & path, std::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_all, ec);
}

void celerix::set_secure_perm_file (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

void celerix::set_secure_perm_file (std::filesystem::path const & path, std::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
}
