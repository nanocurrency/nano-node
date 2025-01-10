#pragma once

#include <celerix/lib/epoch.hpp>
#include <celerix/lib/fwd.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/timer.hpp>

namespace celerix
{
/**
 * Latest information about an account
 */
class account_info final
{
public:
	account_info () = default;
	account_info (celerix::block_hash const &, celerix::account const &, celerix::block_hash const &, celerix::amount const &, celerix::seconds_t modified, uint64_t, epoch);
	bool deserialize (celerix::stream &);
	bool operator== (celerix::account_info const &) const;
	bool operator!= (celerix::account_info const &) const;
	size_t db_size () const;
	celerix::epoch epoch () const;
	celerix::block_hash head{ 0 };
	celerix::account representative{};
	celerix::block_hash open_block{ 0 };
	celerix::amount balance{ 0 };
	/** Seconds since posix epoch */
	celerix::seconds_t modified{ 0 };
	uint64_t block_count{ 0 };
	celerix::epoch epoch_m{ celerix::epoch::epoch_0 };
};

/**
 * This is a snapshot of the account_info table at v22 which needs to be read for the v22 to v23 upgrade
 */
class account_info_v22 final
{
public:
	account_info_v22 () = default;
	size_t db_size () const;
	bool deserialize (celerix::stream &);
	celerix::block_hash head{ 0 };
	celerix::account representative{};
	celerix::block_hash open_block{ 0 };
	celerix::amount balance{ 0 };
	/** Seconds since posix epoch */
	celerix::seconds_t modified{ 0 };
	uint64_t block_count{ 0 };
	celerix::epoch epoch_m{ celerix::epoch::epoch_0 };
};
} // namespace celerix
