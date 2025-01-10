#include <celerix/lib/config.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/store/write_queue.hpp>

#include <algorithm>

/*
 * write_guard
 */

celerix::store::write_guard::write_guard (write_queue & queue, writer type) :
	queue{ queue },
	type{ type }
{
	renew ();
}

celerix::store::write_guard::write_guard (write_guard && other) noexcept :
	queue{ other.queue },
	type{ other.type },
	owns{ other.owns }
{
	other.owns = false;
}

celerix::store::write_guard::~write_guard ()
{
	if (owns)
	{
		release ();
	}
}

bool celerix::store::write_guard::is_owned () const
{
	return owns;
}

void celerix::store::write_guard::release ()
{
	release_assert (owns);
	queue.release (type);
	owns = false;
}

void celerix::store::write_guard::renew ()
{
	release_assert (!owns);
	queue.acquire (type);
	owns = true;
}

/*
 * write_queue
 */

celerix::store::write_queue::write_queue ()
{
}

celerix::store::write_guard celerix::store::write_queue::wait (writer writer)
{
	return write_guard{ *this, writer };
}

bool celerix::store::write_queue::contains (writer writer) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return std::any_of (queue.cbegin (), queue.cend (), [writer] (auto const & item) {
		return item.first == writer;
	});
}

void celerix::store::write_queue::pop ()
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	if (!queue.empty ())
	{
		queue.pop_front ();
	}
	condition.notify_all ();
}

void celerix::store::write_queue::acquire (writer writer)
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };

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

void celerix::store::write_queue::release (writer writer)
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		release_assert (!queue.empty ());
		release_assert (queue.front ().first == writer);
		queue.pop_front ();
	}
	condition.notify_all ();
}
