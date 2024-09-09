#include <nano/lib/config.hpp>
#include <nano/store/lmdb/options.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

auto nano::store::lmdb::options::set_config (nano::lmdb_config config_a) -> options &
{
	config = config_a;
	return *this;
}

auto nano::store::lmdb::options::set_use_no_mem_init (int use_no_mem_init_a) -> options &
{
	use_no_mem_init = use_no_mem_init_a;
	return *this;
}

auto nano::store::lmdb::options::override_config_map_size (std::size_t map_size_a) -> options &
{
	config.map_size = map_size_a;
	return *this;
}

auto nano::store::lmdb::options::override_config_sync (nano::lmdb_config::sync_strategy sync_a) -> options &
{
	config.sync = sync_a;
	return *this;
}

auto nano::store::lmdb::options::apply (::lmdb::env & env) -> options &
{
	env.set_max_dbs (config.max_databases);
	auto map_size = config.map_size;
	auto max_instrumented_map_size = 16 * 1024 * 1024;
	if (memory_intensive_instrumentation () && map_size > max_instrumented_map_size)
	{
		// In order to run LMDB with some types of memory instrumentation, the maximum map size must be smaller than what is normally used when non-instrumented
		map_size = max_instrumented_map_size;
	}
	env.set_mapsize (map_size);
	return *this;
}

auto nano::store::lmdb::options::flags () const -> unsigned int
{
	// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
	// This can happen if something like 256 io_threads are specified in the node config
	// MDB_NORDAHEAD will allow platforms that support it to load the DB in memory as needed.
	// MDB_NOMEMINIT prevents zeroing malloc'ed pages. Can provide improvement for non-sensitive data but may make memory checkers noisy (e.g valgrind).
	unsigned int environment_flags = MDB_NOSUBDIR | MDB_NOTLS | MDB_NORDAHEAD;
	if (config.sync == nano::lmdb_config::sync_strategy::nosync_safe)
	{
		environment_flags |= MDB_NOMETASYNC;
	}
	else if (config.sync == nano::lmdb_config::sync_strategy::nosync_unsafe)
	{
		environment_flags |= MDB_NOSYNC;
	}
	else if (config.sync == nano::lmdb_config::sync_strategy::nosync_unsafe_large_memory)
	{
		environment_flags |= MDB_NOSYNC | MDB_WRITEMAP | MDB_MAPASYNC;
	}
	if (!memory_intensive_instrumentation () && use_no_mem_init)
	{
		environment_flags |= MDB_NOMEMINIT;
	}
	return environment_flags;
}
