#include <nano/lib/utility.hpp>
#include <nano/store/lmdb/lmdb_env.hpp>

#include <boost/system/error_code.hpp>

nano::store::lmdb::env::env (bool & error_a, std::filesystem::path const & path_a, nano::store::lmdb::options options_a)
{
	init (error_a, path_a, options_a);
}

void nano::store::lmdb::env::init (bool & error_a, std::filesystem::path const & path_a, nano::store::lmdb::options options_a)
{
	debug_assert (path_a.extension () == ".ldb", "invalid filename extension for lmdb database file");

	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		std::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		nano::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			auto status1 (mdb_env_create (&environment));
			release_assert (status1 == 0);
			options_a.apply (*this);
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), options_a.flags (), 00600));
			if (status4 != 0)
			{
				std::string message = "Could not open lmdb environment(" + std::to_string (status4) + "): " + mdb_strerror (status4);
				throw std::runtime_error (message);
			}
			release_assert (status4 == 0);
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

nano::store::lmdb::env::~env ()
{
	if (environment != nullptr)
	{
		// Make sure the commits are flushed. This is a no-op unless MDB_NOSYNC is used.
		mdb_env_sync (environment, true);
		mdb_env_close (environment);
	}
}

nano::store::lmdb::env::operator MDB_env * () const
{
	return environment;
}
