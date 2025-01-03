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

nano::store::write_queue::write_queue ()
{
}

nano::store::write_guard nano::store::write_queue::wait (writer writer)
{
	return write_guard{ *this, writer };
}

bool nano::store::write_queue::contains (writer writer) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return std::any_of (queue.cbegin (), queue.cend (), [writer] (auto const & item) {
		return item.first == writer;
	});
}

void nano::store::write_queue::pop ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (!queue.empty ())
	{
		queue.pop_front ();
	}
	condition.notify_all ();
}

void nano::store::write_queue::acquire (writer writer)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// There should be no duplicates in the queue (exception is testing)
	debug_assert (std::none_of (queue.cbegin (), queue.cend (), [writer] (auto const & item) {
		return item.first == writer;
	})
	|| writer == writer::testing);

	auto const id = next++;

	// Add writer to the end of the queue if it's not already waiting
	queue.push_back ({ writer, id });

	// Wait until we are at the front of the queue
	condition.wait (lock, [&] () { return queue.front ().second == id; });
}

void nano::store::write_queue::release (writer writer)
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		release_assert (!queue.empty ());
		release_assert (queue.front ().first == writer);
		queue.pop_front ();
	}
	condition.notify_all ();
}
