#pragma once

#include <nano/lib/utility.hpp>

#include <boost/intrusive/list.hpp>

#include <deque>
#include <optional>
#include <utility>

namespace nano
{
template <typename Request, typename Key>
class fair_ring;

/**
 * One origin's requests with its cap and its share per turn, served by a fair_ring.
 * The owner keeps its lanes where they never move, in map nodes or an array; their requests change only through the ring.
 */
template <typename Request, typename Key>
class fair_lane final
{
public:
	fair_lane () = default;
	fair_lane (Key key, size_t max_size, size_t priority);
	fair_lane (fair_lane const &) = delete; // The ring links the lane in place
	fair_lane & operator= (fair_lane const &) = delete;
	~fair_lane ();

	bool empty () const;
	size_t size () const;
	// True when the lane holds its cap and a push would be dropped
	bool full () const;

public:
	// The origin a request popped from the lane reports
	Key key{};
	// Requests the lane holds at most
	size_t max_size{ 0 };
	// Requests the lane takes per turn before the next lane is served, at least one whatever it says
	size_t priority{ 0 };

private:
	// Takes the oldest request, the lane must not be empty
	Request pop ();
	// Appends the request, false when the lane is full and it was dropped
	bool push (Request);
	// Pushes like push but a full lane makes room by dropping its oldest request, which is returned
	std::optional<Request> push_evicting (Request);

private:
	std::deque<Request> requests;
	boost::intrusive::list_member_hook<> hook; // Links the lane into its ring while it holds requests

	friend class nano::fair_ring<Request, Key>;
};

/**
 * Serves lanes round robin, each taking its priority's worth of requests per turn before the turn passes to the next lane.
 * The lanes holding requests form a ring in the order they filled, which the pops walk, so idle lanes cost a pop nothing.
 * The lanes belong to the owner, which discards a lane holding requests before it destroys it and declares the ring after its lanes.
 */
template <typename Request, typename Key>
class fair_ring final
{
public:
	using lane_type = nano::fair_lane<Request, Key>;
	using value_type = std::pair<Request, Key>;

public:
	fair_ring () = default;
	fair_ring (fair_ring const &) = delete; // The cursor points into this ring's own list, so the ring neither copies nor moves
	fair_ring & operator= (fair_ring const &) = delete;
	~fair_ring (); // Drops what is still queued, so a lane outliving the ring can serve another

	// Queues the request in the lane, false when the lane is full and it was dropped
	bool push (lane_type &, Request);
	// Pushes like push but a full lane makes room by dropping its oldest request, which is returned; the lane must take requests at all
	std::optional<Request> push_evicting (lane_type &, Request);

	// Pops the next request in round robin order with its lane's key, something must be queued
	value_type next ();
	// Pops the next request of a lane whose key the predicate accepts, nullopt when there is none
	template <typename Predicate>
	std::optional<value_type> next_ready (Predicate && ready);

	// Drops the lane's requests and takes it out of the ring, the turn moving past it as past a drained lane; returns the requests dropped
	size_t discard (lane_type &);
	// Drops the requests of every lane
	void clear ();

	// Requests over all lanes
	size_t size () const;
	bool empty () const;

private:
	// Puts a lane that just took its first request at the end of the round under way, behind the cursor
	void join (lane_type &);
	// Pops from the lane under the cursor, then passes the turn to the next lane once this one is drained or has taken its share
	value_type pop_current ();

private:
	// The lanes holding requests in the order they filled, an intrusive list since the lanes live with the owner and a pop must never allocate
	using ring_type = boost::intrusive::list<lane_type, boost::intrusive::member_hook<lane_type, boost::intrusive::list_member_hook<>, &lane_type::hook>, boost::intrusive::constant_time_size<false>>;
	ring_type lanes;

	typename ring_type::iterator cursor{ lanes.end () }; // The lane whose turn it is, end when the round starts over at the first lane or nothing is queued
	size_t counter{ 0 }; // Requests the lane under the cursor has taken in its turn, 0 while the cursor is at the end
	size_t total_size{ 0 };
};
}

/*
 * fair_lane
 */

template <typename Request, typename Key>
nano::fair_lane<Request, Key>::fair_lane (Key key_a, size_t max_size_a, size_t priority_a) :
	key{ std::move (key_a) },
	max_size{ max_size_a },
	priority{ priority_a }
{
}

template <typename Request, typename Key>
nano::fair_lane<Request, Key>::~fair_lane ()
{
	// The ring would be left pointing at freed memory
	release_assert (!hook.is_linked (), "lane destroyed while its ring still serves it");
}

template <typename Request, typename Key>
bool nano::fair_lane<Request, Key>::empty () const
{
	return requests.empty ();
}

template <typename Request, typename Key>
size_t nano::fair_lane<Request, Key>::size () const
{
	return requests.size ();
}

template <typename Request, typename Key>
bool nano::fair_lane<Request, Key>::full () const
{
	return requests.size () >= max_size;
}

template <typename Request, typename Key>
Request nano::fair_lane<Request, Key>::pop ()
{
	release_assert (!requests.empty ());

	auto request = std::move (requests.front ());
	requests.pop_front ();
	return request;
}

template <typename Request, typename Key>
bool nano::fair_lane<Request, Key>::push (Request request)
{
	if (!full ())
	{
		requests.push_back (std::move (request));
		return true; // Added
	}
	return false; // Dropped
}

template <typename Request, typename Key>
std::optional<Request> nano::fair_lane<Request, Key>::push_evicting (Request request)
{
	debug_assert (max_size > 0);

	std::optional<Request> evicted;
	if (full ())
	{
		evicted = std::move (requests.front ());
		requests.pop_front ();
	}
	requests.push_back (std::move (request));
	return evicted;
}

/*
 * fair_ring
 */

template <typename Request, typename Key>
nano::fair_ring<Request, Key>::~fair_ring ()
{
	clear ();
}

template <typename Request, typename Key>
bool nano::fair_ring<Request, Key>::push (lane_type & lane, Request request)
{
	bool const was_empty = lane.empty ();
	bool const added = lane.push (std::move (request)); // True if added, false if dropped
	if (added)
	{
		++total_size;
		if (was_empty)
		{
			join (lane);
		}
	}
	return added;
}

template <typename Request, typename Key>
std::optional<Request> nano::fair_ring<Request, Key>::push_evicting (lane_type & lane, Request request)
{
	bool const was_empty = lane.empty ();
	auto evicted = lane.push_evicting (std::move (request));
	if (!evicted)
	{
		++total_size; // An eviction leaves the total unchanged
	}
	if (was_empty)
	{
		join (lane);
	}
	return evicted;
}

template <typename Request, typename Key>
auto nano::fair_ring<Request, Key>::next () -> value_type
{
	release_assert (!empty ()); // Should be checked before calling next

	// A round that ran off the end of the ring starts over at its first lane
	if (cursor == lanes.end ())
	{
		debug_assert (counter == 0);
		cursor = lanes.begin ();
	}

	return pop_current ();
}

template <typename Request, typename Key>
template <typename Predicate>
auto nano::fair_ring<Request, Key>::next_ready (Predicate && ready) -> std::optional<value_type>
{
	debug_assert (lanes.empty () == (total_size == 0));
	if (lanes.empty ())
	{
		return std::nullopt; // Nothing queued at all
	}

	// One round over the ring at most, from the lane whose turn it is, so a round without a ready lane ends instead of spinning
	auto const start = cursor == lanes.end () ? lanes.begin () : cursor;
	auto candidate = start;
	do
	{
		if (ready (std::as_const (candidate->key)))
		{
			// The lane whose turn it is serves on within its share, any other one starts a turn of its own
			if (candidate != cursor)
			{
				cursor = candidate;
				counter = 0;
			}
			return pop_current ();
		}
		++candidate;
		if (candidate == lanes.end ())
		{
			candidate = lanes.begin ();
		}
	} while (candidate != start);

	// Nothing ready, the cursor and the share count stay as they were so the next attempt resumes the same round
	return std::nullopt;
}

template <typename Request, typename Key>
size_t nano::fair_ring<Request, Key>::discard (lane_type & lane)
{
	if (lane.empty ())
	{
		debug_assert (!lane.hook.is_linked ());
		return 0; // Never joined or already left
	}

	// Out of the ring first, a lane under the cursor handing its turn to the next one with a fresh share
	release_assert (lane.hook.is_linked ());
	auto const position = lanes.iterator_to (lane);
	if (position == cursor)
	{
		cursor = lanes.erase (position);
		counter = 0;
	}
	else
	{
		lanes.erase (position);
	}

	// Then its requests go
	auto const dropped = lane.size ();
	lane.requests.clear ();
	total_size -= dropped;
	return dropped;
}

template <typename Request, typename Key>
void nano::fair_ring<Request, Key>::clear ()
{
	lanes.clear_and_dispose ([] (lane_type * lane) {
		lane->requests.clear ();
	});
	cursor = lanes.end ();
	counter = 0;
	total_size = 0;
}

template <typename Request, typename Key>
size_t nano::fair_ring<Request, Key>::size () const
{
	return total_size;
}

template <typename Request, typename Key>
bool nano::fair_ring<Request, Key>::empty () const
{
	return total_size == 0;
}

template <typename Request, typename Key>
void nano::fair_ring<Request, Key>::join (lane_type & lane)
{
	debug_assert (!lane.empty ());
	release_assert (!lane.hook.is_linked ());
	// Behind the cursor is the end of the round under way, so a round stays bounded by the lanes it started with and a refill cannot overtake the ones still waiting
	if (cursor == lanes.end ())
	{
		lanes.push_back (lane);
	}
	else
	{
		lanes.insert (cursor, lane);
	}
}

template <typename Request, typename Key>
auto nano::fair_ring<Request, Key>::pop_current () -> value_type
{
	release_assert (cursor != lanes.end ());

	// The lane under the cursor is served first, whatever its share
	auto & lane = *cursor; // The lane outlives the pop, which at most takes it out of the ring
	auto request = lane.pop ();
	++counter;
	--total_size;

	// Its turn is decided only now, so a lane the cursor reaches can never be passed over without a request served
	if (lane.empty ())
	{
		// A drained lane leaves the ring and the turn passes to the next one
		cursor = lanes.erase (cursor);
		counter = 0;
	}
	else if (counter >= lane.priority)
	{
		// The share is taken and the turn passes to the next lane
		++cursor;
		counter = 0;
	}
	return { std::move (request), lane.key };
}
