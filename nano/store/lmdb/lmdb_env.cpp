#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/lmdb/common.hpp>
#include <nano/store/lmdb/lmdb_env.hpp>

nano::store::lmdb::env::env (std::filesystem::path const & path_a, nano::store::lmdb::env::options options_a) :
	database_path{ path_a }
{
	init (path_a, options_a);
}

void nano::store::lmdb::env::init (std::filesystem::path const & path_a, nano::store::lmdb::env::options options_a)
{
	debug_assert (path_a.extension () == ".ldb", "invalid filename extension for lmdb database file");

	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		std::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		nano::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			MDB_env * environment;
			auto status1 (mdb_env_create (&environment));
			release_assert (success (status1), error_string (status1));
			this->environment.reset (environment);
			auto status2 (mdb_env_set_maxdbs (environment, options_a.config.max_databases));
			release_assert (success (status2), error_string (status2));
			auto map_size = options_a.config.map_size;
			auto max_instrumented_map_size = 16 * 1024 * 1024;
			if (memory_intensive_instrumentation () && map_size > max_instrumented_map_size)
			{
				// In order to run LMDB with some types of memory instrumentation, the maximum map size must be smaller than what is normally used when non-instrumented
				map_size = max_instrumented_map_size;
			}
			auto status3 (mdb_env_set_mapsize (environment, map_size));
			release_assert (success (status3), error_string (status3));

			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			// MDB_NORDAHEAD will allow platforms that support it to load the DB in memory as needed.
			// MDB_NOMEMINIT prevents zeroing malloc'ed pages. Can provide improvement for non-sensitive data but may make memory checkers noisy (e.g valgrind).
			auto environment_flags = MDB_NOSUBDIR | MDB_NOTLS | MDB_NORDAHEAD;
			if (options_a.config.sync == nano::lmdb_config::sync_strategy::nosync_safe)
			{
				environment_flags |= MDB_NOMETASYNC;
			}
			else if (options_a.config.sync == nano::lmdb_config::sync_strategy::nosync_unsafe)
			{
				environment_flags |= MDB_NOSYNC;
			}
			else if (options_a.config.sync == nano::lmdb_config::sync_strategy::nosync_unsafe_large_memory)
			{
				environment_flags |= MDB_NOSYNC | MDB_WRITEMAP | MDB_MAPASYNC;
			}

			if (options_a.read_only)
			{
				environment_flags |= MDB_RDONLY;
			}

			if (!memory_intensive_instrumentation () && options_a.use_no_mem_init)
			{
				environment_flags |= MDB_NOMEMINIT;
			}

			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), environment_flags, 00600));
			if (!success (status4))
			{
				if (status4 == ENOENT)
				{
					// Expected errors are packaged as nano::error and handled at a higher level
					throw nano::error (nano::error_backend::db_not_found);
				}
				else
				{
					std::string message = "Could not open lmdb environment: (" + std::to_string (status4) + ") " + mdb_strerror (status4);
					nano::default_logger ().error (nano::log::type::lmdb, "{}", message);
					throw std::runtime_error (message);
				}
			}
			release_assert (success (status4), error_string (status4));
		}
		else
		{
			throw std::runtime_error ("Could not create database directory: " + error_mkdir.message ());
		}
	}
	else
	{
		throw std::runtime_error ("Invalid database path: path must have parent directory");
	}
}

nano::store::lmdb::env::~env ()
{
	if (environment != nullptr)
	{
		// Make sure the commits are flushed. This is a no-op unless MDB_NOSYNC is used.
		mdb_env_sync (environment.get (), true);
	}
}

nano::store::lmdb::env::operator MDB_env * () const
{
	return environment.get ();
}

nano::store::read_transaction nano::store::lmdb::env::tx_begin_read (nano::store::txn_callbacks callbacks) const
{
	return store::read_transaction{ std::make_unique<nano::store::lmdb::read_transaction_impl> (*this, callbacks) };
}

nano::store::write_transaction nano::store::lmdb::env::tx_begin_write (nano::store::txn_callbacks callbacks) const
{
	return store::write_transaction{ std::make_unique<nano::store::lmdb::write_transaction_impl> (*this, callbacks) };
}

MDB_txn * nano::store::lmdb::env::tx (store::transaction const & transaction_a) const
{
	debug_assert (transaction_a.store_id () == store_id);
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}

void nano::store::lmdb::env::create_backup_file (std::filesystem::path const & filepath, nano::logger & logger) const
{
	auto extension = filepath.extension ();
	auto filename_without_extension = filepath.filename ().replace_extension ("");
	auto orig_filepath = filepath;
	auto & backup_path = orig_filepath.remove_filename ();
	auto backup_filename = filename_without_extension;
	backup_filename += "_backup_";
	backup_filename += std::to_string (std::chrono::system_clock::now ().time_since_epoch ().count ());
	backup_filename += extension;
	auto backup_filepath = backup_path / backup_filename;

	logger.info (nano::log::type::lmdb, "Performing {} backup before database upgrade...", filepath.filename ().string ());

	auto error (mdb_env_copy (*this, backup_filepath.string ().c_str ()));
	if (error)
	{
		logger.critical (nano::log::type::lmdb, "Database backup failed");
		std::exit (1);
	}
	else
	{
		logger.info (nano::log::type::lmdb, "Database backup completed. Backup can be found at: {}", backup_filepath.string ());
	}
}