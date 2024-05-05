#pragma once

#include <nano/lib/utility.hpp>
#include <nano/node/transport/channel.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <numeric>
#include <tuple>
#include <utility>

namespace nano
{
template <typename Request, typename Source>
class fair_queue final
{
public:
	/**
	 * Holds user supplied source type(s) and an optional channel. This is used to uniquely identify and categorize the source of a request.
	 */
	struct origin
	{
		Source source;

		// This can be null for some sources (eg. local RPC) to indicate that the source is not associated with a channel.
		std::shared_ptr<nano::transport::channel> channel;

		origin (Source source, std::shared_ptr<nano::transport::channel> channel = nullptr) :
			source{ source },
			channel{ std::move (channel) }
		{
		}

		origin (origin const & origin) = default;

		bool alive () const
		{
			if (channel)
			{
				return channel->alive ();
			}
			else
			{
				// Some sources (eg. local RPC) don't have an associated channel, never remove their queue
				return true;
			}
		}

		auto operator<=> (origin const & other) const = default;
	};

private:
	struct entry
	{
		using queue_t = std::deque<Request>;
		queue_t requests;

		size_t priority;
		size_t max_size;

		entry (size_t max_size, size_t priority) :
			priority{ priority },
			max_size{ max_size }
		{
		}

		Request pop ()
		{
			release_assert (!requests.empty ());

			auto request = std::move (requests.front ());
			requests.pop_front ();
			return request;
		}

		bool push (Request request)
		{
			if (requests.size () < max_size)
			{
				requests.push_back (std::move (request));
				return true; // Added
			}
			return false; // Dropped
		}

		bool empty () const
		{
			return requests.empty ();
		}

		size_t size () const
		{
			return requests.size ();
		}
	};

public:
	using origin_type = origin;
	using value_type = std::pair<Request, origin_type>;

public:
	size_t size (origin_type source) const
	{
		auto it = queues.find (source);
		return it == queues.end () ? 0 : it->second.size ();
	}

	size_t max_size (origin_type source) const
	{
		auto it = queues.find (source);
		return it == queues.end () ? 0 : it->second.max_size;
	}

	size_t priority (origin_type source) const
	{
		auto it = queues.find (source);
		return it == queues.end () ? 0 : it->second.priority;
	}

	size_t size () const
	{
		debug_assert (total_size == calculate_total_size ());
		return total_size;
	};

	bool empty () const
	{
		return size () == 0;
	}

	size_t queues_size () const
	{
		return queues.size ();
	}

	void clear ()
	{
		queues.clear ();
	}

	/**
	 * Should be called periodically to clean up stale channels and update queue priorities and max sizes
	 */
	bool periodic_update (std::chrono::milliseconds interval = std::chrono::milliseconds{ 1000 * 30 })
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

	/**
	 * Push a request to the appropriate queue based on the source
	 * Request will be dropped if the queue is full
	 * @return true if added, false if dropped
	 */
	bool push (Request request, origin_type source)
	{
		auto it = queues.find (source);

		// Create a new queue if it doesn't exist
		if (it == queues.end ())
		{
			auto max_size = max_size_query (source);
			auto priority = priority_query (source);

			// It's safe to not invalidate current iterator, since std::map container guarantees that iterators are not invalidated by insert operations
			it = queues.emplace (source, entry{ max_size, priority }).first;
		}
		release_assert (it != queues.end ());

		auto & queue = it->second;
		bool added = queue.push (std::move (request)); // True if added, false if dropped
		if (added)
		{
			++total_size;
		}
		return added;
	}

public:
	using max_size_query_t = std::function<size_t (origin_type const &)>;
	using priority_query_t = std::function<size_t (origin_type const &)>;

	max_size_query_t max_size_query{ [] (auto const & origin) { debug_assert (false, "max_size_query callback empty"); return 0; } };
	priority_query_t priority_query{ [] (auto const & origin) { debug_assert (false, "priority_query callback empty"); return 0; } };

public:
	value_type next ()
	{
		release_assert (!empty ()); // Should be checked before calling next
		debug_assert ((std::chrono::steady_clock::now () - last_update) < 60s); // The queue should be cleaned up periodically

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

	std::deque<value_type> next_batch (size_t max_count)
	{
		periodic_update ();

		auto const count = std::min (size (), max_count);

		std::deque<value_type> result;
		while (result.size () < count)
		{
			result.emplace_back (next ());
		}
		return result;
	}

private:
	bool should_seek () const
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

	void seek_next ()
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

	void cleanup ()
	{
		// Invalidate the current iterator
		iterator = queues.end ();

		// Only removing empty queues, no need to update the `total size` counter
		erase_if (queues, [] (auto const & entry) {
			return entry.second.empty () && !entry.first.alive ();
		});
	}

	void update ()
	{
		for (auto & [source, queue] : queues)
		{
			queue.max_size = max_size_query (source);
			queue.priority = priority_query (source);
		}
	}

	size_t calculate_total_size () const
	{
		return std::accumulate (queues.begin (), queues.end (), size_t{ 0 }, [] (size_t total, auto const & queue) {
			return total + queue.second.size ();
		});
	}

private:
	std::map<origin, entry> queues;
	typename std::map<origin, entry>::iterator iterator{ queues.end () };
	size_t counter{ 0 };
	size_t total_size{ 0 };
	std::chrono::steady_clock::time_point last_update{ std::chrono::steady_clock::now () };

public:
	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const
	{
		auto composite = std::make_unique<container_info_composite> (name);
		composite->add_component (std::make_unique<container_info_leaf> (container_info_entry{ "queues", queues_size (), sizeof (typename decltype (queues)::value_type) }));
		composite->add_component (std::make_unique<container_info_leaf> (container_info_entry{ "total_size", size (), sizeof (typename decltype (queues)::value_type) }));
		return composite;
	}
};
}
