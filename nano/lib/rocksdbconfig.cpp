#include <nano/lib/config.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/lib/tomlconfig.hpp>

nano::error nano::rocksdb_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Whether to use the RocksDB backend for the ledger database.\ntype:bool");
	toml.put ("io_threads", io_threads, "Number of threads to use with the background compaction and flushing.\ntype:uint32");
	toml.put ("read_cache", read_cache, "Amount of megabytes per table allocated to read cache. Valid range is 1 - 1024. Default is 32.\nCarefully monitor memory usage if non-default values are used\ntype:long");
	toml.put ("write_cache", write_cache, "Total amount of megabytes allocated to write cache. Valid range is 1 - 256. Default is 64.\nCarefully monitor memory usage if non-default values are used\ntype:long");

	return toml.get_error ();
}

nano::error nano::rocksdb_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<unsigned> ("io_threads", io_threads);
	toml.get_optional<long> ("read_cache", read_cache);
	toml.get_optional<long> ("write_cache", write_cache);

	// Validate ranges
	if (io_threads == 0)
	{
		toml.get_error ().set ("io_threads must be non-zero");
	}

	if (read_cache < 1 || read_cache > 1024)
	{
		toml.get_error ().set ("read_cache must be between 1 and 1024 MB");
	}

	if (write_cache < 1 || write_cache > 256)
	{
		toml.get_error ().set ("write_cache must be between 1 and 256 MB");
	}

	return toml.get_error ();
}

bool nano::rocksdb_config::using_rocksdb_in_tests ()
{
	auto use_rocksdb_str = std::getenv ("TEST_USE_ROCKSDB");
	return use_rocksdb_str && (boost::lexical_cast<int> (use_rocksdb_str) == 1);
}
