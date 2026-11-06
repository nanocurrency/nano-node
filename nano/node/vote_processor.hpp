#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/vote_context.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/common.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <unordered_set>

namespace nano
{
class vote_processor_config final
{
public:
	nano::error serialize (nano::tomlconfig & toml) const;
	nano::error deserialize (nano::tomlconfig & toml);

public:
	bool enable{ true };

	size_t max_pr_queue{ 256 }; // Queue slots per channel and tier for votes of tiered representatives
	size_t max_non_pr_queue{ 32 }; // Queue slots per channel for votes of untiered representatives
	size_t pr_priority{ 3 }; // Base of the queue priority by tier: the cube for tier 3, the square for tier 2, the base for tier 1
	size_t threads{ std::clamp (nano::hardware_concurrency () / 2, 1u, 4u) }; // Processing threads, each takes its own batches
	size_t batch_size{ 1024 }; // Votes a thread takes off the queue at once
};

/** A vote that passed signature verification, with the router's aggregate code for its hashes */
struct processed_vote final
{
	nano::vote_context context;
	nano::vote_code code;
};

using processed_votes_batch = std::deque<processed_vote>;

class vote_processor final
{
public:
	vote_processor (vote_processor_config const &, nano::vote_router &, nano::node_observers &, nano::stats &, nano::node_flags &, nano::logger &, nano::online_reps &, nano::rep_crawler &, nano::ledger &);
	~vote_processor ();

	void start ();
	void stop ();

	// Queue the vote for a processing thread
	// @return false if the queue for its channel and tier is full
	bool vote (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &, nano::vote_source = nano::vote_source::live);
	// Verify and route the vote on the calling thread, returns the aggregate code of its hashes
	nano::vote_code vote_blocking (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &, nano::vote_source = nano::vote_source::live);
	// Same for a vote whose context was looked up already
	nano::vote_code vote_blocking (nano::vote_context const &);

	// Look up the vote's context: its representative's weight, and its tier and principal status against the trended online stake
	nano::vote_context context (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &, nano::vote_source = nano::vote_source::live) const;

	// Queue the hash for a lookup of its cached votes
	void trigger (nano::block_hash const & hash);

	// Votes waiting in the queue
	std::size_t size () const;
	bool empty () const;

	nano::container_info container_info () const;

	std::atomic<uint64_t> total_processed{ 0 }; // Votes taken off the queue so far, verified or not

public: // Events
	// Fired once per batch from the processing thread, after every vote in it went through the router; invalid votes are left out
	nano::observer_set<processed_votes_batch const &> batch_processed;

private: // Dependencies
	vote_processor_config const & config;
	nano::vote_router & vote_router;
	nano::node_observers & observers;
	nano::stats & stats;
	nano::node_flags & flags;
	nano::logger & logger;
	nano::online_reps & online_reps;
	nano::rep_crawler & rep_crawler;
	nano::ledger & ledger;

private:
	// Thread loop, takes batches while the queue has votes
	void run ();
	// Verify and route one batch outside the lock, then hand it to the batch observers
	void run_batch (nano::unique_lock<nano::mutex> &);

private:
	using entry_t = nano::vote_context;
	// Votes keyed by tier and channel, drained round robin with the tier priority
	nano::fair_queue<entry_t, nano::rep_tier, std::shared_ptr<nano::transport::channel>> queue;

private:
	bool stopped{ false }; // Set by stop ()
	nano::condition_variable condition; // Wakes the threads for new votes or stop
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_processor) }; // Guards the queue
	std::vector<std::thread> threads; // Processing threads

	nano::interval log_interval; // Rate limits the queue pressure log
};

class vote_cache_processor_config final
{
public:
	bool enable{ true }; // Not in the config file, elections rely on it for votes that arrived before they started
	std::size_t max_triggered{ 16384 }; // Hashes waiting for a lookup, the oldest is dropped beyond this
};

class vote_cache_processor final
{
public:
	vote_cache_processor (vote_cache_processor_config const &, nano::vote_router &, nano::vote_cache &, nano::vote_processor &, nano::stats &, nano::logger &);
	~vote_cache_processor ();

	void start ();
	void stop ();

	// Queue the hash for a lookup of its cached votes
	void trigger (nano::block_hash const & hash);

	// Hashes waiting for their lookup
	std::size_t size () const;
	bool empty () const;

	nano::container_info container_info () const;

private:
	// Thread loop, looks up triggered hashes in batches
	void run ();
	// Replay the cached votes of one batch of hashes through the router
	void run_batch (nano::unique_lock<nano::mutex> &);

private: // Dependencies
	vote_cache_processor_config const & config;
	nano::vote_router & vote_router;
	nano::vote_cache & vote_cache;
	nano::vote_processor & vote_processor;
	nano::stats & stats;
	nano::logger & logger;

private:
	std::deque<nano::block_hash> triggered; // Hashes waiting for a lookup, oldest first

	bool stopped{ false }; // Set by stop ()
	nano::condition_variable condition; // Wakes the thread for new hashes or stop
	mutable nano::mutex mutex; // Guards the triggered hashes
	std::thread thread; // Lookup thread

	nano::interval log_interval; // Rate limits the backlog log
};
}
