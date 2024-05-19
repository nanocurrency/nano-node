#include <nano/lib/blocks.hpp>
#include <nano/node/scheduler/bucket.hpp>

bool nano::scheduler::bucket::value_type::operator< (value_type const & other_a) const
{
	return time < other_a.time || (time == other_a.time && block->hash () < other_a.block->hash ());
}

bool nano::scheduler::bucket::value_type::operator== (value_type const & other_a) const
{
	return time == other_a.time && block->hash () == other_a.block->hash ();
}

nano::scheduler::bucket::bucket (size_t maximum) :
	maximum{ maximum }
{
	debug_assert (maximum > 0);
}

nano::scheduler::bucket::~bucket ()
{
}

std::shared_ptr<nano::block> nano::scheduler::bucket::top () const
{
	debug_assert (!queue.empty ());
	return queue.begin ()->block;
}

void nano::scheduler::bucket::pop ()
{
	debug_assert (!queue.empty ());
	queue.erase (queue.begin ());
}

void nano::scheduler::bucket::push (uint64_t time, std::shared_ptr<nano::block> block)
{
	// Place this item which can lie anywhere in the range ( queue.begin (), queue.end () ]
	queue.insert ({ time, block });
	if (queue.size () > maximum)
	{
		// Trim lowest priority transaction from block queue
		debug_assert (!queue.empty ());
		// Drop last item which is lowest priority
		queue.erase (--queue.end ());
	}
}

size_t nano::scheduler::bucket::size () const
{
	return queue.size ();
}

bool nano::scheduler::bucket::empty () const
{
	return queue.empty ();
}

void nano::scheduler::bucket::dump () const
{
	for (auto const & item : queue)
	{
		std::cerr << item.time << ' ' << item.block->hash ().to_string () << '\n';
	}
}
