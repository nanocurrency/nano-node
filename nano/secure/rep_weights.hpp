#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/fwd.hpp>

#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace nano
{
class rep_weights
{
public:
	explicit rep_weights (nano::store::rep_weight &, nano::uint128_t min_weight = 0);

	/* Adds or subtracts weight to the representative */
	void add (store::write_transaction const &, nano::account const & rep, nano::uint128_t const & amount_add);
	void sub (store::write_transaction const &, nano::account const & rep, nano::uint128_t const & amount_sub);

	/* Move weight from one representative to another */
	void move (store::write_transaction const &, nano::account const & source_rep, nano::account const & dest_rep, nano::uint128_t const & amount);

	/* Move weight from one representative to another while adding or subtracting the weight */
	void move_add_sub (store::write_transaction const &, nano::account const & source_rep, nano::uint128_t const & amount_source, nano::account const & dest_rep, nano::uint128_t const & amount_dest);

	/* Only use this method when loading rep weights from the database table */
	void put (nano::account const & rep, nano::uint128_t const & weight);
	void put_unused (nano::uint128_t const & weight);
	void append_from (rep_weights const & other);

	nano::uint128_t get (nano::account const & rep) const;
	std::unordered_map<nano::account, nano::uint128_t> get_rep_amounts () const;

	size_t size () const;
	nano::container_info container_info () const;
	bool empty () const;

	nano::uint128_t get_weight_committed () const;
	nano::uint128_t get_weight_unused () const;

	void verify_consistency (nano::uint128_t burn_balance) const;

private:
	nano::store::rep_weight & rep_weight_store;
	nano::uint128_t const min_weight;

	mutable std::shared_mutex mutex;
	std::unordered_map<nano::account, nano::uint128_t> rep_amounts;

	// Used for consistency checking, use higher precision types to detect overflows
	nano::uint256_t weight_committed{ 0 };
	nano::uint256_t weight_unused{ 0 };

private:
	void put_cache (nano::account const & rep, nano::uint128_union const & weight);
	void put_store (store::write_transaction const &, nano::account const & rep, nano::uint128_t const & previous_weight, nano::uint128_t const & new_weight);
	nano::uint128_t get_impl (nano::account const & rep) const;
};
}
