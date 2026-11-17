#pragma once

#include <nano/lib/container_info.hpp>
#include <nano/lib/utility.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/unordered/unordered_node_map.hpp>

#include <algorithm>
#include <chrono>
#include <concepts>
#include <deque>
#include <functional>
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

// What becomes of the requests queued under an origin whose channel has died
enum class dead_origin_policy
{
	serve, // Still served, the origin goes once it is empty, for requests that stand on their own like blocks and votes
	discard, // Dropped with the origin at the next purge, for requests that only mean something to the channel like replies
};

/**
 * Queues requests by origin and serves the origins round robin, each taking its priority's worth of requests per turn.
 * An origin is a source value plus an optional channel and has a queue of its own, capped by max_size_query.
 * Queues come into being as origins appear and go once maintenance finds their channel dead, drained first or not as the dead_origin_policy says.
 * The non-empty queues form a ring in the order they filled, which the pops walk, so the queues of idle origins cost a pop nothing.
 */
template <typename Request, typename Source, typename Channel = std::monostate>
class fair_queue final
{
public:
	// Requests a batch holds inline before it grows onto the heap
	static constexpr size_t batch_capacity = 256;

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

	// Requests popped in one go with their origins
	using batch = boost::container::small_vector<value_type, batch_capacity>;

	using origin_size_query = std::function<size_t (origin_type const &)>;
	using origin_priority_query = std::function<size_t (origin_type const &)>;
	using origin_alive_query = std::function<bool (origin_type const &)>;

public:
	// A queue without channels has no dead origins to decide about
	fair_queue ()
		requires std::same_as<Channel, std::monostate>
	= default;
	// A queue keyed by channels states what happens to the requests of an origin whose channel dies
	explicit fair_queue (nano::dead_origin_policy);

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
	batch next_batch (size_t max_count);
	// Pops the next request of an origin the predicate accepts, nullopt when there is none
	template <typename Predicate>
	std::optional<value_type> next_ready (Predicate && ready);
	// Up to max_count requests of origins the predicate accepts
	template <typename Predicate>
	batch next_ready_batch (size_t max_count, Predicate && ready);

	// Pacing for the upkeep below: it runs at most once per maintenance_interval, every other call returns at once
	// The upkeep erases the dead origins, under discard together with their queued requests, and re-asks the cap and priority of every origin left
	// Returns the requests dropped, 0 for a call that did nothing; the batch pops and every new origin call it, so a consumer popping with next alone calls it itself
	size_t maintenance ();

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
	// Cap of an origin's queue, asked when the origin first appears and again by every maintenance
	origin_size_query max_size_query{ [] (auto const & origin) { debug_assert (false, "max_size_query callback empty"); return 0; } };
	// Requests an origin takes per turn before the next origin is served, at least one whatever it returns, asked like max_size_query
	origin_priority_query priority_query{ [] (auto const & origin) { debug_assert (false, "priority_query callback empty"); return 0; } };
	// Whether an origin's channel is still alive, a dead origin is purged as the policy says
	origin_alive_query alive_query{ [] (auto const & origin) { return fair_queue_traits<Channel>::alive (origin.channel); } };

	// How long maintenance waits between two runs, a bound on what dead origins hold until the next one
	std::chrono::milliseconds maintenance_interval{ std::chrono::seconds{ 5 } };

private:
	// One origin's requests with its cap and its share per turn
	struct entry
	{
		std::deque<Request> requests;
		size_t priority;
		size_t max_size;
		origin const * key{ nullptr }; // The origin this is the queue of, the map's copy of it
		boost::intrusive::list_member_hook<> hook; // Links the queue into the ring while it holds requests

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

	// Hashes an origin by its source and its channel for the queue lookup
	struct origin_hash
	{
		size_t operator() (origin const &) const;
	};

	// The non-empty queues in the order they filled, an intrusive list since the entries live in the map and a pop must never allocate
	using ring_type = boost::intrusive::list<entry, boost::intrusive::member_hook<entry, boost::intrusive::list_member_hook<>, &entry::hook>, boost::intrusive::constant_time_size<false>>;

private:
	// The queue of the origin, created with its cap and priority on first use
	entry & find_or_create (origin_type const &);
	// Puts a queue that just took its first request at the end of the round under way, behind the cursor
	void join_ring (entry &);
	// Takes a queue about to be erased out of the ring, the cursor moving past it as past a drained queue
	void leave_ring (entry &);
	// Pops from the queue under the cursor, then passes the turn to the next queue once this one is drained or has taken its share
	value_type pop_current ();
	// Erases the dead origins, under serve only the empty ones, returns the requests dropped
	size_t purge ();
	// Re-asks every origin's cap and priority from the callbacks
	void refresh ();
	size_t calculate_total_size () const;

private:
	nano::dead_origin_policy policy{ nano::dead_origin_policy::serve }; // Moot without channels, every origin stays alive then
	boost::unordered_node_map<origin, entry, origin_hash> queues; // A node map, so the entries and their keys keep their addresses across rehashes, which the ring and the key pointers rely on
	ring_type ring; // Declared after the queues, so it detaches from them before they go
	typename ring_type::iterator cursor{ ring.end () }; // The queue whose turn it is, end when the round starts over at the first queue or nothing is queued
	size_t counter{ 0 }; // Requests the queue under the cursor has taken in its turn, 0 while the cursor is at the end
	size_t total_size{ 0 };
	std::chrono::steady_clock::time_point last_maintenance{ std::chrono::steady_clock::now () };
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
 * fair_queue::origin_hash
 */

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::origin_hash::operator() (origin const & origin_a) const
{
	size_t seed = std::hash<Source>{}(origin_a.source);
	boost::hash_combine (seed, std::hash<Channel>{}(origin_a.channel));
	return seed;
}

/*
 * fair_queue
 */

template <typename Request, typename Source, typename Channel>
nano::fair_queue<Request, Source, Channel>::fair_queue (nano::dead_origin_policy policy_a) :
	policy{ policy_a }
{
}

template <typename Request, typename Source, typename Channel>
bool nano::fair_queue<Request, Source, Channel>::push (Request request, origin_type source)
{
	auto & queue = find_or_create (source);
	bool const was_empty = queue.empty ();
	bool added = queue.push (std::move (request)); // True if added, false if dropped
	if (added)
	{
		++total_size;
		if (was_empty)
		{
			join_ring (queue);
		}
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
		bool const was_empty = queue.empty ();
		result.added = true;
		result.evicted = queue.push_evicting (std::move (request));
		if (!result.evicted)
		{
			++total_size; // An eviction leaves the total unchanged
		}
		if (was_empty)
		{
			join_ring (queue);
		}
	}
	return result;
}

template <typename Request, typename Source, typename Channel>
auto nano::fair_queue<Request, Source, Channel>::next () -> value_type
{
	release_assert (!empty ()); // Should be checked before calling next
	debug_assert ((std::chrono::steady_clock::now () - last_maintenance) < std::chrono::seconds{ 60 }); // The queue should be cleaned up periodically

	// A round that ran off the end of the ring starts over at its first queue
	if (cursor == ring.end ())
	{
		debug_assert (counter == 0);
		cursor = ring.begin ();
	}

	return pop_current ();
}

template <typename Request, typename Source, typename Channel>
auto nano::fair_queue<Request, Source, Channel>::next_batch (size_t max_count) -> batch
{
	maintenance ();

	batch result;
	result.reserve (std::min (max_count, size ()));
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
	debug_assert ((std::chrono::steady_clock::now () - last_maintenance) < std::chrono::seconds{ 60 }); // The queue should be cleaned up periodically

	debug_assert (ring.empty () == (total_size == 0));
	if (ring.empty ())
	{
		return std::nullopt; // Nothing queued at all
	}

	// One round over the ring at most, from the queue whose turn it is, so a round without a ready origin ends instead of spinning
	auto const start = cursor == ring.end () ? ring.begin () : cursor;
	auto candidate = start;
	do
	{
		if (ready (*candidate->key))
		{
			// The queue whose turn it is serves on within its share, any other one starts a turn of its own
			if (candidate != cursor)
			{
				cursor = candidate;
				counter = 0;
			}
			return pop_current ();
		}
		++candidate;
		if (candidate == ring.end ())
		{
			candidate = ring.begin ();
		}
	} while (candidate != start);

	// Nothing ready, the cursor and the share count stay as they were so the next attempt resumes the same round
	return std::nullopt;
}

template <typename Request, typename Source, typename Channel>
template <typename Predicate>
auto nano::fair_queue<Request, Source, Channel>::next_ready_batch (size_t max_count, Predicate && ready) -> batch
{
	maintenance ();

	batch result;
	result.reserve (std::min (max_count, size ()));
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
size_t nano::fair_queue<Request, Source, Channel>::maintenance ()
{
	if (!elapsed (last_maintenance, maintenance_interval))
	{
		return 0; // Not due
	}
	last_maintenance = std::chrono::steady_clock::now ();

	auto const dropped = purge ();
	refresh ();
	return dropped;
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::clear ()
{
	ring.clear ();
	queues.clear ();
	cursor = ring.end ();
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
		// A new origin is the one way the queue grows, so maintenance is due here as much as at the pops
		maintenance ();

		auto max_size = max_size_query (source);
		auto priority = priority_query (source);

		// The entry is built in place, so its hook and the key pointer it is given are never moved
		it = queues.try_emplace (source, max_size, priority).first;
		it->second.key = &it->first;
	}
	release_assert (it != queues.end ());
	return it->second;
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::join_ring (entry & queue)
{
	debug_assert (!queue.empty ());
	release_assert (!queue.hook.is_linked ());
	// Behind the cursor is the end of the round under way, so a round stays bounded by the queues it started with and a refill cannot overtake the ones still waiting
	if (cursor == ring.end ())
	{
		ring.push_back (queue);
	}
	else
	{
		ring.insert (cursor, queue);
	}
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::leave_ring (entry & queue)
{
	if (queue.empty ())
	{
		debug_assert (!queue.hook.is_linked ());
		return; // Never joined or already left
	}

	release_assert (queue.hook.is_linked ());
	auto const position = ring.iterator_to (queue);
	if (position == cursor)
	{
		// The next queue takes over the turn with a fresh share
		cursor = ring.erase (position);
		counter = 0;
	}
	else
	{
		ring.erase (position);
	}
}

template <typename Request, typename Source, typename Channel>
auto nano::fair_queue<Request, Source, Channel>::pop_current () -> value_type
{
	release_assert (cursor != ring.end ());

	// The queue under the cursor is served first, whatever its share
	auto & queue = *cursor;
	auto const & source = *queue.key; // The key outlives the pop, which at most takes the queue out of the ring
	auto request = queue.pop ();
	++counter;
	--total_size;

	// Its turn is decided only now, so a queue the cursor reaches can never be passed over without a request served
	if (queue.empty ())
	{
		// A drained queue leaves the ring and the turn passes to the next one
		cursor = ring.erase (cursor);
		counter = 0;
	}
	else if (counter >= queue.priority)
	{
		// The share is taken and the turn passes to the next queue
		++cursor;
		counter = 0;
	}
	return { std::move (request), source };
}

template <typename Request, typename Source, typename Channel>
size_t nano::fair_queue<Request, Source, Channel>::purge ()
{
	size_t dropped = 0;
	erase_if (queues, [this, &dropped] (auto & entry) {
		if (alive_query (entry.first))
		{
			return false;
		}
		switch (policy)
		{
			case nano::dead_origin_policy::serve:
				return entry.second.empty (); // Drained first, its requests are still worth serving, an empty queue is out of the ring already
			case nano::dead_origin_policy::discard:
			{
				dropped += entry.second.size (); // No one left to reply to
				leave_ring (entry.second);
				return true;
			}
		}
		debug_assert (false);
		return false;
	});
	total_size -= dropped;
	return dropped;
}

template <typename Request, typename Source, typename Channel>
void nano::fair_queue<Request, Source, Channel>::refresh ()
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
