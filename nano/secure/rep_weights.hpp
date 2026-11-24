#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/fwd.hpp>

#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <shared_mutex>
#include <span>

namespace nano
{
// Vote weight per representative
// A flat hash table keeps every entry in one contiguous array, so a copy of the whole map is a single allocation and a lookup stays within that array
using rep_weight_map = boost::unordered_flat_map<nano::account, nano::uint128_t>;

class rep_weights
{
public:
	explicit rep_weights (nano::store::ledger::rep_weight_view &, nano::uint128_t min_weight = 0);

	/* Adds or subtracts weight to the representative */
	void add (store::write_transaction const &, nano::account const & rep, nano::uint128_t const & amount_add);
	void sub (store::write_transaction const &, nano::account const & rep, nano::uint128_t const & amount_sub);

	/* Move weight from one representative to another */
	void move (store::write_transaction const &, nano::account const & source_rep, nano::account const & dest_rep, nano::uint128_t const & amount);

	/* Take amount_source from the source rep and give amount_dest to the destination rep in one cache update, as a block changing representative and balance at once requires */
	void move_add_sub (store::write_transaction const &, nano::account const & source_rep, nano::uint128_t const & amount_source, nano::account const & dest_rep, nano::uint128_t const & amount_dest);

	/* Only use this method when loading rep weights from the database table */
	void put (nano::account const & rep, nano::uint128_t const & weight);
	void put_unused (nano::uint128_t const & weight);
	void append_from (rep_weights const & other);

	/* Cached weight of the representative, zero when it has no cached weight (unknown, below the cache minimum, or the zero account) */
	nano::uint128_t get (nano::account const & rep) const;

	/* Weights of all given representatives read under a single lock, so the result is one consistent snapshot; every requested rep has an entry, zero when it has no cached weight */
	nano::rep_weight_map get (std::span<nano::account const> reps) const;

	/* Same snapshot without a map: writes the weight of `reps[i]` to `weights[i]`, which allocates nothing */
	void get (std::span<nano::account const> reps, std::span<nano::uint128_t> weights) const;

	/* Copy of the whole cache read under a single lock: every representative with a cached weight, i.e. at or above the cache minimum */
	nano::rep_weight_map get_all () const;

	size_t size () const;
	nano::container_info container_info () const;
	bool empty () const;

	/* Weight delegated to representatives, the sum of all account balances, including representatives below the cache minimum */
	nano::uint128_t get_weight_committed () const;
	/* Weight delegated to no one, the sum of all receivable amounts, including sends to the burn account */
	nano::uint128_t get_weight_unused () const;

	/* Release asserts that committed and unused weight add up to the whole supply, that `burn_balance` is counted as unused and that the cached weights do not exceed the committed weight */
	void verify_consistency (nano::uint128_t burn_balance) const;

private:
	nano::store::ledger::rep_weight_view & rep_weight_store;
	nano::uint128_t const min_weight;

	mutable std::shared_mutex mutex;

	// Weight of every representative at or above the cache minimum
	nano::rep_weight_map rep_amounts;

	// Used for consistency checking, use higher precision types to detect overflows
	nano::uint256_t weight_committed{ 0 };
	nano::uint256_t weight_unused{ 0 };

private:
	void put_cache (nano::account const & rep, nano::uint128_union const & weight);
	void put_store (store::write_transaction const &, nano::account const & rep, nano::uint128_t const & previous_weight, nano::uint128_t const & new_weight);
	nano::uint128_t get_impl (nano::account const & rep) const;
};
}
