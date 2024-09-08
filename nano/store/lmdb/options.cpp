#include <nano/store/lmdb/options.hpp>

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
