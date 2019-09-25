#pragma once

#include <nano/lib/errors.hpp>

#include <thread>

namespace nano
{
class tomlconfig;

/** Configuration options for RocksDB */
class rocksdb_config final
{
public:
	nano::error serialize_toml (nano::tomlconfig & toml_a) const;
	nano::error deserialize_toml (nano::tomlconfig & toml_a);

	bool enable{ false };
	unsigned bloom_filter_bits{ 0 };
	uint64_t block_cache{ 64 }; // MB
	unsigned io_threads{ std::thread::hardware_concurrency () };
	bool enable_pipelined_write{ false };
	bool cache_index_and_filter_blocks{ false };
	unsigned block_size{ 4 }; // KB
	unsigned memtable_size{ 32 }; // MB
	unsigned num_memtables{ 2 }; // Need a minimum of 2
	unsigned total_memtable_size{ 512 }; // MB
};
}
