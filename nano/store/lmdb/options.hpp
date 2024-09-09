#pragma once

#include <nano/lib/lmdbconfig.hpp>

namespace nano::store::lmdb
{
class env;
}

namespace nano::store::lmdb
{
/** Environment options, most of which originates from the config file. */
class options final
{
public:
	static options make ()
	{
		return options ();
	}

	options &
	set_config (nano::lmdb_config config_a);

	options & set_use_no_mem_init (int use_no_mem_init_a);

	/** Used by the wallet to override the config map size */
	options & override_config_map_size (std::size_t map_size_a);

	/** Used by the wallet to override the sync strategy */
	options & override_config_sync (nano::lmdb_config::sync_strategy sync_a);

	options & apply (nano::store::lmdb::env & env);
	unsigned int flags () const;

private:
	bool use_no_mem_init{ false };
	nano::lmdb_config config;
};
}
