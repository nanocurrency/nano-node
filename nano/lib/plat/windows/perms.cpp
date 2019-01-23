#include <boost/filesystem.hpp>
#include <cassert>
#include <nano/lib/utility.hpp>

#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

void nano::set_umask ()
{
	int oldMode;

	auto result (_umask_s (_S_IWRITE | _S_IREAD, &oldMode));
	assert (result == 0);
}

void nano::set_secure_perm_directory (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all);
}

void nano::set_secure_perm_directory (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all, ec);
}

void nano::set_secure_perm_file (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_read | boost::filesystem::owner_write);
}

void nano::set_secure_perm_file (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_read | boost::filesystem::owner_write, ec);
}
