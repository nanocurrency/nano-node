#include <nano/lib/blocks.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/scheduler/buckets.hpp>

#include <string>

bool nano::scheduler::buckets::value_type::operator< (value_type const & other_a) const
{
	return time < other_a.time || (time == other_a.time && block->hash () < other_a.block->hash ());
}

bool nano::scheduler::buckets::value_type::operator== (value_type const & other_a) const
{
	return time == other_a.time && block->hash () == other_a.block->hash ();
}

/** Moves the bucket pointer to the next bucket */
void nano::scheduler::buckets::next ()
{
	++current;
	if (current == schedule.end ())
	{
		current = schedule.begin ();
	}
}

/** Seek to the next non-empty bucket, if one exists */
void nano::scheduler::buckets::seek ()
{
	next ();
	for (std::size_t i = 0, n = schedule.size (); buckets_m[*current].empty () && i < n; ++i)
	{
		next ();
	}
}

/** Initialise the schedule vector */
void nano::scheduler::buckets::populate_schedule ()
{
	for (auto i = 0; i < buckets_m.size (); ++i)
	{
		schedule.push_back (i);
	}
}

/**
 * Prioritization constructor, construct a container containing approximately 'maximum' number of blocks.
 * @param maximum number of blocks that this container can hold, this is a soft and approximate limit.
 */
nano::scheduler::buckets::buckets (uint64_t maximum) :
	maximum{ maximum }
{
	auto build_region = [this] (uint128_t const & begin, uint128_t const & end, size_t count) {
		auto width = (end - begin) / count;
		for (auto i = 0; i < count; ++i)
		{
			minimums.push_back (begin + i * width);
		}
	};
	minimums.push_back (uint128_t{ 0 });
	build_region (uint128_t{ 1 } << 88, uint128_t{ 1 } << 92, 2);
	build_region (uint128_t{ 1 } << 92, uint128_t{ 1 } << 96, 4);
	build_region (uint128_t{ 1 } << 96, uint128_t{ 1 } << 100, 8);
	build_region (uint128_t{ 1 } << 100, uint128_t{ 1 } << 104, 16);
	build_region (uint128_t{ 1 } << 104, uint128_t{ 1 } << 108, 16);
	build_region (uint128_t{ 1 } << 108, uint128_t{ 1 } << 112, 8);
	build_region (uint128_t{ 1 } << 112, uint128_t{ 1 } << 116, 4);
	build_region (uint128_t{ 1 } << 116, uint128_t{ 1 } << 120, 2);
	minimums.push_back (uint128_t{ 1 } << 120);
	buckets_m.resize (minimums.size ());
	populate_schedule ();
	current = schedule.begin ();
}

std::size_t nano::scheduler::buckets::index (nano::uint128_t const & balance) const
{
	auto index = std::upper_bound (minimums.begin (), minimums.end (), balance) - minimums.begin () - 1;
	return index;
}

/**
 * Push a block and its associated time into the prioritization container.
 * The time is given here because sideband might not exist in the case of state blocks.
 */
void nano::scheduler::buckets::push (uint64_t time, std::shared_ptr<nano::block> block, nano::amount const & priority)
{
	auto was_empty = empty ();
	auto & bucket = buckets_m[index (priority.number ())];
	bucket.emplace (value_type{ time, block });
	if (bucket.size () > std::max (decltype (maximum){ 1 }, maximum / buckets_m.size ()))
	{
		bucket.erase (--bucket.end ());
	}
	if (was_empty)
	{
		seek ();
	}
}

/** Return the highest priority block of the current bucket */
std::shared_ptr<nano::block> nano::scheduler::buckets::top () const
{
	debug_assert (!empty ());
	debug_assert (!buckets_m[*current].empty ());
	auto result = buckets_m[*current].begin ()->block;
	return result;
}

/** Pop the current block from the container and seek to the next block, if it exists */
void nano::scheduler::buckets::pop ()
{
	debug_assert (!empty ());
	debug_assert (!buckets_m[*current].empty ());
	auto & bucket = buckets_m[*current];
	bucket.erase (bucket.begin ());
	seek ();
}

/** Returns the total number of blocks in buckets */
std::size_t nano::scheduler::buckets::size () const
{
	std::size_t result{ 0 };
	for (auto const & queue : buckets_m)
	{
		result += queue.size ();
	}
	return result;
}

/** Returns number of buckets, 62 by default */
std::size_t nano::scheduler::buckets::bucket_count () const
{
	return buckets_m.size ();
}

/** Returns number of items in bucket with index 'index' */
std::size_t nano::scheduler::buckets::bucket_size (std::size_t index) const
{
	return buckets_m[index].size ();
}

/** Returns true if all buckets are empty */
bool nano::scheduler::buckets::empty () const
{
	return std::all_of (buckets_m.begin (), buckets_m.end (), [] (priority const & bucket_a) { return bucket_a.empty (); });
}

/** Print the state of the class in stderr */
void nano::scheduler::buckets::dump () const
{
	for (auto const & i : buckets_m)
	{
		for (auto const & j : i)
		{
			std::cerr << j.time << ' ' << j.block->hash ().to_string () << '\n';
		}
	}
	std::cerr << "current: " << std::to_string (*current) << '\n';
}

std::unique_ptr<nano::container_info_component> nano::scheduler::buckets::collect_container_info (std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	for (auto i = 0; i < buckets_m.size (); ++i)
	{
		auto const & bucket = buckets_m[i];
		composite->add_component (std::make_unique<container_info_leaf> (container_info{ std::to_string (i), bucket.size (), 0 }));
	}
	return composite;
}
