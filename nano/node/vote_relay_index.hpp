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
 * Pure state, thread safety and time are the responsibility of the caller.
 */
class vote_relay_index
{
public:
	using id_t = uint64_t;

	// Representatives that still need to answer for a single hash
	struct want
	{
		nano::block_hash hash;
		nano::root root;
		std::vector<nano::account> reps;
	};

	// Upstream query to send to a single representative
	struct query
	{
		nano::account rep;
		std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	};

	// Reply for a tracked request with the votes accumulated so far
	struct reply
	{
		std::shared_ptr<nano::transport::channel> channel;
		id_t id;
		std::vector<std::shared_ptr<nano::vote>> votes;
	};

public:
	// Track a request, replacing any previous one with the same channel and id
	// @return queries for (hash, rep) pairs that are not already in flight
	std::vector<query> insert (std::shared_ptr<nano::transport::channel> const &, id_t, std::vector<want> const &, bool include_non_final, std::chrono::steady_clock::time_point deadline);

	// Offer a processed vote to pending requests
	// @return requests fully satisfied by this vote
	std::vector<reply> vote (std::shared_ptr<nano::vote> const &);

	// Remove and return requests past their deadline, accumulated votes may be partial or empty
	std::vector<reply> evict (std::chrono::steady_clock::time_point now);

	bool erase (std::shared_ptr<nano::transport::channel> const &, id_t);
	void clear ();

	std::optional<std::chrono::steady_clock::time_point> next_deadline () const;

	std::size_t size () const;
	std::size_t pending_size () const;
	bool empty () const;

private:
	struct request_entry
	{
		std::shared_ptr<nano::transport::channel> channel;
		id_t id;
		bool include_non_final;
		std::chrono::steady_clock::time_point deadline;
		std::vector<std::shared_ptr<nano::vote>> votes; // Votes accumulated so far
		std::size_t pending; // Number of live pending rows
	};

	// One row per (request, hash) pair, removed once all wanted reps have answered
	struct pending_entry
	{
		std::shared_ptr<nano::transport::channel> channel;
		id_t id;
		nano::block_hash hash;
		nano::root root;
		std::vector<nano::account> reps; // Reps that have not answered yet
		bool include_non_final;
	};

	// Union of reps with an in-flight query for this hash
	std::unordered_set<nano::account> in_flight_reps (nano::block_hash const &) const;
	void erase_pending (std::shared_ptr<nano::transport::channel> const &, id_t);

	// clang-format off
	class tag_request {};
	class tag_deadline {};
	class tag_hash {};

	using ordered_requests = boost::multi_index_container<request_entry,
	mi::indexed_by<
		mi::ordered_unique<mi::tag<tag_request>,
			mi::composite_key<request_entry,
				mi::member<request_entry, std::shared_ptr<nano::transport::channel>, &request_entry::channel>,
				mi::member<request_entry, id_t, &request_entry::id>>>,
		mi::ordered_non_unique<mi::tag<tag_deadline>,
			mi::member<request_entry, std::chrono::steady_clock::time_point, &request_entry::deadline>>
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

	ordered_requests requests;
	ordered_pending pending;
};
}
