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
	static size_t constexpr bucket_count = 129;
	buckets.resize (bucket_count);
	(void)minimums[0];
	nano::uint128_t minimum{ 1 };
	minimums.push_back (0);
	for (auto i = 1; i < bucket_count; ++i)
	{
		minimums.push_back (minimum);
		minimum <<= 1;
	}
	populate_schedule ();
	current = schedule.begin ();
}

void nano::prioritization::insert (uint32_t time, nano::amount const & balance_a, nano::account const & account_a)
{
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

size_t nano::prioritization::bucket_count () const
{
	return buckets.size ();
}

size_t nano::prioritization::bucket_size (size_t index) const
{
	return buckets[index].size ();
}
