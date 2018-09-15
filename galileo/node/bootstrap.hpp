#pragma once

#include <galileo/node/common.hpp>
#include <galileo/secure/blockstore.hpp>
#include <galileo/secure/ledger.hpp>

#include <atomic>
#include <future>
#include <queue>
#include <stack>
#include <unordered_set>

#include <boost/log/sources/logger.hpp>

namespace galileo
{
class bootstrap_attempt;
class bootstrap_client;
class node;
enum class sync_result
{
	success,
	error,
	fork
};
class socket : public std::enable_shared_from_this<galileo::socket>
{
public:
	socket (std::shared_ptr<galileo::node>);
	void async_connect (galileo::tcp_endpoint const &, std::function<void(boost::system::error_code const &)>);
	void async_read (std::shared_ptr<std::vector<uint8_t>>, size_t, std::function<void(boost::system::error_code const &, size_t)>);
	void async_write (std::shared_ptr<std::vector<uint8_t>>, std::function<void(boost::system::error_code const &, size_t)>);
	void start (std::chrono::steady_clock::time_point = std::chrono::steady_clock::now () + std::chrono::seconds (5));
	void stop ();
	void close ();
	galileo::tcp_endpoint remote_endpoint ();
	boost::asio::ip::tcp::socket socket_m;

private:
	std::atomic<unsigned> ticket;
	std::shared_ptr<galileo::node> node;
};

/**
 * The length of every message header, parsed by galileo::message::read_header ()
 * The 2 here represents the size of a std::bitset<16>, which is 2 chars long normally
 */
static const int bootstrap_message_header_size = sizeof (galileo::message_header::magic_number) + sizeof (uint8_t) + sizeof (uint8_t) + sizeof (uint8_t) + sizeof (galileo::message_type) + 2;

class bootstrap_client;
class pull_info
{
public:
	pull_info ();
	pull_info (galileo::account const &, galileo::block_hash const &, galileo::block_hash const &);
	galileo::account account;
	galileo::block_hash head;
	galileo::block_hash end;
	unsigned attempts;
};
class frontier_req_client;
class bulk_push_client;
class bootstrap_attempt : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	bootstrap_attempt (std::shared_ptr<galileo::node> node_a);
	~bootstrap_attempt ();
	void run ();
	std::shared_ptr<galileo::bootstrap_client> connection (std::unique_lock<std::mutex> &);
	bool consume_future (std::future<bool> &);
	void populate_connections ();
	bool request_frontier (std::unique_lock<std::mutex> &);
	void request_pull (std::unique_lock<std::mutex> &);
	void request_push (std::unique_lock<std::mutex> &);
	void add_connection (galileo::endpoint const &);
	void pool_connection (std::shared_ptr<galileo::bootstrap_client>);
	void stop ();
	void requeue_pull (galileo::pull_info const &);
	void add_pull (galileo::pull_info const &);
	bool still_pulling ();
	unsigned target_connections (size_t pulls_remaining);
	bool should_log ();
	void add_bulk_push_target (galileo::block_hash const &, galileo::block_hash const &);
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::weak_ptr<galileo::bootstrap_client>> clients;
	std::weak_ptr<galileo::bootstrap_client> connection_frontier_request;
	std::weak_ptr<galileo::frontier_req_client> frontiers;
	std::weak_ptr<galileo::bulk_push_client> push;
	std::deque<galileo::pull_info> pulls;
	std::deque<std::shared_ptr<galileo::bootstrap_client>> idle;
	std::atomic<unsigned> connections;
	std::atomic<unsigned> pulling;
	std::shared_ptr<galileo::node> node;
	std::atomic<unsigned> account_count;
	std::atomic<uint64_t> total_blocks;
	std::vector<std::pair<galileo::block_hash, galileo::block_hash>> bulk_push_targets;
	bool stopped;
	std::mutex mutex;
	std::condition_variable condition;
};
class frontier_req_client : public std::enable_shared_from_this<galileo::frontier_req_client>
{
public:
	frontier_req_client (std::shared_ptr<galileo::bootstrap_client>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	void request_account (galileo::account const &, galileo::block_hash const &);
	void unsynced (galileo::block_hash const &, galileo::block_hash const &);
	void next (galileo::transaction const &);
	void insert_pull (galileo::pull_info const &);
	std::shared_ptr<galileo::bootstrap_client> connection;
	galileo::account current;
	galileo::account_info info;
	unsigned count;
	galileo::account landing;
	galileo::account faucet;
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
	/** A very rough estimate of the cost of `bulk_push`ing missing blocks */
	uint64_t bulk_push_cost;
};
class bulk_pull_client : public std::enable_shared_from_this<galileo::bulk_pull_client>
{
public:
	bulk_pull_client (std::shared_ptr<galileo::bootstrap_client>, galileo::pull_info const &);
	~bulk_pull_client ();
	void request ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, galileo::block_type);
	galileo::block_hash first ();
	std::shared_ptr<galileo::bootstrap_client> connection;
	galileo::block_hash expected;
	galileo::pull_info pull;
};
class bootstrap_client : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<galileo::node>, std::shared_ptr<galileo::bootstrap_attempt>, galileo::tcp_endpoint const &);
	~bootstrap_client ();
	void run ();
	std::shared_ptr<galileo::bootstrap_client> shared ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	std::shared_ptr<galileo::node> node;
	std::shared_ptr<galileo::bootstrap_attempt> attempt;
	std::shared_ptr<galileo::socket> socket;
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	galileo::tcp_endpoint endpoint;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count;
	std::atomic<bool> pending_stop;
	std::atomic<bool> hard_stop;
};
class bulk_push_client : public std::enable_shared_from_this<galileo::bulk_push_client>
{
public:
	bulk_push_client (std::shared_ptr<galileo::bootstrap_client> const &);
	~bulk_push_client ();
	void start ();
	void push (galileo::transaction const &);
	void push_block (galileo::block const &);
	void send_finished ();
	std::shared_ptr<galileo::bootstrap_client> connection;
	std::promise<bool> promise;
	std::pair<galileo::block_hash, galileo::block_hash> current_target;
};
class bootstrap_initiator
{
public:
	bootstrap_initiator (galileo::node &);
	~bootstrap_initiator ();
	void bootstrap (galileo::endpoint const &, bool add_to_peers = true);
	void bootstrap ();
	void run_bootstrap ();
	void notify_listeners (bool);
	void add_observer (std::function<void(bool)> const &);
	bool in_progress ();
	std::shared_ptr<galileo::bootstrap_attempt> current_attempt ();
	void stop ();

private:
	galileo::node & node;
	std::shared_ptr<galileo::bootstrap_attempt> attempt;
	bool stopped;
	std::mutex mutex;
	std::condition_variable condition;
	std::vector<std::function<void(bool)>> observers;
	std::thread thread;
};
class bootstrap_server;
class bootstrap_listener
{
public:
	bootstrap_listener (boost::asio::io_service &, uint16_t, galileo::node &);
	void start ();
	void stop ();
	void accept_connection ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<galileo::socket>);
	std::mutex mutex;
	std::unordered_map<galileo::bootstrap_server *, std::weak_ptr<galileo::bootstrap_server>> connections;
	galileo::tcp_endpoint endpoint ();
	boost::asio::ip::tcp::acceptor acceptor;
	galileo::tcp_endpoint local;
	boost::asio::io_service & service;
	galileo::node & node;
	bool on;
};
class message;
class bootstrap_server : public std::enable_shared_from_this<galileo::bootstrap_server>
{
public:
	bootstrap_server (std::shared_ptr<galileo::socket>, std::shared_ptr<galileo::node>);
	~bootstrap_server ();
	void receive ();
	void receive_header_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_action (boost::system::error_code const &, size_t, galileo::message_header const &);
	void receive_bulk_pull_account_action (boost::system::error_code const &, size_t, galileo::message_header const &);
	void receive_bulk_pull_blocks_action (boost::system::error_code const &, size_t, galileo::message_header const &);
	void receive_frontier_req_action (boost::system::error_code const &, size_t, galileo::message_header const &);
	void receive_bulk_push_action ();
	void add_request (std::unique_ptr<galileo::message>);
	void finish_request ();
	void run_next ();
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<galileo::socket> socket;
	std::shared_ptr<galileo::node> node;
	std::mutex mutex;
	std::queue<std::unique_ptr<galileo::message>> requests;
};
class bulk_pull;
class bulk_pull_server : public std::enable_shared_from_this<galileo::bulk_pull_server>
{
public:
	bulk_pull_server (std::shared_ptr<galileo::bootstrap_server> const &, std::unique_ptr<galileo::bulk_pull>);
	void set_current_end ();
	std::unique_ptr<galileo::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<galileo::bootstrap_server> connection;
	std::unique_ptr<galileo::bulk_pull> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	galileo::block_hash current;
	bool include_start;
};
class bulk_pull_account;
class bulk_pull_account_server : public std::enable_shared_from_this<galileo::bulk_pull_account_server>
{
public:
	bulk_pull_account_server (std::shared_ptr<galileo::bootstrap_server> const &, std::unique_ptr<galileo::bulk_pull_account>);
	void set_params ();
	std::pair<std::unique_ptr<galileo::pending_key>, std::unique_ptr<galileo::pending_info>> get_next ();
	void send_frontier ();
	void send_next_block ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void complete (boost::system::error_code const &, size_t);
	std::shared_ptr<galileo::bootstrap_server> connection;
	std::unique_ptr<galileo::bulk_pull_account> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	std::unordered_map<galileo::uint256_union, bool> deduplication;
	galileo::pending_key current_key;
	bool pending_address_only;
	bool invalid_request;
};
class bulk_pull_blocks;
class bulk_pull_blocks_server : public std::enable_shared_from_this<galileo::bulk_pull_blocks_server>
{
public:
	bulk_pull_blocks_server (std::shared_ptr<galileo::bootstrap_server> const &, std::unique_ptr<galileo::bulk_pull_blocks>);
	void set_params ();
	std::unique_ptr<galileo::block> get_next ();
	void send_next ();
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<galileo::bootstrap_server> connection;
	std::unique_ptr<galileo::bulk_pull_blocks> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
};
class bulk_push_server : public std::enable_shared_from_this<galileo::bulk_push_server>
{
public:
	bulk_push_server (std::shared_ptr<galileo::bootstrap_server> const &);
	void receive ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, galileo::block_type);
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<galileo::bootstrap_server> connection;
};
class frontier_req;
class frontier_req_server : public std::enable_shared_from_this<galileo::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<galileo::bootstrap_server> const &, std::unique_ptr<galileo::frontier_req>);
	void skip_old ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	std::shared_ptr<galileo::bootstrap_server> connection;
	galileo::account current;
	galileo::account_info info;
	std::unique_ptr<galileo::frontier_req> request;
	std::shared_ptr<std::vector<uint8_t>> send_buffer;
	size_t count;
};
}
