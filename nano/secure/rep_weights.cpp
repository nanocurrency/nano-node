#include <nano/secure/rep_weights.hpp>
#include <nano/store/component.hpp>

void nano::rep_weights::representation_add (nano::account const & source_rep_a, nano::uint128_t const & amount_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	auto source_previous (get (source_rep_a));
	put (source_rep_a, source_previous + amount_a);
}

void nano::rep_weights::representation_add_dual (nano::account const & source_rep_1, nano::uint128_t const & amount_1, nano::account const & source_rep_2, nano::uint128_t const & amount_2)
{
	if (source_rep_1 != source_rep_2)
	{
		nano::lock_guard<nano::mutex> guard (mutex);
		auto source_previous_1 (get (source_rep_1));
		put (source_rep_1, source_previous_1 + amount_1);
		auto source_previous_2 (get (source_rep_2));
		put (source_rep_2, source_previous_2 + amount_2);
	}
	else
	{
		representation_add (source_rep_1, amount_1 + amount_2);
	}
}

void nano::rep_weights::representation_put (nano::account const & account_a, nano::uint128_union const & representation_a)
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
