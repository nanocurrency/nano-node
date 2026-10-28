#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
/**
 * Tracks vote relay requests that could not be served from the vote cache and are waiting for votes from representatives.
 * Upstream queries are deduplicated against pairs already in flight, an arriving vote satisfies all requests waiting for it.
 * Votes are handed out in flushes as they arrive, a request ends once every queried representative answered, when no vote came for the idle timeout, or at the end of its lifetime.
 * Pure state, thread safety and time are the responsibility of the caller.
 */
class vote_relay_index
{
public:
	using id_t = uint64_t;

	// Time and size limits of a request, injected so the index carries no configuration
	struct request_limits
	{
		std::chrono::milliseconds lifetime; // Longest a request is kept open for votes
		std::chrono::milliseconds idle_timeout; // A request without a new vote for this long is finished
		std::chrono::milliseconds flush_interval; // Longest a received vote waits for others before it is handed out
		std::size_t flush_threshold; // Unsent votes that are handed out without waiting for the interval
	};

	// Representatives that still need to answer for a single hash
	struct want
	{
		nano::block_hash hash;
		nano::root root;
		std::deque<nano::account> reps;
	};

	// Upstream query to send to a single representative
	struct query
	{
		nano::account rep;
		std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	};

	// Why votes are handed out for a request
	enum class outcome
	{
		flush, // The unsent votes go out, the request stays open
		completed, // Every queried representative answered
		idle, // No vote arrived for the idle timeout
		expired, // The lifetime ran out
	};

	// Votes to send for a tracked request, followed by the terminator unless it is a flush
	struct reply
	{
		std::shared_ptr<nano::transport::channel> channel;
		id_t id;
		std::deque<std::shared_ptr<nano::vote>> votes; // Received since the previous flush
		outcome result; // Why the votes go out, the request is finished unless it is a flush
	};

public:
	explicit vote_relay_index (request_limits);

	// Track a request, replacing any previous one with the same channel and id, its lifetime and idle timeout start now
	// @return queries for (hash, rep) pairs that are not already in flight
	std::deque<query> insert (std::shared_ptr<nano::transport::channel> const &, id_t, std::deque<want> const &, nano::vote_finality, std::chrono::steady_clock::time_point now);

	// Offer a processed vote to pending requests
	// @return requests the vote completed, and requests whose unsent votes reached the flush threshold
	std::deque<reply> vote (std::shared_ptr<nano::vote> const &, std::chrono::steady_clock::time_point now);

	// Hand out what is due: flushes past their interval, and the remaining votes of requests past their idle timeout or lifetime, which are removed
	std::deque<reply> due (std::chrono::steady_clock::time_point now);

	// Stop tracking a request together with its pending rows
	// @return false if the request is unknown
	bool erase (std::shared_ptr<nano::transport::channel> const &, id_t);
	void clear ();

	// Earliest moment due () has something to hand out
	std::optional<std::chrono::steady_clock::time_point> next_due () const;

	std::size_t size () const;
	// Number of tracked requests from the channel
	std::size_t size (std::shared_ptr<nano::transport::channel> const &) const;
	// Number of (request, hash) rows still waiting for representatives
	std::size_t pending_size () const;
	bool empty () const;

private:
	struct request_entry
	{
		std::shared_ptr<nano::transport::channel> channel;
		id_t id;
		std::chrono::steady_clock::time_point expiry; // End of the lifetime
		std::chrono::steady_clock::time_point idle_due; // When the request is finished for lack of votes, pushed back by every vote
		std::optional<std::chrono::steady_clock::time_point> flush_due; // When the unsent votes go out, set by the first of them
		std::chrono::steady_clock::time_point due; // Earliest of the three, what the caller waits for
		std::deque<std::shared_ptr<nano::vote>> votes; // Votes received and not handed out yet
		std::size_t pending; // Number of live pending rows
	};

	// One row per (request, hash) pair, removed once all wanted reps have answered
	struct pending_entry
	{
		std::shared_ptr<nano::transport::channel> channel;
		id_t id;
		nano::block_hash hash;
		nano::root root;
		std::deque<nano::account> reps; // Reps that have not answered yet
		nano::vote_finality finality; // Votes that settle a rep for this hash
	};

	// Union of reps with an in-flight query for this hash
	std::unordered_set<nano::account> in_flight_reps (nano::block_hash const &) const;
	// Drop the pending rows of a request, the request entry itself stays
	void erase_pending (std::shared_ptr<nano::transport::channel> const &, id_t);
	// Earliest of the request's expiry, idle timeout and pending flush
	static std::chrono::steady_clock::time_point due_of (request_entry const &);

	// clang-format off
	class tag_request {};
	class tag_due {};
	class tag_hash {};

	using ordered_requests = boost::multi_index_container<request_entry,
	mi::indexed_by<
		mi::ordered_unique<mi::tag<tag_request>,
			mi::composite_key<request_entry,
				mi::member<request_entry, std::shared_ptr<nano::transport::channel>, &request_entry::channel>,
				mi::member<request_entry, id_t, &request_entry::id>>>,
		mi::ordered_non_unique<mi::tag<tag_due>,
			mi::member<request_entry, std::chrono::steady_clock::time_point, &request_entry::due>>
	>>;

	using ordered_pending = boost::multi_index_container<pending_entry,
	mi::indexed_by<
		mi::ordered_unique<mi::tag<tag_request>,
			mi::composite_key<pending_entry,
				mi::member<pending_entry, std::shared_ptr<nano::transport::channel>, &pending_entry::channel>,
				mi::member<pending_entry, id_t, &pending_entry::id>,
				mi::member<pending_entry, nano::block_hash, &pending_entry::hash>>>,
		mi::hashed_non_unique<mi::tag<tag_hash>,
			mi::member<pending_entry, nano::block_hash, &pending_entry::hash>>
	>>;
	// clang-format on

	request_limits const limits;
	ordered_requests requests; // Tracked requests, ordered by what is due first
	ordered_pending pending; // Rows of (request, hash) still waiting, found by hash when a vote arrives
};
}
