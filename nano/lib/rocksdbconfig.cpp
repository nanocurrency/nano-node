#include <nano/lib/config.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/lib/tomlconfig.hpp>

nano::error nano::rocksdb_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Whether to use the RocksDB backend for the ledger database.\ntype:bool");
	toml.put ("cache_size", cache_size, "Amount of memory in MB used for caching for each table. Valid values are from 1 to 1024. Default is 64.\ntype:uint8");
	toml.put ("io_threads", io_threads, "Number of threads to use with the background compaction and flushing.\ntype:uint32");
	return toml.get_error ();
}

nano::error nano::rocksdb_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<uint16_t> ("cache_size", cache_size);
	toml.get_optional<unsigned> ("io_threads", io_threads);

	// Validate ranges
	if (io_threads == 0)
	{
		toml.get_error ().set ("io_threads must be non-zero");
	}
	if (cache_size < 1 || cache_size > 1024)
	{
		toml.get_error ().set ("cache_size must be between 1 and 1024 MB");
	}

	return toml.get_error ();
}

bool nano::rocksdb_config::using_rocksdb_in_tests ()
{
	auto use_rocksdb_str = std::getenv ("TEST_USE_ROCKSDB");
	return use_rocksdb_str && (boost::lexical_cast<int> (use_rocksdb_str) == 1);
}
