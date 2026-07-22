#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/vote_relay_index.hpp>

#include <condition_variable>
#include <memory>
#include <thread>

namespace nano
{
class vote_relay_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ false };
	std::chrono::milliseconds request_timeout{ std::chrono::seconds{ 5 } };
	std::size_t max_requests{ 1024 * 4 };
	std::size_t channel_limit{ 32 };
	std::size_t batch_size{ 64 };
};

/**
 * Serves vote relay requests, allowing peers to obtain votes from representatives they cannot reach directly.
 * Votes are first looked up in the local vote cache, representatives are queried with confirm_req for anything missing.
 * Every request is terminated with an empty ack once no more votes will be sent for it.
 */
class vote_relay final
{
public:
	vote_relay (vote_relay_config const &, nano::vote_cache &, nano::vote_router &, nano::rep_crawler &, nano::network_constants const &, nano::stats &, nano::logger &);
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
	nano::rep_crawler & rep_crawler;
	nano::network_constants const & network_constants;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();
	void run_batch (nano::unique_lock<nano::mutex> &);
	
	// Serve a request from the vote cache and query reps for anything missing
	void process (nano::messages::vote_relay_req const &, std::shared_ptr<nano::transport::channel> const &);
	// Offer a processed vote to pending requests, flushing any completed ones
	void process_vote (std::shared_ptr<nano::vote> const &);
	// Send votes batched into acks, terminated with an empty ack
	void send_reply (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t, std::vector<std::shared_ptr<nano::vote>> const &);
	// Send votes batched into acks without the terminating empty ack
	void send_votes (std::shared_ptr<nano::transport::channel> const &, vote_relay_index::id_t, std::vector<std::shared_ptr<nano::vote>> const &);
	void send_ack (std::shared_ptr<nano::transport::channel> const &, nano::messages::vote_relay_ack const &);

private:
	nano::fair_queue<nano::messages::vote_relay_req, nano::no_value, std::shared_ptr<nano::transport::channel>> queue;

	nano::vote_relay_index index;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_relay) };
	std::thread thread;

	nano::interval log_interval;
};
}
