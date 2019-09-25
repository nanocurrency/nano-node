#include <nano/lib/rocksdbconfig.hpp>
#include <nano/lib/tomlconfig.hpp>

nano::error nano::rocksdb_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Whether to use the RocksDB backend for the ledger database\ntype:bool");
	toml.put ("enable_pipelined_write", enable_pipelined_write, "Whether to use 2 separate write queues for memtable/WAL, true is recommended.\ntype:bool");
	toml.put ("cache_index_and_filter_blocks", cache_index_and_filter_blocks, "Whether index and filter blocks are stored in block_cache, true is recommended.\ntype:bool");
	toml.put ("bloom_filter_bits", bloom_filter_bits, "Number of bits to use with a bloom filter. Helps with point reads but uses more memory. 0 disables the bloom filter, 10 is recommended\ntype:uint32");
	toml.put ("block_cache", block_cache, "Size (MB) of the block cache; A larger number will increase performance of read operations. At least 512MB is recommended.\ntype:uint64");
	toml.put ("io_threads", io_threads, "Number of threads to use with the background compaction and flushing. Number of hardware threads is recommended.\ntype:uint32");
	toml.put ("block_size", block_size, "Uncompressed data (KBs) per block. Increasing block size decreases memory usage and space amplification, but increases read amplification. 16 is recommended.\ntype:uint32");
	toml.put ("num_memtables", num_memtables, "Number of memtables to keep in memory per column family. 2 is the minimum, 3 is recommended.\ntype:uint32");
	toml.put ("memtable_size", memtable_size, "Amount of memory (MB) to build up before flushing to disk for an individual column family. Large values increase performance. 64 or 128 is recommended\ntype:uint32");
	toml.put ("total_memtable_size", total_memtable_size, "Total memory (MB) which can be used across all memtables, set to 0 for unconstrained.\ntype:uint32");
	return toml.get_error ();
}

nano::error nano::rocksdb_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<bool> ("enable_pipelined_write", enable_pipelined_write);
	toml.get_optional<bool> ("cache_index_and_filter_blocks", cache_index_and_filter_blocks);
	toml.get_optional<unsigned> ("bloom_filter_bits", bloom_filter_bits);
	toml.get_optional<uint64_t> ("block_cache", block_cache);
	toml.get_optional<unsigned> ("io_threads", io_threads);
	toml.get_optional<unsigned> ("block_size", block_size);
	toml.get_optional<unsigned> ("num_memtables", num_memtables);
	toml.get_optional<unsigned> ("memtable_size", memtable_size);
	toml.get_optional<unsigned> ("total_memtable_size", total_memtable_size);

	// Validate ranges
	if (bloom_filter_bits > 100)
	{
		toml.get_error ().set ("bloom_filter_bits is too high");
	}
	if (num_memtables < 2)
	{
		toml.get_error ().set ("num_memtables must be at least 2");
	}
	if (memtable_size == 0)
	{
		toml.get_error ().set ("memtable_size must be non-zero");
	}
	if ((total_memtable_size < memtable_size * 8) && (total_memtable_size != 0))
	{
		toml.get_error ().set ("total_memtable_size should be at least 8 times greater than memtable_size or be set to 0");
	}
	if (io_threads == 0)
	{
		toml.get_error ().set ("io_threads must be non-zero");
	}
	if (block_size == 0)
	{
		toml.get_error ().set ("block_size must be non-zero");
	}

	return toml.get_error ();
}
