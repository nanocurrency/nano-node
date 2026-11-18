#pragma once

#include <nano/lib/enum_util.hpp>
#include <nano/node/fair_ring.hpp>

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <functional>
#include <utility>

namespace nano
{
/**
 * Queues requests by a key from a closed enum and serves the keys round robin, each taking its priority's worth of requests per turn.
 * Every key has its queue from the start, with the cap and the share asked once at construction, so nothing is looked up, created or cleaned up later.
 * The queues holding requests are served in the order they filled, not in the enum's order.
 */
template <typename Request, typename Key, size_t BatchCapacity = 256>
class enum_fair_queue final
{
public:
	// Requests a batch holds inline before it grows onto the heap
	static constexpr size_t batch_capacity = BatchCapacity;

	using value_type = std::pair<Request, Key>;

	// Requests popped in one go with their keys
	using batch = boost::container::small_vector<value_type, batch_capacity>;

	using key_query = std::function<size_t (Key)>;

public:
	// Asks every key's cap and its share per turn, once
	enum_fair_queue (key_query const & max_size_query, key_query const & priority_query);

	// Queues the request under its key, false when the key's queue is full and the request was dropped
	bool push (Request, Key);

	// Pops the next request in round robin order, something must be queued
	value_type next ();
	// Pops up to max_count requests in round robin order
	batch next_batch (size_t max_count);

	// Requests over all keys
	size_t size () const;
	bool empty () const;
	// Requests queued under the key
	size_t size (Key) const;
	// True when the key's queue holds its cap and a push under the key would be dropped
	bool full (Key) const;

private:
	nano::enum_array<Key, nano::fair_lane<Request, Key>> queues;
	nano::fair_ring<Request, Key> ring; // Declared after the queues, so it detaches from them before they go
};
}

template <typename Request, typename Key, size_t BatchCapacity>
nano::enum_fair_queue<Request, Key, BatchCapacity>::enum_fair_queue (key_query const & max_size_query, key_query const & priority_query)
{
	for (auto key : nano::enum_values<Key> ())
	{
		auto & queue = queues.at (key);
		queue.key = key;
		queue.max_size = max_size_query (key);
		queue.priority = priority_query (key);
	}
}

template <typename Request, typename Key, size_t BatchCapacity>
bool nano::enum_fair_queue<Request, Key, BatchCapacity>::push (Request request, Key key)
{
	return ring.push (queues.at (key), std::move (request)); // True if added, false if dropped
}

template <typename Request, typename Key, size_t BatchCapacity>
auto nano::enum_fair_queue<Request, Key, BatchCapacity>::next () -> value_type
{
	return ring.next ();
}

template <typename Request, typename Key, size_t BatchCapacity>
auto nano::enum_fair_queue<Request, Key, BatchCapacity>::next_batch (size_t max_count) -> batch
{
	batch result;
	result.reserve (std::min (max_count, size ()));
	while (!empty () && result.size () < max_count)
	{
		result.emplace_back (next ());
	}
	return result;
}

template <typename Request, typename Key, size_t BatchCapacity>
size_t nano::enum_fair_queue<Request, Key, BatchCapacity>::size () const
{
	return ring.size ();
}

template <typename Request, typename Key, size_t BatchCapacity>
bool nano::enum_fair_queue<Request, Key, BatchCapacity>::empty () const
{
	return ring.empty ();
}

template <typename Request, typename Key, size_t BatchCapacity>
size_t nano::enum_fair_queue<Request, Key, BatchCapacity>::size (Key key) const
{
	return queues.at (key).size ();
}

template <typename Request, typename Key, size_t BatchCapacity>
bool nano::enum_fair_queue<Request, Key, BatchCapacity>::full (Key key) const
{
	return queues.at (key).full ();
}
