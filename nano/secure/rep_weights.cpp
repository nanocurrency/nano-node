#include <nano/lib/numbers.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/store/component.hpp>
#include <nano/store/rep_weight.hpp>

nano::rep_weights::rep_weights (nano::store::rep_weight & rep_weight_store_a, nano::uint128_t min_weight_a) :
	rep_weight_store{ rep_weight_store_a },
	min_weight{ min_weight_a }
{
}

void nano::rep_weights::representation_add (store::write_transaction const & txn_a, nano::account const & rep_a, nano::uint128_t const & amount_a)
{
	auto previous_weight{ rep_weight_store.get (txn_a, rep_a) };
	auto new_weight = previous_weight + amount_a;
	put_store (txn_a, rep_a, previous_weight, new_weight);
	std::unique_lock guard{ mutex };
	put_cache (rep_a, new_weight);
}

void nano::rep_weights::representation_add_dual (store::write_transaction const & txn_a, nano::account const & rep_1, nano::uint128_t const & amount_1, nano::account const & rep_2, nano::uint128_t const & amount_2)
{
	if (rep_1 != rep_2)
	{
		auto previous_weight_1{ rep_weight_store.get (txn_a, rep_1) };
		auto previous_weight_2{ rep_weight_store.get (txn_a, rep_2) };
		auto new_weight_1 = previous_weight_1 + amount_1;
		auto new_weight_2 = previous_weight_2 + amount_2;
		put_store (txn_a, rep_1, previous_weight_1, new_weight_1);
		put_store (txn_a, rep_2, previous_weight_2, new_weight_2);
		std::unique_lock guard{ mutex };
		put_cache (rep_1, new_weight_1);
		put_cache (rep_2, new_weight_2);
	}
	else
	{
		representation_add (txn_a, rep_1, amount_1 + amount_2);
	}
}

void nano::rep_weights::representation_put (nano::account const & account_a, nano::uint128_t const & representation_a)
{
	std::unique_lock guard{ mutex };
	put_cache (account_a, representation_a);
}

nano::uint128_t nano::rep_weights::representation_get (nano::account const & account_a) const
{
	std::shared_lock lk{ mutex };
	return get (account_a);
}

/** Makes a copy */
std::unordered_map<nano::account, nano::uint128_t> nano::rep_weights::get_rep_amounts () const
{
	std::shared_lock guard{ mutex };
	return rep_amounts;
}

void nano::rep_weights::copy_from (nano::rep_weights & other_a)
{
	std::unique_lock guard_this{ mutex };
	std::shared_lock guard_other{ other_a.mutex };
	for (auto const & entry : other_a.rep_amounts)
	{
		auto prev_amount (get (entry.first));
		put_cache (entry.first, prev_amount + entry.second);
	}
}

void nano::rep_weights::put_cache (nano::account const & account_a, nano::uint128_union const & representation_a)
{
	auto it = rep_amounts.find (account_a);
	if (representation_a < min_weight || representation_a.is_zero ())
	{
		if (it != rep_amounts.end ())
		{
			rep_amounts.erase (it);
		}
	}
	else
	{
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
}

void nano::rep_weights::put_store (store::write_transaction const & txn_a, nano::account const & rep_a, nano::uint128_t const & previous_weight_a, nano::uint128_t const & new_weight_a)
{
	if (new_weight_a.is_zero ())
	{
		if (!previous_weight_a.is_zero ())
		{
			rep_weight_store.del (txn_a, rep_a);
		}
	}
	else
	{
		rep_weight_store.put (txn_a, rep_a, new_weight_a);
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

std::size_t nano::rep_weights::size () const
{
	std::shared_lock guard{ mutex };
	return rep_amounts.size ();
}

nano::container_info nano::rep_weights::container_info () const
{
	std::shared_lock guard{ mutex };

	nano::container_info info;
	info.put ("rep_amounts", rep_amounts);
	return info;
}
