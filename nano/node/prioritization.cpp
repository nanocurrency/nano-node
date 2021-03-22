#include <nano/node/prioritization.hpp>

#include <nano/lib/utility.hpp>

void nano::prioritization::trim ()
{
}

void nano::prioritization::next ()
{
	++current;
	if (current == buckets.end ())
	{
		current = buckets.begin ();
	}
}

nano::prioritization::prioritization (std::function<void (nano::block_hash const &)> const & drop_a) :
	drop{ drop_a },
	current{ buckets.begin () }
{
	(void)minimums[0];
	nano::uint128_t current{ 1 };
	for (auto i = minimums.begin (), n = minimums.end (); i != n; ++i)
	{
		*i = current;
		current <<= 1;
	}
}

void nano::prioritization::insert (uint32_t time, nano::amount const & balance_a, nano::account const & account_a)
{
	assert (!balance_a.is_zero ());
	auto count = 0;
	auto bucket = std::upper_bound (minimums.begin (), minimums.end (), balance_a.number ());
	debug_assert (bucket != minimums.begin ());
	buckets[bucket - 1 - minimums.begin ()].emplace (value_type{ time, account_a });
	trim ();
}

size_t nano::prioritization::size () const
{
	size_t result{ 0 };
	for (auto const & queue: buckets)
	{
		result += queue.size ();
	}
	return result;
}
