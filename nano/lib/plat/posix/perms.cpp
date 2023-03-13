#include <nano/lib/utility.hpp>

#include <boost/filesystem.hpp>

#include <sys/stat.h>
#include <sys/types.h>

void nano::set_umask ()
{
	umask (077);
}

void nano::set_secure_perm_directory (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::owner_all);
}

void nano::set_secure_perm_directory (std::filesystem::path const & path, boost::system::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::owner_all, ec);
}

void nano::set_secure_perm_file (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

void nano::set_secure_perm_file (std::filesystem::path const & path, boost::system::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
}
