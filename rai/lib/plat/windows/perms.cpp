#include <assert.h>
#include <boost/filesystem.hpp>
#include <rai/lib/utility.hpp>

#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

void rai::set_umask ()
{
	int oldMode;

	auto result (_umask_s (_S_IWRITE | _S_IREAD, &oldMode));
	assert (result == 0);
}

void rai::set_secure_perm_directory (boost::filesystem::path const & path)
{
	/*
	 * XXX:TODO: Set the permissions sanely; For now we rely on umask
	 */
}

void rai::set_secure_perm_directory (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	/*
	 * XXX:TODO: Set the permissions sanely; For now we rely on umask
	 */
}

void rai::set_secure_perm_file (boost::filesystem::path const & path)
{
	/*
	 * XXX:TODO: Set the permissions sanely; For now we rely on umask
	 */
}

void rai::set_secure_perm_file (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	/*
	 * XXX:TODO: Set the permissions sanely; For now we rely on umask
	 */
}
