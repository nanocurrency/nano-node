#include <nano/lib/config.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/write_queue.hpp>

#include <algorithm>

/*
 * write_guard
 */

nano::store::write_guard::write_guard (write_queue & queue, writer type) :
	queue{ queue },
	type{ type }
{
	renew ();
}

nano::store::write_guard::write_guard (write_guard && other) noexcept :
	queue{ other.queue },
	type{ other.type },
	owns{ other.owns }
{
	other.owns = false;
}

nano::store::write_guard::~write_guard ()
{
	if (owns)
	{
		release ();
	}
}

bool nano::store::write_guard::is_owned () const
{
	return owns;
}

void nano::store::write_guard::release ()
{
	release_assert (owns);
	queue.release (type);
	owns = false;
}

void nano::store::write_guard::renew ()
{
	release_assert (!owns);
	queue.acquire (type);
	owns = true;
}

/*
 * write_queue
 */

nano::store::write_queue::write_queue (bool use_noops_a) :
	use_noops{ use_noops_a }
{
}

nano::store::write_guard nano::store::write_queue::wait (writer writer)
{
	return write_guard{ *this, writer };
}

bool nano::store::write_queue::contains (writer writer) const
{
	debug_assert (!use_noops);
	nano::lock_guard<nano::mutex> guard{ mutex };
	return std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
}

void nano::store::write_queue::pop ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (!queue.empty ())
	{
		queue.pop_front ();
	}
}

void nano::store::write_queue::acquire (writer writer)
{
	if (use_noops)
	{
		return; // Pass immediately
	}

	nano::unique_lock<nano::mutex> lock{ mutex };

	// There should be no duplicates in the queue
	debug_assert (std::none_of (queue.cbegin (), queue.cend (), [writer] (auto const & item) { return item == writer; }));

	// Add writer to the end of the queue if it's not already waiting
	auto exists = std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
	if (!exists)
	{
		queue.push_back (writer);
	}

	condition.wait (lock, [&] () { return queue.front () == writer; });
}

void nano::store::write_queue::release (writer writer)
{
	if (use_noops)
	{
		return; // Pass immediately
	}
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		release_assert (!queue.empty ());
		release_assert (queue.front () == writer);
		queue.pop_front ();
	}
	condition.notify_all ();
}