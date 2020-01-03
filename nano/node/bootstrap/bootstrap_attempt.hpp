#pragma once

#include <nano/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <nano/node/common.hpp>
#include <nano/node/socket.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/ledger.hpp>


#include <atomic>
#include <future>

namespace nano
{
class bootstrap_attempt_lazy;
class bootstrap_attempt_wallet;
class bootstrap_client;
class node;
namespace transport
{
	class channel_tcp;
}

enum class bootstrap_mode
{
	legacy,
	lazy,
	wallet_lazy
};
class frontier_req_client;
class bulk_push_client;
class bootstrap_attempt : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	explicit bootstrap_attempt (std::shared_ptr<nano::node> node_a, nano::bootstrap_mode mode_a = nano::bootstrap_mode::legacy);
	~bootstrap_attempt ();
	void run ();
	std::shared_ptr<nano::bootstrap_client> connection (nano::unique_lock<std::mutex> &, bool = false);
	bool consume_future (std::future<bool> &);
	void populate_connections ();
	void start_populate_connections ();
	bool request_frontier (nano::unique_lock<std::mutex> &, bool = false);
	void request_pull (nano::unique_lock<std::mutex> &);
	void request_push (nano::unique_lock<std::mutex> &);
	void add_connection (nano::endpoint const &);
	void connect_client (nano::tcp_endpoint const &);
	void pool_connection (std::shared_ptr<nano::bootstrap_client>);
	void stop ();
	void requeue_pull (nano::pull_info const &, bool = false);
	void add_pull (nano::pull_info const &);
	bool still_pulling ();
	void run_start (nano::unique_lock<std::mutex> &);
	unsigned target_connections (size_t pulls_remaining);
	bool should_log ();
	void add_bulk_push_target (nano::block_hash const &, nano::block_hash const &);
	void attempt_restart_check (nano::unique_lock<std::mutex> &);
	bool confirm_frontiers (nano::unique_lock<std::mutex> &);
	virtual bool process_block (std::shared_ptr<nano::block>, nano::account const &, uint64_t, nano::bulk_pull::count_t, bool, unsigned) = 0;
	virtual void requeue_pending (nano::account const &) = 0;
	virtual size_t wallet_size () = 0;
	std::mutex next_log_mutex;
	std::chrono::steady_clock::time_point next_log{ std::chrono::steady_clock::now () };
	std::deque<std::weak_ptr<nano::bootstrap_client>> clients;
	std::weak_ptr<nano::bootstrap_client> connection_frontier_request;
	nano::tcp_endpoint endpoint_frontier_request;
	std::weak_ptr<nano::frontier_req_client> frontiers;
	std::weak_ptr<nano::bulk_push_client> push;
	std::deque<nano::pull_info> pulls;
	std::deque<nano::block_hash> recent_pulls_head;
	std::deque<std::shared_ptr<nano::bootstrap_client>> idle;
	std::atomic<unsigned> connections{ 0 };
	std::atomic<unsigned> pulling{ 0 };
	std::shared_ptr<nano::node> node;
	std::atomic<unsigned> account_count{ 0 };
	std::atomic<uint64_t> total_blocks{ 0 };
	std::atomic<unsigned> requeued_pulls{ 0 };
	std::vector<std::pair<nano::block_hash, nano::block_hash>> bulk_push_targets;
	std::atomic<bool> frontiers_received{ false };
	std::atomic<bool> frontiers_confirmed{ false };
	std::atomic<bool> populate_connections_started{ false };
	std::atomic<bool> stopped{ false };
	std::chrono::steady_clock::time_point attempt_start{ std::chrono::steady_clock::now () };
	nano::bootstrap_mode mode;
	std::mutex mutex;
	nano::condition_variable condition;
};
class bootstrap_attempt_legacy : public bootstrap_attempt
{
public:
	using bootstrap_attempt::bootstrap_attempt;
	bool process_block (std::shared_ptr<nano::block>, nano::account const &, uint64_t, nano::bulk_pull::count_t, bool, unsigned) override;
	void requeue_pending (nano::account const &) override;
	size_t wallet_size () override;
};
class bootstrap_client final : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<nano::node>, std::shared_ptr<nano::bootstrap_attempt>, std::shared_ptr<nano::transport::channel_tcp>, std::shared_ptr<nano::socket>);
	~bootstrap_client ();
	std::shared_ptr<nano::bootstrap_client> shared ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	std::shared_ptr<nano::node> node;
	std::shared_ptr<nano::bootstrap_attempt> attempt;
	std::shared_ptr<nano::transport::channel_tcp> channel;
	std::shared_ptr<nano::socket> socket;
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count;
	std::atomic<bool> pending_stop;
	std::atomic<bool> hard_stop;
};
}
