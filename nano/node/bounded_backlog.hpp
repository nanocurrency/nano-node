#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/lib/observer_set.hpp>
#include <celerix/lib/rate_limiting.hpp>
#include <celerix/lib/thread_pool.hpp>
#include <celerix/node/bucketing.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <unordered_map>

namespace mi = boost::multi_index;

namespace celerix
{
class backlog_index
{
public:
	struct priority_key
	{
		celerix::bucket_index bucket;
		celerix::priority_timestamp priority;

		auto operator<=> (priority_key const &) const = default;
	};

	struct entry
	{
		celerix::block_hash hash;
		celerix::account account;
		celerix::bucket_index bucket;
		celerix::priority_timestamp priority;

		backlog_index::priority_key priority_key () const
		{
			return { bucket, priority };
		}
	};

public:
	backlog_index () = default;

	bool insert (celerix::block const & block, celerix::bucket_index, celerix::priority_timestamp);

	bool erase (celerix::account const & account);
	bool erase (celerix::block_hash const & hash);

	using filter_callback = std::function<bool (celerix::block_hash const &)>;
	std::deque<celerix::block_hash> top (celerix::bucket_index, size_t count, filter_callback const &) const;

	std::deque<celerix::block_hash> next (celerix::block_hash last, size_t count) const;

	bool contains (celerix::block_hash const & hash) const;
	size_t size () const;
	size_t size (celerix::bucket_index) const;

	celerix::container_info container_info () const;

private:
	// clang-format off
	class tag_hash {};
	class tag_hash_ordered {};
	class tag_account {};
	class tag_priority {};

	using ordered_blocks = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>, // Allows for fast lookup
			mi::member<entry, celerix::block_hash, &entry::hash>>,
		mi::ordered_unique<mi::tag<tag_hash_ordered>, // Allows for sequential scan
			mi::member<entry, celerix::block_hash, &entry::hash>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<entry, celerix::account, &entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::const_mem_fun<entry, priority_key, &entry::priority_key>, std::greater<>> // DESC order
	>>;
	// clang-format on

	ordered_blocks blocks;

	// Keep track of the size of the backlog in number of unconfirmed blocks per bucket
	std::unordered_map<celerix::bucket_index, size_t> size_by_bucket;
};

class bounded_backlog_config
{
public:
	celerix::error deserialize (celerix::tomlconfig &);
	celerix::error serialize (celerix::tomlconfig &) const;

public:
	bool enable{ true };
	size_t batch_size{ 32 };
	size_t max_queued_notifications{ 128 };
	size_t scan_rate{ 64 };
};

class bounded_backlog
{
public:
	bounded_backlog (celerix::node_config const &, celerix::node &, celerix::ledger &, celerix::bucketing &, celerix::backlog_scan &, celerix::block_processor &, celerix::confirming_set &, celerix::stats &, celerix::logger &);
	~bounded_backlog ();

	void start ();
	void stop ();

	size_t index_size () const;
	size_t bucket_threshold () const;
	bool contains (celerix::block_hash const &) const;

	celerix::container_info container_info () const;

private: // Dependencies
	celerix::node_config const & config;
	celerix::node & node;
	celerix::ledger & ledger;
	celerix::bucketing & bucketing;
	celerix::backlog_scan & backlog_scan;
	celerix::block_processor & block_processor;
	celerix::confirming_set & confirming_set;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	void activate (celerix::secure::transaction &, celerix::account const &, celerix::account_info const &, celerix::confirmation_height_info const &);
	void update (celerix::secure::transaction const &, celerix::block_hash const &);
	bool insert (celerix::secure::transaction const &, celerix::block const &);

	bool predicate () const;
	void run ();
	std::deque<celerix::block_hash> gather_targets (size_t max_count) const;
	bool should_rollback (celerix::block_hash const &) const;

	std::deque<celerix::block_hash> perform_rollbacks (std::deque<celerix::block_hash> const & targets, size_t max_rollbacks);

	void run_scan ();

private:
	celerix::backlog_index index;

	celerix::rate_limiter scan_limiter;

	std::atomic<bool> stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;
	std::thread scan_thread;

	celerix::thread_pool workers;
};
}