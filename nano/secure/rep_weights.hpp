#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/lib/utility.hpp>

#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace celerix
{
namespace store
{
	class component;
	class rep_weight;
	class write_transaction;
}

class rep_weights
{
public:
	explicit rep_weights (celerix::store::rep_weight & rep_weight_store_a, celerix::uint128_t min_weight_a = 0);
	void representation_add (store::write_transaction const & txn_a, celerix::account const & source_rep_a, celerix::uint128_t const & amount_a);
	void representation_add_dual (store::write_transaction const & txn_a, celerix::account const & source_rep_1, celerix::uint128_t const & amount_1, celerix::account const & source_rep_2, celerix::uint128_t const & amount_2);
	celerix::uint128_t representation_get (celerix::account const & account_a) const;
	/* Only use this method when loading rep weights from the database table */
	void representation_put (celerix::account const & account_a, celerix::uint128_t const & representation_a);
	std::unordered_map<celerix::account, celerix::uint128_t> get_rep_amounts () const;
	/* Only use this method when loading rep weights from the database table */
	void copy_from (rep_weights & other_a);
	size_t size () const;
	celerix::container_info container_info () const;

private:
	mutable std::shared_mutex mutex;
	std::unordered_map<celerix::account, celerix::uint128_t> rep_amounts;
	celerix::store::rep_weight & rep_weight_store;
	celerix::uint128_t min_weight;
	void put_cache (celerix::account const & account_a, celerix::uint128_union const & representation_a);
	void put_store (store::write_transaction const & txn_a, celerix::account const & rep_a, celerix::uint128_t const & previous_weight_a, celerix::uint128_t const & new_weight_a);
	celerix::uint128_t get (celerix::account const & account_a) const;
};
}
