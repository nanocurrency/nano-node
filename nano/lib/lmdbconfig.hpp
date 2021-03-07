#pragma once

#include <nano/lib/errors.hpp>

#include <thread>

namespace nano
{
class tomlconfig;

/** Configuration options for LMDB */
class lmdb_config final
{
public:
	/**
	 * Dictates how lmdb flushes to disk on commit.
	 * These options only apply to the ledger database; the wallet database
	 * always flush.
	 */
	enum sync_strategy
	{
		/** Always flush to disk on commit. This is default. */
		always,

		/** Do not flush meta data eagerly. This may cause loss of transactions, but maintains integrity. */
		nosync_safe,

		/**
		 * Let the OS decide when to flush to disk. On filesystems with write ordering, this has the same
		 * guarantees as nosync_safe, otherwise corruption may occur on system crash.
		 */
		nosync_unsafe,
		/**
		 * Use a writeable memory map. Let the OS decide when to flush to disk, and make the request asynchronous.
		 * This may give better performance on systems where the database fits entirely in memory, otherwise is
		 * may be slower.
		 * @warning Do not use this option if external processes uses the database concurrently.
		 */
		nosync_unsafe_large_memory
	};

	nano::error serialize_toml (nano::tomlconfig & toml_a) const;
	nano::error deserialize_toml (nano::tomlconfig & toml_a, bool is_deprecated_lmdb_dbs_used);

	/** Sync strategy for the ledger database */
	sync_strategy sync{ always };
	uint32_t max_databases{ 128 };
	size_t map_size{ 256ULL * 1024 * 1024 * 1024 };
};
}
