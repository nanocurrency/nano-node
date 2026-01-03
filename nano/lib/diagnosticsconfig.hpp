#pragma once

#include <nano/lib/errors.hpp>

#include <chrono>

namespace nano
{
class jsonconfig;
class tomlconfig;
class txn_tracking_config final
{
public:
	/** If true, enable tracking for transaction read/writes held open longer than the min time variables */
	bool enable{ false };
	std::chrono::milliseconds min_read_txn_time{ 5000 };
	std::chrono::milliseconds min_write_txn_time{ 500 };
	bool ignore_writes_below_block_processor_max_time{ true };
	/** Time threshold for filtering block processor writes (used when ignore_writes_below_block_processor_max_time is true) */
	std::chrono::milliseconds block_processor_batch_max_time{ 5000 };
};

/** Configuration options for diagnostics information */
class diagnostics_config final
{
public:
	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);

	txn_tracking_config txn_tracking;
};
}
