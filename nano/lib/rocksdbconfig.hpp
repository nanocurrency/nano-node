#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/threading.hpp>

#include <string>
#include <thread>

namespace nano
{
class tomlconfig;

class rocksdb_config final
{
public:
	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);

public:
	bool enable{ false };
	unsigned io_threads{ std::max (nano::hardware_concurrency () / 2, 1u) };
	long read_cache{ 32 };
	long write_cache{ 64 };
	unsigned max_log_files{ 100 };
	std::string log_level{ "warn" };
};
}
