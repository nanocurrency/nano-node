#pragma once

#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_bulk_pull.hpp>

#include <atomic>
#include <future>

namespace nano
{
class node;

class frontier_req_client;
class bulk_push_client;
class bootstrap_attempt : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	explicit bootstrap_attempt (std::shared_ptr<nano::node> node_a, nano::bootstrap_mode mode_a, uint64_t incremental_id_a, std::string id_a);
	virtual ~bootstrap_attempt ();
	virtual void run () = 0;
	virtual void stop ();
	bool still_pulling ();
	void pull_started ();
	void pull_finished ();
	bool should_log ();
	std::string mode_text ();
	virtual void restart_condition ();
	virtual void add_frontier (nano::pull_info const &);
	virtual void add_bulk_push_target (nano::block_hash const &, nano::block_hash const &);
	virtual bool request_bulk_push_target (std::pair<nano::block_hash, nano::block_hash> &);
	virtual void add_recent_pull (nano::block_hash const &);
	virtual void lazy_start (nano::hash_or_account const &, bool confirmed = true);
	virtual void lazy_add (nano::pull_info const &);
	virtual void lazy_requeue (nano::block_hash const &, nano::block_hash const &, bool);
	virtual uint32_t lazy_batch_size ();
	virtual bool lazy_has_expired () const;
	virtual bool lazy_processed_or_exists (nano::block_hash const &);
	virtual bool process_block (std::shared_ptr<nano::block>, nano::account const &, uint64_t, nano::bulk_pull::count_t, bool, unsigned);
	virtual void requeue_pending (nano::account const &);
	virtual void wallet_start (std::deque<nano::account> &);
	virtual size_t wallet_size ();
	virtual void get_information (boost::property_tree::ptree &) = 0;
	std::mutex next_log_mutex;
	std::chrono::steady_clock::time_point next_log{ std::chrono::steady_clock::now () };
	std::atomic<unsigned> pulling{ 0 };
	std::shared_ptr<nano::node> node;
	std::atomic<uint64_t> total_blocks{ 0 };
	std::atomic<unsigned> requeued_pulls{ 0 };
	std::atomic<bool> started{ false };
	std::atomic<bool> stopped{ false };
	uint64_t incremental_id{ 0 };
	std::string id;
	std::chrono::steady_clock::time_point attempt_start{ std::chrono::steady_clock::now () };
	std::atomic<bool> frontiers_received{ false };
	std::atomic<bool> frontiers_confirmed{ false };
	nano::bootstrap_mode mode;
	std::mutex mutex;
	nano::condition_variable condition;
};
class bootstrap_attempt_legacy : public bootstrap_attempt
{
public:
	explicit bootstrap_attempt_legacy (std::shared_ptr<nano::node> node_a, uint64_t incremental_id_a, std::string id_a = "");
	void run () override;
	bool consume_future (std::future<bool> &);
	void stop () override;
	bool request_frontier (nano::unique_lock<std::mutex> &, bool = false);
	void request_pull (nano::unique_lock<std::mutex> &);
	void request_push (nano::unique_lock<std::mutex> &);
	void add_frontier (nano::pull_info const &) override;
	void add_bulk_push_target (nano::block_hash const &, nano::block_hash const &) override;
	bool request_bulk_push_target (std::pair<nano::block_hash, nano::block_hash> &) override;
	void add_recent_pull (nano::block_hash const &) override;
	void run_start (nano::unique_lock<std::mutex> &);
	void restart_condition () override;
	void attempt_restart_check (nano::unique_lock<std::mutex> &);
	bool confirm_frontiers (nano::unique_lock<std::mutex> &);
	void get_information (boost::property_tree::ptree &) override;
	nano::tcp_endpoint endpoint_frontier_request;
	std::weak_ptr<nano::frontier_req_client> frontiers;
	std::weak_ptr<nano::bulk_push_client> push;
	std::deque<nano::pull_info> frontier_pulls;
	std::deque<nano::block_hash> recent_pulls_head;
	std::vector<std::pair<nano::block_hash, nano::block_hash>> bulk_push_targets;
	std::atomic<unsigned> account_count{ 0 };
	std::atomic<bool> frontiers_confirmation_pending{ false };
};
}
