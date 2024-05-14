#include <nano/lib/blocks.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/scheduler/bucket.hpp>
#include <nano/node/scheduler/buckets.hpp>

#include <string>

/** Moves the bucket pointer to the next bucket */
void nano::scheduler::buckets::next ()
{
	++current;
	if (current == buckets_m.end ())
	{
		current = buckets_m.begin ();
	}
}

/** Seek to the next non-empty bucket, if one exists */
void nano::scheduler::buckets::seek ()
{
	next ();
	for (std::size_t i = 0, n = buckets_m.size (); current->second->empty () && i < n; ++i)
	{
		next ();
	}
}

void nano::scheduler::buckets::setup_buckets (uint64_t maximum)
{
	auto build_region = [&] (uint128_t const & begin, uint128_t const & end, size_t count) {
		auto width = (end - begin) / count;
		for (auto i = 0; i < count; ++i)
		{
			buckets_m.emplace (begin + i * width, std::make_unique<nano::scheduler::bucket> (maximum));
		}
	};
	build_region (0, uint128_t{ 1 } << 88, 1);
	build_region (uint128_t{ 1 } << 88, uint128_t{ 1 } << 92, 2);
	build_region (uint128_t{ 1 } << 92, uint128_t{ 1 } << 96, 4);
	build_region (uint128_t{ 1 } << 96, uint128_t{ 1 } << 100, 8);
	build_region (uint128_t{ 1 } << 100, uint128_t{ 1 } << 104, 16);
	build_region (uint128_t{ 1 } << 104, uint128_t{ 1 } << 108, 16);
	build_region (uint128_t{ 1 } << 108, uint128_t{ 1 } << 112, 8);
	build_region (uint128_t{ 1 } << 112, uint128_t{ 1 } << 116, 4);
	build_region (uint128_t{ 1 } << 116, uint128_t{ 1 } << 120, 2);
	build_region (uint128_t{ 1 } << 120, uint128_t{ 1 } << 127, 1);
}

/**
 * Prioritization constructor, construct a container containing approximately 'maximum' number of blocks.
 * @param maximum number of blocks that this container can hold, this is a soft and approximate limit.
 */
nano::scheduler::buckets::buckets (uint64_t maximum)
{
	setup_buckets (maximum);
	current = buckets_m.begin ();
}

nano::scheduler::buckets::~buckets ()
{
}

auto nano::scheduler::buckets::bucket (nano::uint128_t const & balance) const -> scheduler::bucket &
{
	auto iter = buckets_m.upper_bound (balance);
	--iter; // Iterator points to bucket after the target priority
	debug_assert (iter != buckets_m.end ());
	return *iter->second;
}

/**
 * Push a block and its associated time into the prioritization container.
 * The time is given here because sideband might not exist in the case of state blocks.
 */
void nano::scheduler::buckets::push (uint64_t time, std::shared_ptr<nano::block> block, nano::amount const & priority)
{
	auto was_empty = empty ();
	bucket (priority.number ()).push (time, block);
	if (was_empty)
	{
		seek ();
	}
}

/** Return the highest priority block of the current bucket */
std::shared_ptr<nano::block> nano::scheduler::buckets::top () const
{
	debug_assert (!empty ());
	auto result = current->second->top ();
	return result;
}

/** Pop the current block from the container and seek to the next block, if it exists */
void nano::scheduler::buckets::pop ()
{
	debug_assert (!empty ());
	current->second->pop ();
	seek ();
}

/** Returns the total number of blocks in buckets */
std::size_t nano::scheduler::buckets::size () const
{
	std::size_t result{ 0 };
	for (auto const & [_, bucket] : buckets_m)
	{
		result += bucket->size ();
	}
	return result;
}

/** Returns number of buckets, 62 by default */
std::size_t nano::scheduler::buckets::bucket_count () const
{
	return buckets_m.size ();
}

/** Returns number of items in bucket with index 'index' */
std::size_t nano::scheduler::buckets::bucket_size (nano::amount const & amount) const
{
	return bucket (amount.number ()).size ();
}

/** Returns true if all buckets are empty */
bool nano::scheduler::buckets::empty () const
{
	return std::all_of (buckets_m.begin (), buckets_m.end (), [] (auto const & item) { return item.second->empty (); });
}

/** Print the state of the class in stderr */
void nano::scheduler::buckets::dump () const
{
	for (auto const & [_, bucket] : buckets_m)
	{
		bucket->dump ();
	}
}

std::unique_ptr<nano::container_info_component> nano::scheduler::buckets::collect_container_info (std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	size_t count = 0;
	for (auto const & [_, bucket] : buckets_m)
	{
		composite->add_component (std::make_unique<container_info_leaf> (container_info{ std::to_string (count++), bucket->size (), 0 }));
	}
	return composite;
}
