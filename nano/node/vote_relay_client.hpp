#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/secure/fwd.hpp>

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
	// Maximum outstanding requests per relay before it stops receiving new ones, defaults to the relay's per-peer limit
	std::size_t max_outstanding{ nano::vote_relay_config::default_max_outstanding };
};

/**
 * Tracks vote relay requests issued by this node until their terminating ack or deadline, and the representatives whose votes relays delivered.
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
		std::chrono::steady_clock::time_point deadline; // Expiry of the request
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
	std::deque<entry> evict (std::chrono::steady_clock::time_point now);
	// Drop all requests, relayed representatives are kept
	void clear ();

	// Number of unanswered requests sent to the channel
	std::size_t outstanding (std::shared_ptr<nano::transport::channel> const &) const;
	// Number of requests still waiting for their terminating ack
	std::size_t outstanding () const;
	// Whether no request is tracked
	bool empty () const;

	// A representative whose vote a relay delivered
	struct relayed_rep
	{
		nano::account account; // Tracked once, however many relays deliver its votes
		std::shared_ptr<nano::transport::channel> relay; // Relay that delivered the latest vote
		std::chrono::steady_clock::time_point time; // When that vote arrived
	};

	// Record a vote of the representative delivered by the relay
	void observe (nano::account const &, std::shared_ptr<nano::transport::channel> const & relay, std::chrono::steady_clock::time_point now);
	// Forget representatives without a relayed vote since the cutoff
	void trim (std::chrono::steady_clock::time_point cutoff);
	// Representatives with a relayed vote since the cutoff
	std::deque<nano::account> relayed (std::chrono::steady_clock::time_point cutoff) const;
	// Number of representatives tracked, stale ones included until trimmed
	std::size_t relayed_size () const;

private:
	// clang-format off
	class tag_id {};
	class tag_channel {};
	class tag_deadline {};
	class tag_account {};
	class tag_time {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<entry, id_t, &entry::id>>,
		mi::hashed_non_unique<mi::tag<tag_channel>,
			mi::member<entry, std::shared_ptr<nano::transport::channel>, &entry::channel>>,
		mi::ordered_non_unique<mi::tag<tag_deadline>,
			mi::member<entry, std::chrono::steady_clock::time_point, &entry::deadline>>
	>>;

	using ordered_relayed = boost::multi_index_container<relayed_rep,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<relayed_rep, nano::account, &relayed_rep::account>>,
		mi::ordered_non_unique<mi::tag<tag_time>,
			mi::member<relayed_rep, std::chrono::steady_clock::time_point, &relayed_rep::time>>
	>>;
	// clang-format on

	ordered_entries entries; // Tracked requests
	ordered_relayed relayed_reps; // Representatives seen through relays, stale ones stay until trimmed
};

/**
 * Requester side of the vote relay protocol.
 * Sends vote_relay_req messages to relay peers and queues the votes from their acks for processing as relay-sourced.
 * Requests are tracked until their terminating empty ack or the request timeout, acks matching no request are dropped.
 * A relay with too many outstanding requests stops receiving new ones until it answers them or they expire.
 * Representatives whose votes arrived through a relay are remembered for as long as online representatives are, which puts a weight on what relays can reach.
 */
class vote_relay_client final
{
public:
	// Request id, drawn at random for every request
	using id_t = vote_relay_client_index::id_t;
	// Blocks to request votes for, as the request message carries them
	using roots_hashes_t = std::vector<std::pair<nano::block_hash, nano::root>>;

public:
	vote_relay_client (vote_relay_client_config const &, nano::vote_processor &, nano::network &, nano::network_params const &, nano::stats &, nano::logger &);

	// Drop all tracked requests, further requests and acks are refused
	void stop ();

	// Peers advertising the vote relay capability that can accept a request, in random order
	std::deque<std::shared_ptr<nano::transport::channel>> relays (std::size_t max_count) const;

	// Ask a relay for votes from the given representatives on the given blocks, registers the request for ack matching
	// @return false if the client is stopped, the relay channel is full, the relay or this node has too many outstanding requests, or the send failed
	bool request (std::shared_ptr<nano::transport::channel> const & relay, std::deque<nano::account> const & reps, roots_hashes_t const & roots_hashes, nano::vote_finality);

	// Consume an ack, its votes are queued for processing as relay-sourced
	// @return false if the client is stopped or the ack matches no tracked request from that relay
	bool process (nano::messages::vote_relay_ack const &, std::shared_ptr<nano::transport::channel> const &);

	// Number of unanswered requests sent to the relay
	std::size_t outstanding (std::shared_ptr<nano::transport::channel> const &) const;
	// Number of requests still waiting for their terminating ack
	std::size_t outstanding () const;

	// Record a validated vote of the representative that the relay delivered
	void observe (nano::account const &, std::shared_ptr<nano::transport::channel> const & relay);
	// Representatives whose votes a relay delivered within the online window
	std::deque<nano::account> relayed () const;

	// Whether no request is tracked
	bool empty () const;

	// Sizes of the tracked state, for diagnostics
	nano::container_info container_info () const;

private: // Dependencies
	vote_relay_client_config const & config;
	nano::vote_processor & vote_processor;
	nano::network & network;
	nano::network_params const & network_params;
	nano::stats & stats;
	nano::logger & logger;

private:
	// Random request id, never zero
	static id_t next_id ();

	// Relayed representatives are kept as long as online representatives are
	std::chrono::steady_clock::time_point relayed_cutoff (std::chrono::steady_clock::time_point now) const;

	vote_relay_client_index index; // Requests and relayed representatives

	bool stopped{ false }; // Once set, requests and acks are refused
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_relay_client) }; // Guards the index
};
}
