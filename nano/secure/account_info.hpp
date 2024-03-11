#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/timer.hpp>

namespace nano
{
/**
 * Latest information about an account
 */
class account_info final
{
public:
	account_info () = default;
	account_info (nano::block_hash const &, nano::account const &, nano::block_hash const &, nano::amount const &, nano::seconds_t modified, uint64_t, epoch);
	bool deserialize (nano::stream &);
	bool operator== (nano::account_info const &) const;
	bool operator!= (nano::account_info const &) const;
	size_t db_size () const;
	nano::epoch epoch () const;
	nano::block_hash head{ 0 };
	nano::account representative{};
	nano::block_hash open_block{ 0 };
	nano::amount balance{ 0 };
	/** Seconds since posix epoch */
	nano::seconds_t modified{ 0 };
	uint64_t block_count{ 0 };
	nano::epoch epoch_m{ nano::epoch::epoch_0 };
};

/**
 * Account info as of DB version 22.
 * This class protects the DB upgrades from future changes of the account_info class.
 */
class account_info_v22 final
{
public:
	account_info_v22 () = default;
	size_t db_size () const;
	bool deserialize (nano::stream &);
	nano::block_hash head{ 0 };
	nano::account representative{};
	nano::block_hash open_block{ 0 };
	nano::amount balance{ 0 };
	/** Seconds since posix epoch */
	nano::seconds_t modified{ 0 };
	uint64_t block_count{ 0 };
	nano::epoch epoch_m{ nano::epoch::epoch_0 };
};
} // namespace nano
