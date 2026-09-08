#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class vote_relay_client_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	// Time to wait for the terminating ack before a request is considered lost, a safety net above the relay's own timeout
	std::chrono::milliseconds request_timeout{ std::chrono::seconds{ 60 } };
	// Maximum number of requests waiting for their terminating ack across all relays
	std::size_t max_requests{ 1024 * 4 };
	// Maximum outstanding requests per relay before it stops receiving new ones
	std::size_t max_outstanding{ 64 };
};

/**
 * Tracks vote relay requests issued by this node until their terminating ack or deadline.
 * Pure state, thread safety and time are the responsibility of the caller.
 */
class vote_relay_client_index
{
public:
	// Request id as carried by vote_relay_req and vote_relay_ack
	using id_t = nano::messages::vote_relay_req::id_t;

	// A request sent to a relay
	struct entry
	{
		id_t id; // Unique among tracked requests, the relay echoes it in every ack
		std::shared_ptr<nano::transport::channel> channel; // Relay the request was sent to
		std::chrono::steady_clock::time_point deadline;
		std::size_t hashes{ 0 }; // Hashes requested
		std::size_t votes{ 0 }; // Votes received so far
	};

	// Track a request
	// @return false if the id is already in use
	bool insert (entry const &);
	// Match an ack to its request, empty when the id is unknown or the ack came from a different channel
	std::optional<entry> find (id_t, std::shared_ptr<nano::transport::channel> const &) const;
	// Record votes received for a request
	void received (id_t, std::size_t votes);
	// Stop tracking a request
	// @return false if the id is unknown
	bool erase (id_t);
	// Remove and return requests past their deadline
	std::vector<entry> evict (std::chrono::steady_clock::time_point now);
	// Drop all requests
	void clear ();

	// Number of unanswered requests sent to the channel
	std::size_t outstanding (std::shared_ptr<nano::transport::channel> const &) const;

	// Number of tracked requests
	std::size_t size () const;
	// Whether no request is tracked
	bool empty () const;

private:
	// clang-format off
	class tag_id {};
	class tag_channel {};
	class tag_deadline {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<entry, id_t, &entry::id>>,
		mi::hashed_non_unique<mi::tag<tag_channel>,
			mi::member<entry, std::shared_ptr<nano::transport::channel>, &entry::channel>>,
		mi::ordered_non_unique<mi::tag<tag_deadline>,
			mi::member<entry, std::chrono::steady_clock::time_point, &entry::deadline>>
	>>;
	// clang-format on

	ordered_entries entries; // Tracked requests
};

/**
 * Requester side of the vote relay protocol.
 * Sends vote_relay_req messages to relay peers and queues the votes from their acks for processing as relay-sourced.
 * Requests are tracked until their terminating empty ack or the request timeout, acks matching no request are dropped.
 * A relay with too many outstanding requests stops receiving new ones until it answers them or they expire.
 */
class vote_relay_client final
{
public:
	// Request id, drawn at random for every request
	using id_t = vote_relay_client_index::id_t;
	// Blocks to request votes for, as the request message carries them
	using roots_hashes_t = std::vector<std::pair<nano::block_hash, nano::root>>;

public:
	vote_relay_client (vote_relay_client_config const &, nano::vote_processor &, nano::network &, nano::network_constants const &, nano::stats &, nano::logger &);

	// Drop all tracked requests, further requests and acks are refused
	void stop ();

	// Peers advertising the vote relay capability that can accept a request, in random order
	std::deque<std::shared_ptr<nano::transport::channel>> relays (std::size_t max_count) const;

	// Ask a relay for votes from the given representatives on the given blocks, registers the request for ack matching
	// @return false if the client is stopped, the relay channel is full, the relay or this node has too many outstanding requests, or the send failed
	bool request (std::shared_ptr<nano::transport::channel> const & relay, std::deque<nano::account> const & reps, roots_hashes_t const & roots_hashes, bool include_non_final);

	// Consume an ack, its votes are queued for processing as relay-sourced
	// @return false if the client is stopped or the ack matches no tracked request from that relay
	bool process (nano::messages::vote_relay_ack const &, std::shared_ptr<nano::transport::channel> const &);

	// Number of unanswered requests sent to the relay
	std::size_t outstanding (std::shared_ptr<nano::transport::channel> const &) const;

	// Number of tracked requests
	std::size_t size () const;
	// Whether no request is tracked
	bool empty () const;

	// Sizes of the tracked state, for diagnostics
	nano::container_info container_info () const;

private: // Dependencies
	vote_relay_client_config const & config;
	nano::vote_processor & vote_processor;
	nano::network & network;
	nano::network_constants const & network_constants;
	nano::stats & stats;
	nano::logger & logger;

private:
	// Random request id, never zero
	static id_t next_id ();

	vote_relay_client_index index; // Tracked requests

	bool stopped{ false }; // Once set, requests and acks are refused
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_relay_client) }; // Guards the index
};
}
