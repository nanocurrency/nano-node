#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_set>

using namespace std::chrono_literals;

namespace mi = boost::multi_index;

/**
 * Rebroadcasts blocks that are in active elections to help propagate them across the network.
 * Blocks are queued for rebroadcast when elections start, become stale and at regular intervals as configured by block_broadcast_interval network parameter.
 * A cooldown mechanism prevents the same block from being rebroadcast too frequently.
 */
namespace nano
{
class block_rebroadcaster_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ true };
	size_t max_queue{ 1024 * 4 }; // Maximum number of blocks to keep in queue for processing
	size_t max_history{ 1024 * 128 }; // Maximum number of recently broadcast hashes to keep
	std::chrono::milliseconds rebroadcast_cooldown{ 60s }; // Minimum time between rebroadcasts for the same block
};

class block_rebroadcaster_index
{
public:
	explicit block_rebroadcaster_index (block_rebroadcaster_config const &);

	// Returns true if block should be rebroadcast, records the rebroadcast
	bool check_and_record (nano::block_hash const & hash, std::chrono::steady_clock::time_point now);
	size_t cleanup (std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	bool contains (nano::block_hash const & hash) const;
	size_t size () const;

private:
	block_rebroadcaster_config const & config;

	struct entry
	{
		nano::block_hash hash;
		std::chrono::steady_clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_hash {};

	using ordered_history = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, nano::block_hash, &entry::hash>>
	>>;
	// clang-format on

	ordered_history history;
};

class block_rebroadcaster final
{
public:
	block_rebroadcaster (block_rebroadcaster_config const &, nano::node_flags const &, nano::active_elections &, nano::network &, nano::stats &, nano::logger &);
	~block_rebroadcaster ();

	void start ();
	void stop ();

	bool push (std::shared_ptr<nano::block> const &);

	nano::container_info container_info () const;

public: // Dependencies
	block_rebroadcaster_config const & config;
	nano::node_flags const & flags;
	nano::active_elections & active;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();
	std::shared_ptr<nano::block> next ();
	void cleanup ();
	size_t broadcast (std::shared_ptr<nano::block> const &);
	bool check_capacity () const;

private:
	// Simple FIFO queue of blocks to rebroadcast
	std::deque<std::shared_ptr<nano::block>> queue;
	boost::unordered_flat_set<nano::block_hash, std::hash<nano::block_hash>> queue_dedup; // Avoid duplicates in the queue

	nano::locked<block_rebroadcaster_index> rebroadcasts;

private:
	bool stopped{ false };
	std::condition_variable condition;
	mutable std::mutex mutex;
	std::thread thread;

	nano::interval cleanup_interval;
	nano::interval log_interval;
	nano::interval capacity_warning_interval;
};
}
