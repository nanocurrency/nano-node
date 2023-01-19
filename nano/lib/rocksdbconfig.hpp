#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/threading.hpp>

#include <thread>

namespace nano
{
class tomlconfig;

/** Configuration options for RocksDB */
class rocksdb_config final
{
public:
	rocksdb_config () :
		enable{ using_rocksdb_in_tests () }
	{
	}
	nano::error serialize_toml (nano::tomlconfig & toml_a) const;
	nano::error deserialize_toml (nano::tomlconfig & toml_a);

	/** To use RocksDB in tests make sure the environment variable TEST_USE_ROCKSDB=1 is set */
	static bool using_rocksdb_in_tests ();

	bool enable{ false };
	uint8_t memory_multiplier{ 2 };
	unsigned io_threads{ nano::hardware_concurrency () };
};
}
