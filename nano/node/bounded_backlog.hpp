#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/bucketing.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread.hpp>

#include <unordered_map>

namespace mi = boost::multi_index;

namespace nano
{
class backlog_index
{
public:
	struct priority_key
	{
		nano::bucket_index bucket;
		nano::priority_timestamp priority;

		auto operator<=> (priority_key const &) const = default;
	};

	struct entry
	{
		nano::block_hash hash;
		nano::account account;
		nano::bucket_index bucket;
		nano::priority_timestamp priority;

		backlog_index::priority_key priority_key () const
		{
			return { bucket, priority };
		}
	};

public:
	backlog_index () = default;

	bool insert (nano::block const & block, nano::bucket_index, nano::priority_timestamp);

	bool erase (nano::account const & account);
	bool erase (nano::block_hash const & hash);

	using filter_callback = std::function<bool (nano::block_hash const &)>;
	std::deque<nano::block_hash> top (nano::bucket_index, size_t count, filter_callback const &) const;

	std::deque<nano::block_hash> next (nano::block_hash last, size_t count) const;

	bool contains (nano::block_hash const & hash) const;
	size_t size () const;
	size_t size (nano::bucket_index) const;

	nano::container_info container_info () const;

private:
	// clang-format off
	class tag_hash {};
	class tag_hash_ordered {};
	class tag_account {};
	class tag_priority {};

	using ordered_blocks = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>, // Allows for fast lookup
			mi::member<entry, nano::block_hash, &entry::hash>>,
		mi::ordered_unique<mi::tag<tag_hash_ordered>, // Allows for sequential scan
			mi::member<entry, nano::block_hash, &entry::hash>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<entry, nano::account, &entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::const_mem_fun<entry, priority_key, &entry::priority_key>, std::greater<>> // DESC order
	>>;
	// clang-format on

	ordered_blocks blocks;

	// Keep track of the size of the backlog in number of unconfirmed blocks per bucket
	std::unordered_map<nano::bucket_index, size_t> size_by_bucket;
};

class bounded_backlog_config
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ true };
	size_t batch_size{ 32 };
	size_t scan_rate{ 64 };
};

class bounded_backlog
{
public:
	bounded_backlog (nano::node_config const &, nano::node &, nano::ledger &, nano::ledger_notifications &, nano::bucketing &, nano::backlog_scan &, nano::block_processor &, nano::cementing_set &, nano::stats &, nano::logger &);
	~bounded_backlog ();

	void start ();
	void stop ();

	size_t index_size () const;
	bool contains (nano::block_hash const &) const;

	nano::container_info container_info () const;

private: // Dependencies
	nano::node_config const & config;
	nano::node & node;
	nano::ledger & ledger;
	nano::ledger_notifications & ledger_notifications;
	nano::bucketing & bucketing;
	nano::backlog_scan & backlog_scan;
	nano::cementing_set & cementing_set;
	nano::stats & stats;
	nano::logger & logger;

private:
	void activate (nano::secure::transaction &, nano::account const &, nano::account_info const &, nano::confirmation_height_info const &);
	void update (nano::secure::transaction const &, nano::block_hash const &);
	bool insert (nano::secure::transaction const &, nano::block const &);

	bool predicate () const;
	void run ();
	std::deque<nano::block_hash> gather_targets (size_t max_count, size_t bucket_threshold) const;
	bool should_rollback (nano::block_hash const &) const;

	std::deque<nano::block_hash> perform_rollbacks (std::deque<nano::block_hash> const & targets, size_t max_rollbacks);

	void run_scan ();

private:
	nano::backlog_index index;

	nano::rate_limiter scan_limiter;

	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	boost::thread thread;
	std::thread scan_thread;
};
}