#include <nano/node/prioritization.hpp>

#include <nano/lib/utility.hpp>

#include <random>

void nano::prioritization::trim ()
{
}

void nano::prioritization::next ()
{
	++current;
	if (current == schedule.end ())
	{
		current = schedule.begin ();
	}
}

void nano::prioritization::populate_schedule ()
{
	for (auto i = 0; i < buckets.size (); ++i)
	{
		schedule.push_back (i);
	}
	std::random_device rd;
	std::mt19937 g{ rd() };
	std::shuffle (schedule.begin (), schedule.end (), g);
}

nano::prioritization::prioritization (std::function<void (nano::block_hash const &)> const & drop_a) :
	drop{ drop_a }
{
	(void)minimums[0];
	nano::uint128_t minimum{ 1 };
	for (auto i = minimums.begin (), n = minimums.end (); i != n; ++i)
	{
		*i = minimum;
		minimum <<= 1;
	}
	populate_schedule ();
	current = schedule.begin ();
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
