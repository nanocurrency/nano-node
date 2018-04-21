#pragma once

#include <banano/blockstore.hpp>
#include <banano/ledger.hpp>
#include <banano/node/common.hpp>

#include <atomic>
#include <future>
#include <queue>
#include <stack>
#include <unordered_set>

#include <boost/log/sources/logger.hpp>

namespace rai
{
class bootstrap_attempt;
class node;
enum class sync_result
{
	success,
	error,
	fork
};

/**
 * The length of every message header, parsed by rai::message::read_header ()
 * The 2 here represents the size of a std::bitset<16>, which is 2 chars long normally
 */
static const int bootstrap_message_header_size = sizeof (rai::message::magic_number) + sizeof (uint8_t) + sizeof (uint8_t) + sizeof (uint8_t) + sizeof (rai::message_type) + 2;

class block_synchronization
{
public:
	block_synchronization (boost::log::sources::logger_mt &);
	virtual ~block_synchronization () = default;
	// Return true if target already has block
	virtual bool synchronized (MDB_txn *, rai::block_hash const &) = 0;
	virtual std::unique_ptr<rai::block> retrieve (MDB_txn *, rai::block_hash const &) = 0;
	virtual rai::sync_result target (MDB_txn *, rai::block const &) = 0;
	// return true if all dependencies are synchronized
	bool add_dependency (MDB_txn *, rai::block const &);
	void fill_dependencies (MDB_txn *);
	rai::sync_result synchronize_one (MDB_txn *);
	rai::sync_result synchronize (MDB_txn *, rai::block_hash const &);
	boost::log::sources::logger_mt & log;
	std::deque<rai::block_hash> blocks;
};
class push_synchronization : public rai::block_synchronization
{
public:
	push_synchronization (rai::node &, std::function<rai::sync_result (MDB_txn *, rai::block const &)> const &);
	virtual ~push_synchronization () = default;
	bool synchronized (MDB_txn *, rai::block_hash const &) override;
	std::unique_ptr<rai::block> retrieve (MDB_txn *, rai::block_hash const &) override;
	rai::sync_result target (MDB_txn *, rai::block const &) override;
	std::function<rai::sync_result (MDB_txn *, rai::block const &)> target_m;
	rai::node & node;
};
class bootstrap_client;
class pull_info
{
public:
	pull_info ();
	pull_info (rai::account const &, rai::block_hash const &, rai::block_hash const &);
	rai::account account;
	rai::block_hash head;
	rai::block_hash end;
	unsigned attempts;
};
class frontier_req_client;
class bulk_push_client;
class bootstrap_attempt : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	bootstrap_attempt (std::shared_ptr<rai::node> node_a);
	~bootstrap_attempt ();
	void run ();
	std::shared_ptr<rai::bootstrap_client> connection (std::unique_lock<std::mutex> &);
	bool consume_future (std::future<bool> &);
	void populate_connections ();
	bool request_frontier (std::unique_lock<std::mutex> &);
	void request_pull (std::unique_lock<std::mutex> &);
	bool request_push (std::unique_lock<std::mutex> &);
	void add_connection (rai::endpoint const &);
	void pool_connection (std::shared_ptr<rai::bootstrap_client>);
	void stop ();
	void requeue_pull (rai::pull_info const &);
	void add_pull (rai::pull_info const &);
	bool still_pulling ();
	void process_fork (MDB_txn *, std::shared_ptr<rai::block>);
	unsigned target_connections (size_t pulls_remaining);
	bool should_log ();
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::weak_ptr<rai::bootstrap_client>> clients;
	std::weak_ptr<rai::bootstrap_client> connection_frontier_request;
	std::weak_ptr<rai::frontier_req_client> frontiers;
	std::weak_ptr<rai::bulk_push_client> push;
	std::deque<rai::pull_info> pulls;
	std::deque<std::shared_ptr<rai::bootstrap_client>> idle;
	std::atomic<unsigned> connections;
	std::atomic<unsigned> pulling;
	std::shared_ptr<rai::node> node;
	std::atomic<unsigned> account_count;
	std::atomic<uint64_t> total_blocks;
	bool stopped;
	std::mutex mutex;
	std::condition_variable condition;
};
class frontier_req_client : public std::enable_shared_from_this<rai::frontier_req_client>
{
public:
	frontier_req_client (std::shared_ptr<rai::bootstrap_client>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	void request_account (rai::account const &, rai::block_hash const &);
	void unsynced (MDB_txn *, rai::account const &, rai::block_hash const &);
	void next (MDB_txn *);
	void insert_pull (rai::pull_info const &);
	std::shared_ptr<rai::bootstrap_client> connection;
	rai::account current;
	rai::account_info info;
	unsigned count;
	rai::account landing;
	rai::account faucet;
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
};
class bulk_pull_client : public std::enable_shared_from_this<rai::bulk_pull_client>
{
public:
	bulk_pull_client (std::shared_ptr<rai::bootstrap_client>, rai::pull_info const &);
	~bulk_pull_client ();
	void request ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t);
	rai::block_hash first ();
	std::shared_ptr<rai::bootstrap_client> connection;
	rai::block_hash expected;
	rai::pull_info pull;
};
class bootstrap_client : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<rai::node>, std::shared_ptr<rai::bootstrap_attempt>, rai::tcp_endpoint const &);
	~bootstrap_client ();
	void run ();
	std::shared_ptr<rai::bootstrap_client> shared ();
	void start_timeout ();
	void stop_timeout ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	std::shared_ptr<rai::node> node;
	std::shared_ptr<rai::bootstrap_attempt> attempt;
	boost::asio::ip::tcp::socket socket;
	std::array<uint8_t, 200> receive_buffer;
	rai::tcp_endpoint endpoint;
	boost::asio::deadline_timer timeout;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count;
	std::atomic<bool> pending_stop;
	std::atomic<bool> hard_stop;
};
class bulk_push_client : public std::enable_shared_from_this<rai::bulk_push_client>
{
public:
	bulk_push_client (std::shared_ptr<rai::bootstrap_client> const &);
	~bulk_push_client ();
	void start ();
	void push (MDB_txn *);
	void push_block (rai::block const &);
	void send_finished ();
	std::shared_ptr<rai::bootstrap_client> connection;
	rai::push_synchronization synchronization;
	std::promise<bool> promise;
};
class bootstrap_initiator
{
public:
	bootstrap_initiator (rai::node &);
	~bootstrap_initiator ();
	void bootstrap (rai::endpoint const &);
	void bootstrap ();
	void run_bootstrap ();
	void notify_listeners (bool);
	void add_observer (std::function<void(bool)> const &);
	bool in_progress ();
	std::shared_ptr<rai::bootstrap_attempt> current_attempt ();
	void process_fork (MDB_txn *, std::shared_ptr<rai::block>);
	void stop ();

private:
	rai::node & node;
	std::shared_ptr<rai::bootstrap_attempt> attempt;
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
	bootstrap_listener (boost::asio::io_service &, uint16_t, rai::node &);
	void start ();
	void stop ();
	void accept_connection ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<boost::asio::ip::tcp::socket>);
	std::mutex mutex;
	std::unordered_map<rai::bootstrap_server *, std::weak_ptr<rai::bootstrap_server>> connections;
	rai::tcp_endpoint endpoint ();
	boost::asio::ip::tcp::acceptor acceptor;
	rai::tcp_endpoint local;
	boost::asio::io_service & service;
	rai::node & node;
	bool on;
};
class message;
class bootstrap_server : public std::enable_shared_from_this<rai::bootstrap_server>
{
public:
	bootstrap_server (std::shared_ptr<boost::asio::ip::tcp::socket>, std::shared_ptr<rai::node>);
	~bootstrap_server ();
	void receive ();
	void receive_header_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_blocks_action (boost::system::error_code const &, size_t);
	void receive_frontier_req_action (boost::system::error_code const &, size_t);
	void receive_bulk_push_action ();
	void add_request (std::unique_ptr<rai::message>);
	void finish_request ();
	void run_next ();
	std::array<uint8_t, 128> receive_buffer;
	std::shared_ptr<boost::asio::ip::tcp::socket> socket;
	std::shared_ptr<rai::node> node;
	std::mutex mutex;
	std::queue<std::unique_ptr<rai::message>> requests;
};
class bulk_pull;
class bulk_pull_server : public std::enable_shared_from_this<rai::bulk_pull_server>
{
public:
	bulk_pull_server (std::shared_ptr<rai::bootstrap_server> const &, std::unique_ptr<rai::bulk_pull>);
	void set_current_end ();
	std::unique_ptr<rai::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<rai::bootstrap_server> connection;
	std::unique_ptr<rai::bulk_pull> request;
	std::vector<uint8_t> send_buffer;
	rai::block_hash current;
};
class bulk_pull_blocks;
class bulk_pull_blocks_server : public std::enable_shared_from_this<rai::bulk_pull_blocks_server>
{
public:
	bulk_pull_blocks_server (std::shared_ptr<rai::bootstrap_server> const &, std::unique_ptr<rai::bulk_pull_blocks>);
	void set_params ();
	std::unique_ptr<rai::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<rai::bootstrap_server> connection;
	std::unique_ptr<rai::bulk_pull_blocks> request;
	std::vector<uint8_t> send_buffer;
	rai::store_iterator stream;
	rai::transaction stream_transaction;
	uint32_t sent_count;
	rai::block_hash checksum;
};
class bulk_push_server : public std::enable_shared_from_this<rai::bulk_push_server>
{
public:
	bulk_push_server (std::shared_ptr<rai::bootstrap_server> const &);
	void receive ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t);
	std::array<uint8_t, 256> receive_buffer;
	std::shared_ptr<rai::bootstrap_server> connection;
};
class frontier_req;
class frontier_req_server : public std::enable_shared_from_this<rai::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<rai::bootstrap_server> const &, std::unique_ptr<rai::frontier_req>);
	void skip_old ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	std::shared_ptr<rai::bootstrap_server> connection;
	rai::account current;
	rai::account_info info;
	std::unique_ptr<rai::frontier_req> request;
	std::vector<uint8_t> send_buffer;
	size_t count;
};
}
