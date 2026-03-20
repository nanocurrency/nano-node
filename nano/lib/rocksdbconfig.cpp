#include <nano/lib/config.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/lib/tomlconfig.hpp>

nano::error nano::rocksdb_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("io_threads", io_threads, "Number of threads to use with the background compaction and flushing.\ntype:uint32");
	toml.put ("read_cache", read_cache, "Amount of megabytes per table allocated to read cache. Valid range is 1 - 1024. Default is 32.\nCarefully monitor memory usage if non-default values are used\ntype:long");
	toml.put ("write_cache", write_cache, "Total amount of megabytes allocated to write cache. Valid range is 1 - 256. Default is 64.\nCarefully monitor memory usage if non-default values are used\ntype:long");
	toml.put ("max_log_files", max_log_files, "Maximum number of RocksDB info log files to keep. Default is 100.\ntype:uint32");
	toml.put ("log_level", log_level, "RocksDB log level. One of: debug, info, warn, error, fatal. Default is warn.\ntype:string");

	return toml.get_error ();
}

nano::error nano::rocksdb_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable); // TODO: This setting can be removed in future versions
	toml.get_optional<unsigned> ("io_threads", io_threads);
	toml.get_optional<long> ("read_cache", read_cache);
	toml.get_optional<long> ("write_cache", write_cache);
	toml.get_optional<unsigned> ("max_log_files", max_log_files);
	toml.get_optional<std::string> ("log_level", log_level);

	// Validate ranges
	if (io_threads == 0)
	{
		toml.get_error ().set ("io_threads must be non-zero");
	}

	if (max_log_files == 0)
	{
		toml.get_error ().set ("max_log_files must be greater than 0");
	}

	if (log_level != "debug" && log_level != "info" && log_level != "warn" && log_level != "error" && log_level != "fatal")
	{
		toml.get_error ().set ("log_level must be one of: debug, info, warn, error, fatal");
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
