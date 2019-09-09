#pragma once

#include <nano/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <nano/node/common.hpp>
#include <nano/node/socket.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/log/sources/logger.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <future>
#include <queue>
#include <stack>
#include <unordered_set>

namespace nano
{
class bootstrap_attempt;
class bootstrap_client;
class node;
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

class bootstrap_client;
enum class bootstrap_mode
{
	legacy,
	lazy,
	wallet_lazy
};
class frontier_req_client;
class bulk_push_client;
class bootstrap_attempt final : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	explicit bootstrap_attempt (std::shared_ptr<nano::node> node_a, nano::bootstrap_mode mode_a = nano::bootstrap_mode::legacy);
	~bootstrap_attempt ();
	void run ();
	std::shared_ptr<nano::bootstrap_client> connection (nano::unique_lock<std::mutex> &);
	bool consume_future (std::future<bool> &);
	void populate_connections ();
	bool request_frontier (nano::unique_lock<std::mutex> &);
	void request_pull (nano::unique_lock<std::mutex> &);
	void request_push (nano::unique_lock<std::mutex> &);
	void add_connection (nano::endpoint const &);
	void connect_client (nano::tcp_endpoint const &);
	void pool_connection (std::shared_ptr<nano::bootstrap_client>);
	void stop ();
	void requeue_pull (nano::pull_info const &);
	void add_pull (nano::pull_info const &);
	bool still_pulling ();
	unsigned target_connections (size_t pulls_remaining);
	bool should_log ();
	void add_bulk_push_target (nano::block_hash const &, nano::block_hash const &);
	bool process_block (std::shared_ptr<nano::block>, nano::account const &, uint64_t, bool);
	/** Lazy bootstrap */
	void lazy_run ();
	void lazy_start (nano::block_hash const &);
	void lazy_add (nano::block_hash const &);
	void lazy_requeue (nano::block_hash const &);
	bool lazy_finished ();
	void lazy_pull_flush ();
	void lazy_clear ();
	bool process_block_lazy (std::shared_ptr<nano::block>, nano::account const &, uint64_t);
	void lazy_block_state (std::shared_ptr<nano::block>);
	void lazy_block_state_backlog_check (std::shared_ptr<nano::block>, nano::block_hash const &);
	void lazy_backlog_cleanup ();
	bool lazy_processed_or_exists (nano::block_hash const &);
	/** Lazy bootstrap */
	/** Wallet bootstrap */
	void request_pending (nano::unique_lock<std::mutex> &);
	void requeue_pending (nano::account const &);
	void wallet_run ();
	void wallet_start (std::deque<nano::account> &);
	bool wallet_finished ();
	/** Wallet bootstrap */
	std::mutex next_log_mutex;
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::weak_ptr<nano::bootstrap_client>> clients;
	std::weak_ptr<nano::bootstrap_client> connection_frontier_request;
	std::weak_ptr<nano::frontier_req_client> frontiers;
	std::weak_ptr<nano::bulk_push_client> push;
	std::deque<nano::pull_info> pulls;
	std::deque<std::shared_ptr<nano::bootstrap_client>> idle;
	std::atomic<unsigned> connections;
	std::atomic<unsigned> pulling;
	std::shared_ptr<nano::node> node;
	std::atomic<unsigned> account_count;
	std::atomic<uint64_t> total_blocks;
	std::atomic<unsigned> runs_count;
	std::vector<std::pair<nano::block_hash, nano::block_hash>> bulk_push_targets;
	std::atomic<bool> stopped;
	nano::bootstrap_mode mode;
	std::mutex mutex;
	nano::condition_variable condition;
	// Lazy bootstrap
	std::unordered_set<nano::block_hash> lazy_blocks;
	std::unordered_map<nano::block_hash, std::pair<nano::block_hash, nano::uint128_t>> lazy_state_backlog;
	std::unordered_map<nano::block_hash, nano::uint128_t> lazy_balances;
	std::unordered_set<nano::block_hash> lazy_keys;
	std::deque<nano::block_hash> lazy_pulls;
	std::chrono::steady_clock::time_point last_lazy_flush{ std::chrono::steady_clock::now () };
	std::mutex lazy_mutex;
	// Wallet lazy bootstrap
	std::deque<nano::account> wallet_accounts;
};
class bootstrap_client final : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<nano::node>, std::shared_ptr<nano::bootstrap_attempt>, std::shared_ptr<nano::transport::channel_tcp>);
	~bootstrap_client ();
	std::shared_ptr<nano::bootstrap_client> shared ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	std::shared_ptr<nano::node> node;
	std::shared_ptr<nano::bootstrap_attempt> attempt;
	std::shared_ptr<nano::transport::channel_tcp> channel;
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count;
	std::atomic<bool> pending_stop;
	std::atomic<bool> hard_stop;
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
	boost::multi_index_container<
	nano::cached_pulls,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<nano::cached_pulls, std::chrono::steady_clock::time_point, &nano::cached_pulls::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::tag<account_head_tag>, boost::multi_index::member<nano::cached_pulls, nano::uint512_union, &nano::cached_pulls::account_head>>>>
	cache;
	constexpr static size_t cache_size_max = 10000;
};

class bootstrap_initiator final
{
public:
	explicit bootstrap_initiator (nano::node &);
	~bootstrap_initiator ();
	void bootstrap (nano::endpoint const &, bool add_to_peers = true);
	void bootstrap ();
	void bootstrap_lazy (nano::block_hash const &, bool = false);
	void bootstrap_wallet (std::deque<nano::account> &);
	void run_bootstrap ();
	void notify_listeners (bool);
	void add_observer (std::function<void(bool)> const &);
	bool in_progress ();
	std::shared_ptr<nano::bootstrap_attempt> current_attempt ();
	nano::pulls_cache cache;
	void stop ();

private:
	nano::node & node;
	std::shared_ptr<nano::bootstrap_attempt> attempt;
	std::atomic<bool> stopped;
	std::mutex mutex;
	nano::condition_variable condition;
	std::mutex observers_mutex;
	std::vector<std::function<void(bool)>> observers;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
class bootstrap_limits final
{
public:
	static constexpr double bootstrap_connection_scale_target_blocks = 50000.0;
	static constexpr double bootstrap_connection_warmup_time_sec = 5.0;
	static constexpr double bootstrap_minimum_blocks_per_sec = 10.0;
	static constexpr double bootstrap_minimum_elapsed_seconds_blockrate = 0.02;
	static constexpr double bootstrap_minimum_frontier_blocks_per_sec = 1000.0;
	static constexpr unsigned bootstrap_frontier_retry_limit = 16;
	static constexpr double bootstrap_minimum_termination_time_sec = 30.0;
	static constexpr unsigned bootstrap_max_new_connections = 10;
	static constexpr unsigned bulk_push_cost_limit = 200;
	static constexpr std::chrono::seconds lazy_flush_delay_sec = std::chrono::seconds (5);
};
}
