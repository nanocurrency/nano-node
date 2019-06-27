#pragma once

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
class pull_info
{
public:
	using count_t = nano::bulk_pull::count_t;
	pull_info () = default;
	pull_info (nano::account const &, nano::block_hash const &, nano::block_hash const &, count_t = 0);
	nano::account account{ 0 };
	nano::block_hash head{ 0 };
	nano::block_hash head_original{ 0 };
	nano::block_hash end{ 0 };
	count_t count{ 0 };
	unsigned attempts{ 0 };
	uint64_t processed{ 0 };
};
enum class bootstrap_mode
{
	legacy,
	lazy,
	wallet_lazy
};
class frontier_req_client;
class bulk_push_client;
class bulk_pull_account_client;
class bootstrap_attempt final : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	explicit bootstrap_attempt (std::shared_ptr<nano::node> node_a);
	~bootstrap_attempt ();
	void run ();
	std::shared_ptr<nano::bootstrap_client> connection (std::unique_lock<std::mutex> &);
	bool consume_future (std::future<bool> &);
	void populate_connections ();
	bool request_frontier (std::unique_lock<std::mutex> &);
	void request_pull (std::unique_lock<std::mutex> &);
	void request_push (std::unique_lock<std::mutex> &);
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
	void lazy_run ();
	void lazy_start (nano::block_hash const &);
	void lazy_add (nano::block_hash const &);
	bool lazy_finished ();
	void lazy_pull_flush ();
	void lazy_clear ();
	void request_pending (std::unique_lock<std::mutex> &);
	void requeue_pending (nano::account const &);
	void wallet_run ();
	void wallet_start (std::deque<nano::account> &);
	bool wallet_finished ();
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
	std::condition_variable condition;
	// Lazy bootstrap
	std::unordered_set<nano::block_hash> lazy_blocks;
	std::unordered_map<nano::block_hash, std::pair<nano::block_hash, nano::uint128_t>> lazy_state_unknown;
	std::unordered_map<nano::block_hash, nano::uint128_t> lazy_balances;
	std::unordered_set<nano::block_hash> lazy_keys;
	std::deque<nano::block_hash> lazy_pulls;
	std::atomic<uint64_t> lazy_stopped;
	uint64_t lazy_max_stopped = 256;
	std::mutex lazy_mutex;
	// Wallet lazy bootstrap
	std::deque<nano::account> wallet_accounts;
};
class frontier_req_client final : public std::enable_shared_from_this<nano::frontier_req_client>
{
public:
	explicit frontier_req_client (std::shared_ptr<nano::bootstrap_client>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	void unsynced (nano::block_hash const &, nano::block_hash const &);
	void next (nano::transaction const &);
	std::shared_ptr<nano::bootstrap_client> connection;
	nano::account current;
	nano::block_hash frontier;
	unsigned count;
	nano::account landing;
	nano::account faucet;
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
	/** A very rough estimate of the cost of `bulk_push`ing missing blocks */
	uint64_t bulk_push_cost;
	std::deque<std::pair<nano::account, nano::block_hash>> accounts;
	static size_t constexpr size_frontier = sizeof (nano::account) + sizeof (nano::block_hash);
};
class bulk_pull_client final : public std::enable_shared_from_this<nano::bulk_pull_client>
{
public:
	bulk_pull_client (std::shared_ptr<nano::bootstrap_client>, nano::pull_info const &);
	~bulk_pull_client ();
	void request ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, nano::block_type);
	nano::block_hash first ();
	std::shared_ptr<nano::bootstrap_client> connection;
	nano::block_hash expected;
	nano::account known_account;
	nano::pull_info pull;
	uint64_t total_blocks;
	uint64_t unexpected_count;
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
class bulk_push_client final : public std::enable_shared_from_this<nano::bulk_push_client>
{
public:
	explicit bulk_push_client (std::shared_ptr<nano::bootstrap_client> const &);
	~bulk_push_client ();
	void start ();
	void push (nano::transaction const &);
	void push_block (nano::block const &);
	void send_finished ();
	std::shared_ptr<nano::bootstrap_client> connection;
	std::promise<bool> promise;
	std::pair<nano::block_hash, nano::block_hash> current_target;
};
class bulk_pull_account_client final : public std::enable_shared_from_this<nano::bulk_pull_account_client>
{
public:
	bulk_pull_account_client (std::shared_ptr<nano::bootstrap_client>, nano::account const &);
	~bulk_pull_account_client ();
	void request ();
	void receive_pending ();
	std::shared_ptr<nano::bootstrap_client> connection;
	nano::account account;
	uint64_t total_blocks;
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
	std::condition_variable condition;
	std::mutex observers_mutex;
	std::vector<std::function<void(bool)>> observers;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);

class bootstrap_server;
class bootstrap_listener final
{
public:
	bootstrap_listener (uint16_t, nano::node &);
	void start ();
	void stop ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<nano::socket>);
	size_t connection_count ();

	std::mutex mutex;
	std::unordered_map<nano::bootstrap_server *, std::weak_ptr<nano::bootstrap_server>> connections;
	nano::tcp_endpoint endpoint ();
	nano::node & node;
	std::shared_ptr<nano::server_socket> listening_socket;
	bool on;
	std::atomic<size_t> bootstrap_count{ 0 };
	std::atomic<size_t> realtime_count{ 0 };

private:
	uint16_t port;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_listener & bootstrap_listener, const std::string & name);

class message;
enum class bootstrap_server_type
{
	undefined,
	bootstrap,
	realtime,
	realtime_response_server // special type for tcp channel response server
};
class bootstrap_server final : public std::enable_shared_from_this<nano::bootstrap_server>
{
public:
	bootstrap_server (std::shared_ptr<nano::socket>, std::shared_ptr<nano::node>);
	~bootstrap_server ();
	void stop ();
	void receive ();
	void receive_header_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void receive_bulk_pull_account_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void receive_frontier_req_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void receive_keepalive_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void receive_publish_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void receive_confirm_req_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void receive_confirm_ack_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void receive_node_id_handshake_action (boost::system::error_code const &, size_t, nano::message_header const &);
	void add_request (std::unique_ptr<nano::message>);
	void finish_request ();
	void finish_request_async ();
	void run_next ();
	void timeout ();
	bool is_bootstrap_connection ();
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<nano::socket> socket;
	std::shared_ptr<nano::node> node;
	std::mutex mutex;
	std::queue<std::unique_ptr<nano::message>> requests;
	std::atomic<bool> stopped{ false };
	std::atomic<nano::bootstrap_server_type> type{ nano::bootstrap_server_type::undefined };
	std::atomic<bool> keepalive_first{ true };
	// Remote enpoint used to remove response channel even after socket closing
	nano::tcp_endpoint remote_endpoint{ boost::asio::ip::address_v6::any (), 0 };
	nano::account remote_node_id{ 0 };
};
class bulk_pull;
class bulk_pull_server final : public std::enable_shared_from_this<nano::bulk_pull_server>
{
public:
	bulk_pull_server (std::shared_ptr<nano::bootstrap_server> const &, std::unique_ptr<nano::bulk_pull>);
	void set_current_end ();
	std::shared_ptr<nano::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<nano::bootstrap_server> connection;
	std::unique_ptr<nano::bulk_pull> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	nano::block_hash current;
	bool include_start;
	nano::bulk_pull::count_t max_count;
	nano::bulk_pull::count_t sent_count;
};
class bulk_pull_account;
class bulk_pull_account_server final : public std::enable_shared_from_this<nano::bulk_pull_account_server>
{
public:
	bulk_pull_account_server (std::shared_ptr<nano::bootstrap_server> const &, std::unique_ptr<nano::bulk_pull_account>);
	void set_params ();
	std::pair<std::unique_ptr<nano::pending_key>, std::unique_ptr<nano::pending_info>> get_next ();
	void send_frontier ();
	void send_next_block ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void complete (boost::system::error_code const &, size_t);
	std::shared_ptr<nano::bootstrap_server> connection;
	std::unique_ptr<nano::bulk_pull_account> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	std::unordered_set<nano::uint256_union> deduplication;
	nano::pending_key current_key;
	bool pending_address_only;
	bool pending_include_address;
	bool invalid_request;
};
class bulk_push_server final : public std::enable_shared_from_this<nano::bulk_push_server>
{
public:
	explicit bulk_push_server (std::shared_ptr<nano::bootstrap_server> const &);
	void receive ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, nano::block_type);
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<nano::bootstrap_server> connection;
};
class frontier_req;
class frontier_req_server final : public std::enable_shared_from_this<nano::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<nano::bootstrap_server> const &, std::unique_ptr<nano::frontier_req>);
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	std::shared_ptr<nano::bootstrap_server> connection;
	nano::account current;
	nano::block_hash frontier;
	std::unique_ptr<nano::frontier_req> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	size_t count;
	std::deque<std::pair<nano::account, nano::block_hash>> accounts;
};
}
