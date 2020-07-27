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
	uint8_t memory_multiplier{ 2 };
	unsigned io_threads{ std::thread::hardware_concurrency () };
};
}
