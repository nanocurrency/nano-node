#include <nano/lib/rep_weights.hpp>
#include <nano/secure/blockstore.hpp>

void nano::rep_weights::representation_add (nano::account const & source_rep, nano::uint128_t const & amount_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	auto source_previous (get (source_rep));
	put (source_rep, source_previous + amount_a);
}

void nano::rep_weights::representation_put (nano::account const & account_a, nano::uint128_union const & representation_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	put (account_a, representation_a);
}

nano::uint128_t nano::rep_weights::representation_get (nano::account const & account_a)
{
	nano::lock_guard<std::mutex> lk (mutex);
	return get (account_a);
}

/** Makes a copy */
std::unordered_map<nano::account, nano::uint128_t> nano::rep_weights::get_rep_amounts ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return rep_amounts;
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

nano::uint128_t nano::rep_weights::get (nano::account const & account_a)
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

std::unique_ptr<nano::container_info_component> nano::collect_container_info (nano::rep_weights & rep_weights, const std::string & name)
{
	size_t rep_amounts_count;

	{
		nano::lock_guard<std::mutex> guard (rep_weights.mutex);
		rep_amounts_count = rep_weights.rep_amounts.size ();
	}
	auto sizeof_element = sizeof (decltype (rep_weights.rep_amounts)::value_type);
	auto composite = std::make_unique<nano::container_info_composite> (name);
	composite->add_component (std::make_unique<nano::container_info_leaf> (container_info{ "rep_amounts", rep_amounts_count, sizeof_element }));
	return composite;
}
