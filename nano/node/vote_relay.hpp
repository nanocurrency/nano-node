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
	std::chrono::milliseconds request_timeout{ std::chrono::seconds{ 5 } }; // Time to wait for representative votes before finishing a request
	std::size_t max_requests{ 1024 * 4 }; // Requests waiting for representative votes at once
	std::size_t channel_limit{ default_max_outstanding }; // Requests per channel, queued or waiting for votes
	std::size_t batch_size{ 64 }; // Requests served per batch
};

/**
 * Serves vote relay requests, allowing peers to obtain votes from representatives they cannot reach directly.
 * Votes are first looked up in the local vote cache, representatives are queried with confirm_req for anything missing.
 * Every request is terminated with an empty ack once no more votes will be sent for it.
 * Every reply is sent from the relay's own thread, the vote processor threads only match votes to waiting requests.
 */
class vote_relay final
{
public:
	vote_relay (vote_relay_config const &, nano::vote_cache &, nano::vote_processor &, nano::rep_crawler &, nano::network_constants const &, nano::stats &, nano::logger &);
	~vote_relay ();

	void start ();
	void stop ();

	// Queue an incoming relay request for processing
	// @return false if the request was dropped
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
	// Thread loop, answers completed and expired requests and serves batches of new ones
	void run ();
	// Serve one batch of queued requests outside the lock
	void run_batch (nano::unique_lock<nano::mutex> &);

	// Serve a request from the vote cache and query reps for anything missing
	void process (nano::messages::vote_relay_req const &, std::shared_ptr<nano::transport::channel> const &);
	// Match a batch of verified votes to pending requests, completed requests are queued for the thread to answer
	void process_votes (nano::processed_votes_batch const &);
	// Send votes batched into acks, terminated with an empty ack
	void send_reply (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t, std::deque<std::shared_ptr<nano::vote>> const &);
	// Send votes batched into acks without the terminating empty ack
	void send_votes (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t, std::deque<std::shared_ptr<nano::vote>> const &);
	// Send one ack and announce it on on_reply
	void send_ack (std::shared_ptr<nano::transport::channel> const &, nano::messages::vote_relay_ack const &);

private:
	// Incoming requests keyed by channel, drained round robin
	nano::fair_queue<nano::messages::vote_relay_req, nano::no_value, std::shared_ptr<nano::transport::channel>> queue;

	// Requests waiting for representative votes
	nano::vote_relay_index index;

	// Requests completed by a vote, waiting for the thread to answer them
	// Bounded by the index capacity plus one batch: a request completes once, and only the thread inserts, after draining this
	std::deque<vote_relay_index::reply> completed;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_relay) };
	std::thread thread; // Processing thread, started only when enabled

	nano::interval log_interval; // Rate limits the queue pressure log
};
}
