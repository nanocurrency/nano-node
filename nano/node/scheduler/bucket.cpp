#include <nano/lib/blocks.hpp>
#include <nano/node/scheduler/bucket.hpp>

nano::block_hash nano::scheduler::bucket::entry_t::hash () const
{
	return block->hash ();
}

nano::scheduler::bucket::bucket (size_t max) :
	max{ max }
{
}

std::shared_ptr<nano::block> nano::scheduler::bucket::insert (std::chrono::steady_clock::time_point time, std::shared_ptr<nano::block> block)
{
	std::lock_guard lock{ mutex };
	backlog.insert (entry_t{ time, block });
	debug_assert (backlog.size () <= max + 1); // One extra at most
	if (backlog.size () <= max)
	{
		return nullptr;
	}
	auto newest = backlog.begin (); // The first item in descending order has the highest timestamp i.e. it is the newest.
	auto discard = newest->block;
	backlog.erase (newest);
	debug_assert (backlog.size () <= max);
	return discard;
}

size_t nano::scheduler::bucket::erase (nano::block_hash const & hash)
{
	std::lock_guard lock{ mutex };
	return backlog.get<tag_hash> ().erase (hash);
}
