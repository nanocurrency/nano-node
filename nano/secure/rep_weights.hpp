#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace nano
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
	explicit rep_weights (nano::store::rep_weight & rep_weight_store_a);
	void representation_add (store::write_transaction const & txn_a, nano::account const & source_rep_a, nano::uint128_t const & amount_a);
	void representation_add_dual (store::write_transaction const & txn_a, nano::account const & source_rep_1, nano::uint128_t const & amount_1, nano::account const & source_rep_2, nano::uint128_t const & amount_2);
	nano::uint128_t representation_get (nano::account const & account_a) const;
	/* Only use this method when loading rep weights from the database table */
	void representation_put (nano::account const & account_a, nano::uint128_t const & representation_a);
	std::unordered_map<nano::account, nano::uint128_t> get_rep_amounts () const;
	void copy_from (rep_weights & other_a);

private:
	mutable nano::mutex mutex;
	std::unordered_map<nano::account, nano::uint128_t> rep_amounts;
	nano::store::rep_weight & rep_weight_store;
	void put (nano::account const & account_a, nano::uint128_union const & representation_a);
	nano::uint128_t get (nano::account const & account_a) const;

	friend std::unique_ptr<container_info_component> collect_container_info (rep_weights const &, std::string const &);
};

std::unique_ptr<container_info_component> collect_container_info (rep_weights const &, std::string const &);
}
