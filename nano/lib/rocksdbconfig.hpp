#pragma once

#include <celerix/lib/errors.hpp>
#include <celerix/lib/threading.hpp>

#include <thread>

namespace celerix
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

	celerix::error serialize_toml (celerix::tomlconfig &) const;
	celerix::error deserialize_toml (celerix::tomlconfig &);

	/** To use RocksDB in tests make sure the environment variable TEST_USE_ROCKSDB=1 is set */
	static bool using_rocksdb_in_tests ();

	bool enable{ false };
	unsigned io_threads{ std::max (celerix::hardware_concurrency () / 2, 1u) };
	long read_cache{ 32 };
	long write_cache{ 64 };
};
}
