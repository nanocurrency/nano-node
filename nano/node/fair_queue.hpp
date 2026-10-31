#pragma once

#include <nano/lib/container_info.hpp>
#include <nano/lib/utility.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

namespace nano
{
// Tells a fair queue whether an origin's channel is still alive, specialized per channel type
template <typename Channel>
struct fair_queue_traits
{
	static bool alive (Channel const &)
	{
		return true;
	}
};

/**
 * Queues requests by origin and serves the origins round robin, each taking its priority's worth of requests per turn.
 * An origin is a source value plus an optional channel and has a queue of its own, capped by max_size_query.
 * Queues come into being as origins appear and go once periodic_update finds them empty with a dead channel.
 */
template <typename Request, typename Source, typename Channel = std::monostate>
class fair_queue final
{
public:
	// Where a request came from, the source value and the channel it arrived on
	// Every distinct origin has a queue of its own
	struct origin
	{
		Source source;
		Channel channel;

		origin (Source source, Channel channel = Channel{});
		origin (origin const &) = default;
		auto operator<=> (origin const &) const = default;
	};

	using origin_type = origin;
	using value_type = std::pair<Request, origin_type>;

	using origin_size_query = std::function<size_t (origin_type const &)>;
	using origin_priority_query = std::function<size_t (origin_type const &)>;
	using origin_alive_query = std::function<bool (origin_type const &)>;

public:
	// Outcome of push_evicting
	struct push_evicting_result
	{
		bool added{ false }; // False when the origin takes no requests at all
		std::optional<Request> evicted; // The origin's oldest request, dropped to make room for the new one
	};

	// Queues the request under its origin, false when the origin's queue is full and the request was dropped
	bool push (Request, origin_type);
	// Pushes like push but a full origin makes room by dropping its oldest request instead of refusing the new one
	push_evicting_result push_evicting (Request, origin_type);

	// Pops the next request in round robin order, something must be queued
	value_type next ();
	// Pops up to max_count requests in round robin order
	std::deque<value_type> next_batch (size_t max_count);
	// Pops the next request of an origin the predicate accepts, nullopt when there is none
	template <typename Predicate>
	std::optional<value_type> next_ready (Predicate && ready);
	// Up to max_count requests of origins the predicate accepts
	template <typename Predicate>
	std::deque<value_type> next_ready_batch (size_t max_count, Predicate && ready);

	// Once per interval erases the empty queues of dead origins and refreshes every queue's cap and priority, true when it ran
	// The batch pops call it on their own, a consumer popping with next alone calls it itself
	bool periodic_update (std::chrono::milliseconds interval = std::chrono::milliseconds{ 1000 * 30 });
	// Drops every queue with its requests
	void clear ();

	// Requests over all origins
	size_t size () const;
	bool empty () const;
	// Origins with a queue, empty ones included
	size_t queues_size () const;
	// Requests queued under the origin
	size_t size (origin_type) const;
	// Cap of the origin's queue, 0 for an unknown origin
	size_t max_size (origin_type) const;
	// Requests the origin takes per turn, 0 for an unknown origin
	size_t priority (origin_type) const;

	nano::container_info container_info () const;

public: // Callbacks
	// Cap of an origin's queue, asked when the origin first appears and again by every periodic_update
	origin_size_query max_size_query{ [] (auto const & origin) { debug_assert (false, "max_size_query callback empty"); return 0; } };
	// Requests an origin takes per turn before the next origin is served, asked like max_size_query
	origin_priority_query priority_query{ [] (auto const & origin) { debug_assert (false, "priority_query callback empty"); return 0; } };
	// Whether an origin's channel is still alive, a dead origin's queue is erased once it is empty
	origin_alive_query alive_query{ [] (auto const & origin) { return fair_queue_traits<Channel>::alive (origin.channel); } };

private:
	// One origin's requests with its cap and its share per turn
	struct entry
	{
		std::deque<Request> requests;
		size_t priority;
		size_t max_size;

		entry (size_t max_size, size_t priority);

		// Takes the oldest request, the queue must not be empty
		Request pop ();
		// Appends the request, false when the queue is full and it was dropped
		bool push (Request);
		// Pushes like push but a full queue makes room by dropping its oldest request, which is returned
		std::optional<Request> push_evicting (Request);
		bool empty () const;
		size_t size () const;
	};

private:
	// The queue of the origin, created with its cap and priority on first use
	entry & find_or_create (origin_type const &);
	// Whether the cursor has to move on, as it points nowhere or at a drained queue or one out of its share
	bool should_seek () const;
	// Moves the cursor to the next non-empty queue, something must be queued
	void seek_next ();
	// Moves the cursor to the next non-empty queue the predicate accepts, false after a full round without one
	template <typename Predicate>
	bool seek_next_ready (Predicate && ready);
	// Erases the empty queues of dead origins
	void cleanup ();
	// Refreshes every queue's cap and priority from the callbacks
	void update ();
	size_t calculate_total_size () const;

private:
	std::map<origin, entry> queues;
	typename std::map<origin, entry>::iterator iterator{ queues.end () }; // The queue being served, end when none is
	size_t counter{ 0 }; // Requests taken from the queue being served in its turn
	size_t total_size{ 0 };
	std::chrono::steady_clock::time_point last_update{ std::chrono::steady_clock::now () };
};
}

/*
 * fair_queue::origin
 */

template <typename Request, typename Source, typename Channel>
nano::fair_queue<Request, Source, Channel>::origin::origin (Source source_a, Channel channel_a) :
	source{ source_a },
	channel{ std::move (channel_a) }
{
}

/*
 * fair_queue::entry
 */

template <typename Request, typename Source, typename Channel>
nano::fair_queue<Request, Source, Channel>::entry::entry (size_t max_size_a, size_t priority_a) :
	priority{ priority_a },
	max_size{ max_size_a }
{
}

template <typename Request, typename Source, typename Channel>
Request nano::fair_queue<Request, Source, Channel>::entry::pop ()
{
	release_assert (!requests.empty ());

	auto request = std::move (requests.front ());
	requests.pop_front ();
	return request;
}

template <typename Request, typename Source, typename Channel>
bool nano::fair_queue<Request, Source, Channel>::entry::push (Request request)
{
	if (requests.size () < max_size)
	{
		requests.push_back (std::move (request));
		return true; // Added
	}
	return false; // Dropped
}

template <typename Request, typename Source, typename Channel>
std::optional<Request> nano::fair_queue<Request, Source, Channel>::entry::push_evicting (Request request)
{
	debug_assert (max_size > 0);

	std::optional<Request> evicted;
	if (requests.size () >= max_size)
	{
		evicted = std::move (requests.front ());
		requests.pop_front ();
	}
	requests.push_back (std::move (request));
	return evicted;
}

template <typename Request, typename Source, typename Channel>
bool nano::fair_queue<Request, Source, Channel>::entry::empty () const
{
	return requests.empty ();
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::entry::size () const
{
	return requests.size ();
}

/*
 * fair_queue
 */

template <typename Request, typename Source, typename Channel>
bool nano::fair_queue<Request, Source, Channel>::push (Request request, origin_type source)
{
	auto & queue = find_or_create (source);
	bool added = queue.push (std::move (request)); // True if added, false if dropped
	if (added)
	{
		++total_size;
	}
	return added;
}

template <typename Request, typename Source, typename Channel>
auto nano::fair_queue<Request, Source, Channel>::push_evicting (Request request, origin_type source) -> push_evicting_result
{
	auto & queue = find_or_create (source);

	push_evicting_result result{};
	// An origin with no room at all refuses like push does, there is nothing to make room from
	if (queue.max_size > 0)
	{
		result.added = true;
		result.evicted = queue.push_evicting (std::move (request));
		if (!result.evicted)
		{
			++total_size; // An eviction leaves the total unchanged
		}
	}
	return result;
}

template <typename Request, typename Source, typename Channel>
auto nano::fair_queue<Request, Source, Channel>::next () -> value_type
{
	release_assert (!empty ()); // Should be checked before calling next
	debug_assert ((std::chrono::steady_clock::now () - last_update) < std::chrono::seconds{ 60 }); // The queue should be cleaned up periodically

	if (should_seek ())
	{
		seek_next ();
	}

	release_assert (iterator != queues.end ());

	auto & source = iterator->first;
	auto & queue = iterator->second;

	++counter;
	--total_size;

	return { queue.pop (), source };
}

template <typename Request, typename Source, typename Channel>
auto nano::fair_queue<Request, Source, Channel>::next_batch (size_t max_count) -> std::deque<value_type>
{
	periodic_update ();

	std::deque<value_type> result;
	while (!empty () && result.size () < max_count)
	{
		result.emplace_back (next ());
	}
	return result;
}

template <typename Request, typename Source, typename Channel>
template <typename Predicate>
auto nano::fair_queue<Request, Source, Channel>::next_ready (Predicate && ready) -> std::optional<value_type>
{
	debug_assert ((std::chrono::steady_clock::now () - last_update) < std::chrono::seconds{ 60 }); // The queue should be cleaned up periodically

	// The current queue serves on within its share while it stays ready, otherwise the next ready one is sought
	if (should_seek () || !ready (iterator->first))
	{
		if (!seek_next_ready (ready))
		{
			return std::nullopt; // Nothing queued is ready
		}
	}

	release_assert (iterator != queues.end ());

	auto & source = iterator->first;
	auto & queue = iterator->second;

	++counter;
	--total_size;

	return value_type{ queue.pop (), source };
}

template <typename Request, typename Source, typename Channel>
template <typename Predicate>
auto nano::fair_queue<Request, Source, Channel>::next_ready_batch (size_t max_count, Predicate && ready) -> std::deque<value_type>
{
	periodic_update ();

	std::deque<value_type> result;
	while (result.size () < max_count)
	{
		auto popped = next_ready (ready);
		if (!popped)
		{
			break; // The rest waits on refused origins
		}
		result.push_back (std::move (*popped));
	}
	return result;
}

template <typename Request, typename Source, typename Channel>
bool nano::fair_queue<Request, Source, Channel>::periodic_update (std::chrono::milliseconds interval)
{
	if (elapsed (last_update, interval))
	{
		last_update = std::chrono::steady_clock::now ();

		cleanup ();
		update ();

		return true; // Updated
	}
	return false; // Not updated
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::clear ()
{
	queues.clear ();
	iterator = queues.end ();
	counter = 0;
	total_size = 0;
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::size () const
{
	debug_assert (total_size == calculate_total_size ());
	return total_size;
}

template <typename Request, typename Source, typename Channel>
bool nano::fair_queue<Request, Source, Channel>::empty () const
{
	return size () == 0;
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::queues_size () const
{
	return queues.size ();
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::size (origin_type source) const
{
	auto it = queues.find (source);
	return it == queues.end () ? 0 : it->second.size ();
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::max_size (origin_type source) const
{
	auto it = queues.find (source);
	return it == queues.end () ? 0 : it->second.max_size;
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::priority (origin_type source) const
{
	auto it = queues.find (source);
	return it == queues.end () ? 0 : it->second.priority;
}

template <typename Request, typename Source, typename Channel>
nano::container_info nano::fair_queue<Request, Source, Channel>::container_info () const
{
	nano::container_info info;
	info.put ("queues", queues);
	info.put ("total_size", size ());
	return info;
}

template <typename Request, typename Source, typename Channel>
auto nano::fair_queue<Request, Source, Channel>::find_or_create (origin_type const & source) -> entry &
{
	auto it = queues.find (source);
	if (it == queues.end ())
	{
		auto max_size = max_size_query (source);
		auto priority = priority_query (source);

		// It's safe to not invalidate current iterator, since std::map container guarantees that iterators are not invalidated by insert operations
		it = queues.emplace (source, entry{ max_size, priority }).first;
	}
	release_assert (it != queues.end ());
	return it->second;
}

template <typename Request, typename Source, typename Channel>
bool nano::fair_queue<Request, Source, Channel>::should_seek () const
{
	if (iterator == queues.end ())
	{
		return true;
	}
	auto & queue = iterator->second;
	if (queue.empty ())
	{
		return true;
	}
	// Allow up to `queue.priority` requests to be processed before moving to the next queue
	if (counter >= queue.priority)
	{
		return true;
	}
	return false;
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::seek_next ()
{
	counter = 0;
	do
	{
		if (iterator != queues.end ())
		{
			++iterator;
		}
		if (iterator == queues.end ())
		{
			iterator = queues.begin ();
		}
		release_assert (iterator != queues.end ());
	} while (iterator->second.empty ());
}

template <typename Request, typename Source, typename Channel>
template <typename Predicate>
bool nano::fair_queue<Request, Source, Channel>::seek_next_ready (Predicate && ready)
{
	auto const previous = iterator;
	auto const previous_counter = counter;

	counter = 0;
	// One round over the queues at most, so a round without a ready origin ends instead of spinning
	for (size_t visited = 0; visited < queues.size (); ++visited)
	{
		if (iterator != queues.end ())
		{
			++iterator;
		}
		if (iterator == queues.end ())
		{
			iterator = queues.begin ();
		}
		if (!iterator->second.empty () && ready (iterator->first))
		{
			return true; // Found
		}
	}

	// Nothing ready, the cursor and the share count stay as they were so the next attempt resumes the same round
	iterator = previous;
	counter = previous_counter;
	return false;
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::cleanup ()
{
	// Invalidate the current iterator
	iterator = queues.end ();

	// Only removing empty queues, no need to update the `total size` counter
	erase_if (queues, [this] (auto const & entry) {
		return entry.second.empty () && !alive_query (entry.first);
	});
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::update ()
{
	for (auto & [source, queue] : queues)
	{
		queue.max_size = max_size_query (source);
		queue.priority = priority_query (source);
	}
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::calculate_total_size () const
{
	return std::accumulate (queues.begin (), queues.end (), size_t{ 0 }, [] (size_t total, auto const & queue) {
		return total + queue.second.size ();
	});
}
