#include <nano/secure/rep_weights.hpp>
#include <nano/store/component.hpp>
#include <nano/store/rep_weight.hpp>

nano::rep_weights::rep_weights (nano::store::rep_weight & rep_weight_store_a) :
	rep_weight_store{ rep_weight_store_a }
{
}

void nano::rep_weights::representation_add (store::write_transaction const & txn_a, nano::account const & source_rep_a, nano::uint128_t const & amount_a)
{
	auto weight{ rep_weight_store.get (txn_a, source_rep_a) };
	weight += amount_a;
	nano::lock_guard<nano::mutex> guard (mutex);
	rep_weight_store.put (txn_a, source_rep_a, weight);
	put (source_rep_a, weight);
}

void nano::rep_weights::representation_add_dual (store::write_transaction const & txn_a, nano::account const & source_rep_1, nano::uint128_t const & amount_1, nano::account const & source_rep_2, nano::uint128_t const & amount_2)
{
	if (source_rep_1 != source_rep_2)
	{
		auto rep_1_weight{ rep_weight_store.get (txn_a, source_rep_1) };
		auto rep_2_weight{ rep_weight_store.get (txn_a, source_rep_2) };
		rep_1_weight += amount_1;
		rep_2_weight += amount_2;
		rep_weight_store.put (txn_a, source_rep_1, rep_1_weight);
		rep_weight_store.put (txn_a, source_rep_2, rep_2_weight);
		nano::lock_guard<nano::mutex> guard (mutex);
		put (source_rep_1, rep_1_weight);
		put (source_rep_2, rep_2_weight);
	}
	else
	{
		representation_add (txn_a, source_rep_1, amount_1 + amount_2);
	}
}

void nano::rep_weights::representation_put (nano::account const & account_a, nano::uint128_t const & representation_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	put (account_a, representation_a);
}

nano::uint128_t nano::rep_weights::representation_get (nano::account const & account_a) const
{
	nano::lock_guard<nano::mutex> lk (mutex);
	return get (account_a);
}

/** Makes a copy */
std::unordered_map<nano::account, nano::uint128_t> nano::rep_weights::get_rep_amounts () const
{
	nano::lock_guard<nano::mutex> guard (mutex);
	return rep_amounts;
}

void nano::rep_weights::copy_from (nano::rep_weights & other_a)
{
	nano::lock_guard<nano::mutex> guard_this (mutex);
	nano::lock_guard<nano::mutex> guard_other (other_a.mutex);
	for (auto const & entry : other_a.rep_amounts)
	{
		auto prev_amount (get (entry.first));
		put (entry.first, prev_amount + entry.second);
	}
}

void nano::rep_weights::put (nano::account const & account_a, nano::uint128_union const & representation_a)
{
	auto it = rep_amounts.find (account_a);
	auto amount = representation_a.number ();
	if (it != rep_amounts.end ())
	{
		it->second = amount;
	}
	else
	{
		rep_amounts.emplace (account_a, amount);
	}
}

nano::uint128_t nano::rep_weights::get (nano::account const & account_a) const
{
	auto it = rep_amounts.find (account_a);
	if (it != rep_amounts.end ())
	{
		return it->second;
	}
	else
	{
		return nano::uint128_t{ 0 };
	}
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (nano::rep_weights const & rep_weights, std::string const & name)
{
	size_t rep_amounts_count;

	{
		nano::lock_guard<nano::mutex> guard (rep_weights.mutex);
		rep_amounts_count = rep_weights.rep_amounts.size ();
	}
	auto sizeof_element = sizeof (decltype (rep_weights.rep_amounts)::value_type);
	auto composite = std::make_unique<nano::container_info_composite> (name);
	composite->add_component (std::make_unique<nano::container_info_leaf> (container_info{ "rep_amounts", rep_amounts_count, sizeof_element }));
	return composite;
}
