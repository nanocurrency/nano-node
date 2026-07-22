#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>

#include <memory>
#include <optional>

namespace nano
{
enum class no_value
{
	dummy
};

class unchecked_key final
{
public:
	unchecked_key () = default;
	explicit unchecked_key (nano::hash_or_account const & dependency);
	unchecked_key (nano::hash_or_account const &, nano::block_hash const &);
	unchecked_key (nano::uint512_union const &);
	bool deserialize (nano::stream &);
	bool operator== (nano::unchecked_key const &) const;
	bool operator< (nano::unchecked_key const &) const;
	nano::block_hash const & key () const;
	nano::block_hash previous{ 0 };
	nano::block_hash hash{ 0 };
};

class topo_key final
{
public:
	topo_key () = default;
	topo_key (uint64_t topo_height_a, nano::block_hash const & hash_a) :
		topo_height{ topo_height_a },
		hash{ hash_a }
	{
	}

	auto operator<=> (topo_key const &) const = default;

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);

public:
	uint64_t topo_height{ 0 };
	nano::block_hash hash{ 0 };
};

// Smallest topology key greater than current, or nullopt if current is the maximum value
std::optional<topo_key> next_key (topo_key const & current);

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<nano::block> const &);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	nano::seconds_t modified () const;
	std::shared_ptr<nano::block> block;

private:
	/** Seconds since posix epoch */
	uint64_t modified_m{ 0 };
};

class block_info final
{
public:
	block_info () = default;
	block_info (nano::account const &, nano::amount const &);
	nano::account account{};
	nano::amount balance{ 0 };
};

class confirmation_height_info final
{
public:
	confirmation_height_info () = default;
	confirmation_height_info (uint64_t, nano::block_hash const &);

	auto operator<=> (confirmation_height_info const &) const = default;

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);

	/** height of the cemented frontier */
	uint64_t height{};

	/** hash of the highest cemented block, the cemented/confirmed frontier */
	nano::block_hash frontier{};
};

namespace confirmation_height
{
	/** When the uncemented count (block count - cemented count) is less than this use the unbounded processor */
	uint64_t const unbounded_cutoff{ 16384 };
}

enum class block_status
{
	invalid, // Status is unknown, block is not processed yet (default value)
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	opened_burn_account, // Block attempts to open the burn account
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position, // This block cannot follow the previous block
	insufficient_work // Insufficient work for this block, even though it passed the minimal validation
};

std::string_view to_string (block_status);
nano::stat::detail to_stat_detail (block_status);

enum class tally_result
{
	vote,
	changed,
	confirm
};

nano::wallet_id random_wallet_id ();
}
