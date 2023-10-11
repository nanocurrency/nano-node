#include <nano/lib/blocks.hpp>
#include <nano/node/scheduler/bucket.hpp>
#include <nano/node/scheduler/limiter.hpp>

bool nano::scheduler::bucket::value_type::operator< (value_type const & other_a) const
{
	return time < other_a.time || (time == other_a.time && block->hash () < other_a.block->hash ());
}

bool nano::scheduler::bucket::value_type::operator== (value_type const & other_a) const
{
	return time == other_a.time && block->hash () == other_a.block->hash ();
}

nano::scheduler::bucket::bucket (std::shared_ptr<nano::scheduler::limiter> limiter, size_t maximum) :
	maximum{ maximum },
	limiter{ limiter }
{
	debug_assert (maximum > 0);
	debug_assert (limiter != nullptr);
}

nano::scheduler::bucket::~bucket ()
{
}

std::pair<std::shared_ptr<nano::block>, std::shared_ptr<nano::scheduler::limiter>> nano::scheduler::bucket::top () const
{
	debug_assert (!queue.empty ());
	auto & first = *queue.begin ();
	return { first.block, limiter };
}

void nano::scheduler::bucket::pop ()
{
	debug_assert (!queue.empty ());
	queue.erase (queue.begin ());
}

void nano::scheduler::bucket::push (uint64_t time, std::shared_ptr<nano::block> block)
{
	queue.insert ({ time, block });
	if (queue.size () > maximum)
	{
		debug_assert (!queue.empty ());
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

bool nano::scheduler::bucket::available () const
{
	return !queue.empty () && limiter->available ();
}

void nano::scheduler::bucket::dump () const
{
	for (auto const & item : queue)
	{
		std::cerr << item.time << ' ' << item.block->hash ().to_string () << '\n';
	}
}
