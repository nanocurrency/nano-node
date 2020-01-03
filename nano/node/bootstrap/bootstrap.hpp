#pragma once

#include <nano/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <future>
#include <queue>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace nano
{
class node;

class bootstrap_attempt_legacy;
class bootstrap_attempt_lazy;
class bootstrap_attempt_wallet;
namespace transport
{
	class channel_tcp;
}
enum class sync_result
{
	success,
	error,
	fork
};
class cached_pulls final
{
public:
	std::chrono::steady_clock::time_point time;
	nano::uint512_union account_head;
	nano::block_hash new_head;
};
class pulls_cache final
{
public:
	void add (nano::pull_info const &);
	void update_pull (nano::pull_info &);
	void remove (nano::pull_info const &);
	std::mutex pulls_cache_mutex;
	class account_head_tag
	{
	};
	// clang-format off
	boost::multi_index_container<nano::cached_pulls,
	mi::indexed_by<
		mi::ordered_non_unique<
			mi::member<nano::cached_pulls, std::chrono::steady_clock::time_point, &nano::cached_pulls::time>>,
		mi::hashed_unique<mi::tag<account_head_tag>,
			mi::member<nano::cached_pulls, nano::uint512_union, &nano::cached_pulls::account_head>>>>
	cache;
	// clang-format on
	constexpr static size_t cache_size_max = 10000;
};
class excluded_peers_item final
{
public:
	std::chrono::steady_clock::time_point exclude_until;
	nano::tcp_endpoint endpoint;
	uint64_t score;
};
class bootstrap_excluded_peers final
{
public:
	uint64_t add (nano::tcp_endpoint const &, size_t);
	bool check (nano::tcp_endpoint const &);
	void remove (nano::tcp_endpoint const &);
	std::mutex excluded_peers_mutex;
	class endpoint_tag
	{
	};
	// clang-format off
	boost::multi_index_container<nano::excluded_peers_item,
	mi::indexed_by<
		mi::ordered_non_unique<
			mi::member<nano::excluded_peers_item, std::chrono::steady_clock::time_point, &nano::excluded_peers_item::exclude_until>>,
		mi::hashed_unique<mi::tag<endpoint_tag>,
			mi::member<nano::excluded_peers_item, nano::tcp_endpoint, &nano::excluded_peers_item::endpoint>>>>
	peers;
	// clang-format on
	constexpr static size_t excluded_peers_size_max = 5000;
	constexpr static double excluded_peers_percentage_limit = 0.5;
	constexpr static uint64_t score_limit = 2;
	constexpr static std::chrono::hours exclude_time_hours = std::chrono::hours (1);
	constexpr static std::chrono::hours exclude_remove_hours = std::chrono::hours (24);
};

class bootstrap_initiator final
{
public:
	explicit bootstrap_initiator (nano::node &);
	~bootstrap_initiator ();
	void bootstrap (nano::endpoint const &, bool add_to_peers = true, bool frontiers_confirmed = false);
	void bootstrap (bool force = false);
	void bootstrap_lazy (nano::hash_or_account const &, bool force = false, bool confirmed = true);
	void bootstrap_wallet (std::deque<nano::account> &);
	void run_bootstrap ();
	void run_lazy_bootstrap ();
	void run_wallet_bootstrap ();
	void lazy_requeue (nano::block_hash const &, nano::block_hash const &, bool);
	void notify_listeners (bool);
	void add_observer (std::function<void(bool)> const &);
	bool in_progress ();
	std::shared_ptr<nano::bootstrap_attempt_legacy> current_attempt ();
	std::shared_ptr<nano::bootstrap_attempt_lazy> current_lazy_attempt ();
	std::shared_ptr<nano::bootstrap_attempt_wallet> current_wallet_attempt ();
	nano::pulls_cache cache;
	nano::bootstrap_excluded_peers excluded_peers;
	void stop ();

private:
	nano::node & node;
	std::shared_ptr<nano::bootstrap_attempt_legacy> attempt;
	std::shared_ptr<nano::bootstrap_attempt_lazy> lazy_attempt;
	std::shared_ptr<nano::bootstrap_attempt_wallet> wallet_attempt;
	std::atomic<bool> stopped;
	std::mutex mutex;
	nano::condition_variable condition;
	std::mutex observers_mutex;
	std::vector<std::function<void(bool)>> observers;
	std::vector<boost::thread> bootstrap_initiator_threads;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
class bootstrap_limits final
{
public:
	static constexpr double bootstrap_connection_scale_target_blocks = 50000.0;
	static constexpr double bootstrap_connection_scale_target_blocks_lazy = bootstrap_connection_scale_target_blocks / 5;
	static constexpr double bootstrap_connection_warmup_time_sec = 5.0;
	static constexpr double bootstrap_minimum_blocks_per_sec = 10.0;
	static constexpr double bootstrap_minimum_elapsed_seconds_blockrate = 0.02;
	static constexpr double bootstrap_minimum_frontier_blocks_per_sec = 1000.0;
	static constexpr double bootstrap_minimum_termination_time_sec = 30.0;
	static constexpr unsigned bootstrap_max_new_connections = 32;
	static constexpr size_t bootstrap_max_confirm_frontiers = 70;
	static constexpr double required_frontier_confirmation_ratio = 0.8;
	static constexpr unsigned frontier_confirmation_blocks_limit = 128 * 1024;
	static constexpr unsigned requeued_pulls_limit = 256;
	static constexpr unsigned requeued_pulls_limit_test = 2;
	static constexpr unsigned bulk_push_cost_limit = 200;
	static constexpr std::chrono::seconds lazy_flush_delay_sec = std::chrono::seconds (5);
	static constexpr unsigned lazy_destinations_request_limit = 256 * 1024;
	static constexpr uint64_t lazy_batch_pull_count_resize_blocks_limit = 4 * 1024 * 1024;
	static constexpr double lazy_batch_pull_count_resize_ratio = 2.0;
	static constexpr size_t lazy_blocks_restart_limit = 1024 * 1024;
};
}
