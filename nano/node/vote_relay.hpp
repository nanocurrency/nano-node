#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_relay_index.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

namespace nano
{
class vote_relay_config final
{
public:
	// Default per-peer limit, shared by channel_limit here and the requester's max_outstanding
	static std::size_t constexpr default_max_outstanding = 64;

public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ false };
	std::chrono::milliseconds request_timeout{ std::chrono::seconds{ 30 } }; // Longest a request is kept open for representative votes
	std::chrono::milliseconds idle_timeout{ std::chrono::seconds{ 15 } }; // A request without a new vote for this long is finished
	std::chrono::milliseconds flush_interval{ std::chrono::seconds{ 3 } }; // Longest a received vote waits for others before it is sent
	std::size_t flush_threshold{ 8 }; // Unsent votes that are sent without waiting for the flush interval
	std::size_t max_requests{ 1024 * 4 }; // Requests waiting for representative votes at once
	std::size_t channel_limit{ default_max_outstanding }; // Requests per channel, queued or waiting for votes
	std::size_t batch_size{ 64 }; // Requests served per batch
	std::size_t max_reps{ 64 }; // Maximum representatives per request, larger requests are dropped
};

/**
 * Serves vote relay requests, allowing peers to obtain votes from representatives they cannot reach directly.
 * Votes are first looked up in the local vote cache, representatives are queried with confirm_req for anything missing.
 * Votes are forwarded as they arrive, in acks of flush_threshold votes or after flush_interval, and every request ends with an ack flagged as completed, carrying the last votes or empty, once no more votes will be sent for it:
 * when every queried representative answered, after idle_timeout without a vote, or at request_timeout.
 * A request the relay does not take gets its terminator with the refused flag set, so the requester can tell a refusal from an answer with no votes.
 * Every reply is sent from the relay's own thread, the vote processor threads only match votes to waiting requests.
 */
class vote_relay final
{
public:
	vote_relay (vote_relay_config const &, nano::vote_cache &, nano::vote_processor &, nano::rep_crawler &, nano::network_constants const &, nano::stats &, nano::logger &);
	~vote_relay ();

	void start ();
	void stop ();

	// Queue an incoming relay request for the thread, one the queue has no room for still gets its terminating ack from it
	// @return false if the relay is disabled or the queue has no room
	bool request (nano::messages::vote_relay_req const &, std::shared_ptr<nano::transport::channel> const &);

	std::size_t size () const;
	bool empty () const;

	nano::container_info container_info () const;

public: // Events
	// Fired for every outgoing ack, intended for testing
	nano::observer_set<nano::messages::vote_relay_ack, std::shared_ptr<nano::transport::channel>> on_reply;

private: // Dependencies
	vote_relay_config const & config;
	nano::vote_cache & vote_cache;
	nano::vote_processor & vote_processor;
	nano::rep_crawler & rep_crawler;
	nano::network_constants const & network_constants;
	nano::stats & stats;
	nano::logger & logger;

private:
	// Thread loop, sends the replies that are ready or due and serves batches of new requests
	void run ();
	// Serve one batch of queued requests outside the lock
	void run_batch (nano::unique_lock<nano::mutex> &);
	// Queue the terminating ack for a request the queue had no room for, called under the lock
	void refuse (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t);

	// Serve a request from the vote cache and query reps for anything missing, or refuse one the relay does not accept
	void process (nano::messages::vote_relay_req const &, std::shared_ptr<nano::transport::channel> const &);
	// Match a batch of verified votes to pending requests, the flushes and completions they produce are queued for the thread to send
	void process_votes (nano::processed_votes_batch const &);
	// Send a reply the index handed out: its votes, followed by the terminator unless it is a flush
	void answer (vote_relay_index::reply const &);
	// Send votes batched into acks, the last one flagged as completed, an empty one when there are none
	void send_reply (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t, std::deque<std::shared_ptr<nano::vote>> const &);
	// Send the terminator of a request the relay did not take, flagged as refused
	void send_refusal (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t);
	// Send votes batched into acks that leave the request open
	void send_votes (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t, std::deque<std::shared_ptr<nano::vote>> const &);
	// Send one ack and announce it on on_reply
	void send_ack (std::shared_ptr<nano::transport::channel> const &, nano::messages::vote_relay_ack const &);

private:
	// A refused request and the channel its terminating ack goes to
	struct refused_request
	{
		std::shared_ptr<nano::transport::channel> channel;
		vote_relay_index::id_t id;
	};

private:
	// Incoming requests keyed by channel, drained round robin
	nano::fair_queue<nano::messages::vote_relay_req, nano::no_value, std::shared_ptr<nano::transport::channel>> queue;

	// Requests waiting for representative votes
	nano::vote_relay_index index;

	// Flushes and completions the processor threads produced, waiting for the thread to send them
	// Bounded by the votes the index holds: a flush empties its request's buffer, and only the thread inserts, after draining this
	std::deque<vote_relay_index::reply> ready;

	// Requests the queue had no room for, waiting for their terminating ack
	// Nothing else bounds them, so they are capped at max_requests and dropped beyond it
	std::deque<refused_request> refused;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_relay) };
	std::thread thread; // Processing thread, started only when enabled

	nano::interval log_interval; // Rate limits the queue pressure log
};
}
