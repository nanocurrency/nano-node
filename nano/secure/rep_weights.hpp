#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace nano::secure
{
class transaction;
class write_transaction;
}
namespace nano::store
{
class component;
class rep_weight;
}

namespace nano::store::rocksdb
{
class unconfirmed_rep_weight;
}

namespace nano
{
class rep_weights
{
public:
	explicit rep_weights (nano::store::rep_weight & confirmed, nano::store::rocksdb::unconfirmed_rep_weight & unconfirmed, nano::uint128_t min_weight_a = 0);
	void representation_add (secure::write_transaction const & txn_a, nano::account const & source_rep_a, nano::uint128_t const & amount_a);
	void representation_add_dual (secure::write_transaction const & txn_a, nano::account const & source_rep_1, nano::uint128_t const & amount_1, nano::account const & source_rep_2, nano::uint128_t const & amount_2);
	nano::uint128_t representation_get (nano::account const & account_a) const;
	/* Only use this method when loading rep weights from the database table */
	void representation_put (nano::account const & account_a, nano::uint128_t const & representation_a);
	std::unordered_map<nano::account, nano::uint128_t> get_rep_amounts () const;
	/* Only use this method when loading rep weights from the database table */
	void copy_from (rep_weights & other_a);
	size_t size () const;
	std::unique_ptr<container_info_component> collect_container_info (std::string const &) const;

private:
	mutable std::shared_mutex mutex;
	std::unordered_map<nano::account, nano::uint128_t> rep_amounts;
	nano::store::rep_weight & confirmed;
	nano::store::rocksdb::unconfirmed_rep_weight & unconfirmed;
	nano::uint128_t min_weight;
	void put_cache (nano::account const & account_a, nano::uint128_union const & representation_a);
	void put_store (secure::write_transaction const & txn_a, nano::account const & rep_a, nano::uint128_t const & previous_weight_a, nano::uint128_t const & new_weight_a);
	nano::uint128_t get_cache (nano::account const & account_a) const;
	nano::uint128_t get_store (secure::transaction const & tx, nano::account const & account_a) const;
};
}
