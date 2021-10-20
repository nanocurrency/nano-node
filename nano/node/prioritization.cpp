#include <nano/lib/blocks.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/prioritization.hpp>

#include <string>

bool nano::prioritization::value_type::operator< (value_type const & other_a) const
{
	return time < other_a.time || (time == other_a.time && block->hash () < other_a.block->hash ());
}

bool nano::prioritization::value_type::operator== (value_type const & other_a) const
{
	return time == other_a.time && block->hash () == other_a.block->hash ();
}

void nano::prioritization::next ()
{
	++current;
	if (current == schedule.end ())
	{
		current = schedule.begin ();
	}
}

void nano::prioritization::seek ()
{
	next ();
	for (std::size_t i = 0, n = schedule.size (); buckets[*current].empty () && i < n; ++i)
	{
		next ();
	}
}

void nano::prioritization::populate_schedule ()
{
	for (auto i = 0; i < buckets.size (); ++i)
	{
		schedule.push_back (i);
	}
}

nano::prioritization::prioritization (uint64_t maximum, std::function<void (std::shared_ptr<nano::block>)> const & drop_a) :
	drop{ drop_a },
	maximum{ maximum }
{
	static std::size_t constexpr bucket_count = 129;
	buckets.resize (bucket_count);
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

void nano::prioritization::push (uint64_t time, std::shared_ptr<nano::block> block)
{
	auto was_empty = empty ();
	auto block_has_balance = block->type () == nano::block_type::state || block->type () == nano::block_type::send;
	debug_assert (block_has_balance || block->has_sideband ());
	auto balance = block_has_balance ? block->balance () : block->sideband ().balance;
	auto index = std::upper_bound (minimums.begin (), minimums.end (), balance.number ()) - 1 - minimums.begin ();
	auto & bucket = buckets[index];
	bucket.emplace (value_type{ time, block });
	if (bucket.size () > std::max (decltype (maximum){ 1 }, maximum / buckets.size ()))
	{
		bucket.erase (--bucket.end ());
	}
	if (was_empty)
	{
		seek ();
	}
}

std::shared_ptr<nano::block> nano::prioritization::top () const
{
	debug_assert (!empty ());
	debug_assert (!buckets[*current].empty ());
	auto result = buckets[*current].begin ()->block;
	return result;
}

void nano::prioritization::pop ()
{
	debug_assert (!empty ());
	debug_assert (!buckets[*current].empty ());
	auto & bucket = buckets[*current];
	bucket.erase (bucket.begin ());
	seek ();
}

std::size_t nano::prioritization::size () const
{
	std::size_t result{ 0 };
	for (auto const & queue : buckets)
	{
		result += queue.size ();
	}
	return result;
}

std::size_t nano::prioritization::bucket_count () const
{
	return buckets.size ();
}

std::size_t nano::prioritization::bucket_size (std::size_t index) const
{
	return buckets[index].size ();
}

bool nano::prioritization::empty () const
{
	return std::all_of (buckets.begin (), buckets.end (), [] (priority const & bucket_a) { return bucket_a.empty (); });
}

void nano::prioritization::dump ()
{
	for (auto const & i : buckets)
	{
		for (auto const & j : i)
		{
			std::cerr << j.time << ' ' << j.block->hash ().to_string () << '\n';
		}
	}
	std::cerr << "current: " << std::to_string (*current) << '\n';
}
