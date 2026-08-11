#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/secure/voting_policy.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace nano
{
class vote_generator_config final
{
public:
	nano::error serialize (nano::tomlconfig &) const;
	nano::error deserialize (nano::tomlconfig &);

public:
	size_t max_queue{ 1024 * 32 };
	size_t batch_size{ 256 };
	size_t normal_threads{ 2 };
	std::chrono::milliseconds delay{ 100ms };
};

/**
 * Fair queue over balance buckets with deduplication by root.
 * Replaces existing entry when a new hash arrives for the same root.
 * Holds candidates for vote generation.
 */
class vote_generator_index final
{
public:
	using entry = std::pair<nano::qualified_root, nano::block_hash>;

	explicit vote_generator_index (size_t max_size_per_bucket);

	/// Push a request. Returns true if added or replaced, false if duplicate (same root+hash) or queue full
	bool push (nano::qualified_root const & root, nano::block_hash const & hash, nano::bucket_index bucket);

	/// Remove and return up to `count` valid entries (stale entries from replacements are skipped)
	std::deque<entry> next_batch (size_t count);

	/// Number of pending roots
	size_t size () const;
	/// True when the underlying queue holds no entries, stale or valid
	bool empty () const;

private:
	nano::fair_queue<entry, nano::bucket_index> queue;
	std::unordered_map<nano::qualified_root, nano::block_hash> dedup;
};

/**
 * Holds vote permits in FIFO order with deduplication by root
 * Provides batch extraction from the front
 */
class vote_broadcast_index final
{
public:
	explicit vote_broadcast_index (size_t max_size);

	/// Insert a permit keyed by root. If same root+hash exists, the new one is dropped. If same root but different hash (conflict), the old entry is replaced.
	bool push (nano::qualified_root const & root, nano::vote_permit permit);

	/// Remove the entry for the given root. Returns true if erased
	bool erase (nano::qualified_root const & root);

	/// Remove and return up to `count` entries from the front (FIFO order)
	std::vector<nano::vote_permit> next_batch (size_t count);

	size_t size () const;
	bool empty () const;

private:
	size_t const max_size;

	struct entry
	{
		nano::qualified_root root;
		nano::vote_permit permit;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, nano::qualified_root, &entry::root>>
	>>;
	// clang-format on

	ordered_entries entries;
};

/**
 * Threaded queue that wraps vote_generator_index.
 * Worker threads pull batches and invoke the process_batch callback.
 */
class vote_generator_verifier final
{
public:
	using entry = vote_generator_index::entry;

public:
	vote_generator_verifier (size_t max_size_per_bucket, size_t batch_size, size_t thread_count, nano::thread_role::name thread_role);
	~vote_generator_verifier ();

	void start ();
	void stop ();

	bool push (nano::qualified_root const &, nano::block_hash const &, nano::bucket_index);

	size_t size () const;
	bool empty () const;

public: // Callbacks
	/// Called on worker threads with a batch of entries.
	std::function<void (std::deque<entry>)> process_batch;

private:
	void run ();

	size_t const batch_size;
	size_t const thread_count;
	nano::thread_role::name const thread_role;

	vote_generator_index index;

	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::vector<std::thread> threads;
	bool stopped{ false };
};

/**
 * Threaded queue that wraps vote_broadcast_index.
 * A single thread pulls batches when a size threshold is reached or a timer expires,
 * then invokes the broadcast_batch callback.
 */
class vote_generator_broadcaster final
{
public:
	using entry = nano::vote_permit;

public:
	vote_generator_broadcaster (size_t max_size, size_t batch_threshold, std::chrono::milliseconds delay, nano::thread_role::name thread_role);
	~vote_generator_broadcaster ();

	void start ();
	void stop ();

	bool push (nano::qualified_root const &, nano::vote_permit const &);

	size_t size () const;
	bool empty () const;

public: // Callbacks
	/// Called on broadcast thread with a batch of permits.
	std::function<void (std::vector<nano::vote_permit>)> broadcast_batch;

	/// Called before extracting a batch. Returns false to defer broadcasting.
	std::function<bool ()> check_capacity;

private:
	void run ();
	bool predicate () const;

	size_t const batch_threshold;
	std::chrono::milliseconds const delay;
	nano::thread_role::name const thread_role;

	vote_broadcast_index index;

	std::chrono::steady_clock::time_point last_broadcast{ std::chrono::steady_clock::now () };

	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::thread thread;
	bool stopped{ false };
};

/**
 * Unified vote generator for both normal and final votes
 */
class vote_generator final
{
public:
	vote_generator (vote_generator_config const &, nano::voting_policy &, nano::ledger &, nano::wallet::wallets &, nano::vote_processor &, nano::network &, nano::stats &, nano::logger &, std::shared_ptr<nano::transport::channel> loopback);
	~vote_generator ();

	void start ();
	void stop ();

	void vote (nano::qualified_root const &, nano::block_hash const &, nano::bucket_index, nano::vote_type);
	void vote_normal (nano::qualified_root const &, nano::block_hash const &, nano::bucket_index);
	void vote_final (nano::qualified_root const &, nano::block_hash const &, nano::bucket_index);

	nano::container_info container_info () const;

private:
	void process_normal (std::deque<vote_generator_verifier::entry> batch);
	void process_final (std::deque<vote_generator_verifier::entry> batch);
	void broadcast_normal (std::vector<nano::vote_permit> batch);
	void broadcast_final (std::vector<nano::vote_permit> batch);
	void broadcast_vote (std::shared_ptr<nano::vote> const & vote) const;
	bool check_normal_capacity ();
	bool check_final_capacity ();

private: // Dependencies
	vote_generator_config const & config;
	nano::voting_policy & policy;
	nano::ledger & ledger;
	nano::wallet::wallets & wallets;
	nano::vote_processor & vote_processor;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

	std::shared_ptr<nano::transport::channel> inproc_channel;

private:
	vote_generator_verifier normal_verifier;
	vote_generator_broadcaster normal_broadcaster;

	vote_generator_verifier final_verifier;
	vote_generator_broadcaster final_broadcaster;

	nano::interval normal_capacity_log_interval;
	nano::interval final_capacity_log_interval;
};
}
