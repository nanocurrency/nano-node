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
	for (std::size_t i = 0, n = buckets_m.size (); (*current)->empty () && i < n; ++i)
	{
		next ();
	}
}

void nano::scheduler::buckets::setup_buckets (uint64_t maximum)
{
	auto const size_expected = 62;
	auto bucket_max = std::max<size_t> (1u, maximum / size_expected);
	auto build_region = [&] (uint128_t const & begin, uint128_t const & end, size_t count) {
		auto width = (end - begin) / count;
		for (auto i = 0; i < count; ++i)
		{
			buckets_m.push_back (std::make_unique<scheduler::bucket> (begin + i * width, bucket_max));
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

	debug_assert (buckets_m.size () == size_expected);
}

/**
 * Prioritization constructor, construct a container containing approximately 'maximum' number of blocks.
 * @param maximum number of blocks that this container can hold, this is a soft and approximate limit.
 */
nano::scheduler::buckets::buckets (uint64_t maximum) :
	maximum{ maximum }
{
	setup_buckets (maximum);
	current = buckets_m.begin ();
}

nano::scheduler::buckets::~buckets ()
{
}

/**
 * Push a block and its associated time into the prioritization container.
 * The time is given here because sideband might not exist in the case of state blocks.
 */
void nano::scheduler::buckets::push (uint64_t time, std::shared_ptr<nano::block> block, nano::amount const & priority)
{
	auto was_empty = empty ();
	auto & bucket = find_bucket (priority.number ());
	bucket.push (time, block);
	if (was_empty)
	{
		seek ();
	}
}

/** Return the highest priority block of the current bucket */
std::shared_ptr<nano::block> nano::scheduler::buckets::top () const
{
	debug_assert (!empty ());
	auto result = (*current)->top ();
	return result;
}

/** Pop the current block from the container and seek to the next block, if it exists */
void nano::scheduler::buckets::pop ()
{
	debug_assert (!empty ());
	auto & bucket = *current;
	bucket->pop ();
	seek ();
}

/** Returns the total number of blocks in buckets */
std::size_t nano::scheduler::buckets::size () const
{
	std::size_t result{ 0 };
	for (auto const & bucket : buckets_m)
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
std::size_t nano::scheduler::buckets::bucket_size (std::size_t index) const
{
	return buckets_m[index]->size ();
}

/** Returns true if all buckets are empty */
bool nano::scheduler::buckets::empty () const
{
	return std::all_of (buckets_m.begin (), buckets_m.end (), [] (auto const & bucket) { return bucket->empty (); });
}

/** Print the state of the class in stderr */
void nano::scheduler::buckets::dump () const
{
	for (auto const & bucket : buckets_m)
	{
		bucket->dump ();
	}
	std::cerr << "current: " << current - buckets_m.begin () << '\n';
}

auto nano::scheduler::buckets::find_bucket (nano::uint128_t priority) -> bucket &
{
	auto it = std::upper_bound (buckets_m.begin (), buckets_m.end (), priority, [] (nano::uint128_t const & priority, std::unique_ptr<bucket> const & bucket) {
		return priority < bucket->minimum_balance;
	});
	release_assert (it != buckets_m.begin ()); // There should always be a bucket with a minimum_balance of 0
	return **std::prev (it);
}

std::unique_ptr<nano::container_info_component> nano::scheduler::buckets::collect_container_info (std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	for (auto i = 0; i < buckets_m.size (); ++i)
	{
		auto const & bucket = buckets_m[i];
		composite->add_component (std::make_unique<container_info_leaf> (container_info{ std::to_string (i), bucket->size (), 0 }));
	}
	return composite;
}
