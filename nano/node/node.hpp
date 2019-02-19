#pragma once

#include <nano/lib/work.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/bootstrap.hpp>
#include <nano/node/logging.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/peers.hpp>
#include <nano/node/portmapping.hpp>
#include <nano/node/signatures.hpp>
#include <nano/node/stats.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <queue>

#include <boost/asio/thread_pool.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

#define xstr(a) ver_str (a)
#define ver_str(a) #a

/**
* Returns build version information
*/
static const char * NANO_MAJOR_MINOR_VERSION = xstr (NANO_VERSION_MAJOR) "." xstr (NANO_VERSION_MINOR);
static const char * NANO_MAJOR_MINOR_RC_VERSION = xstr (NANO_VERSION_MAJOR) "." xstr (NANO_VERSION_MINOR) "RC" xstr (NANO_VERSION_PATCH);

namespace nano
{
class node;
class election_status
{
public:
	std::shared_ptr<nano::block> winner;
	nano::amount tally;
	std::chrono::milliseconds election_end;
	std::chrono::milliseconds election_duration;
};
class vote_info
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	nano::block_hash hash;
};
class election_vote_result
{
public:
	election_vote_result ();
	election_vote_result (bool, bool);
	bool replay;
	bool processed;
};
class election : public std::enable_shared_from_this<nano::election>
{
	std::function<void(std::shared_ptr<nano::block>)> confirmation_action;
	void confirm_once (nano::transaction const &, bool = false);
	void confirm_back (nano::transaction const &);

public:
	election (nano::node &, std::shared_ptr<nano::block>, std::function<void(std::shared_ptr<nano::block>)> const &);
	nano::election_vote_result vote (nano::account, uint64_t, nano::block_hash);
	nano::tally_t tally (nano::transaction const &);
	// Check if we have vote quorum
	bool have_quorum (nano::tally_t const &, nano::uint128_t);
	// Change our winner to agree with the network
	void compute_rep_votes (nano::transaction const &);
	// Confirm this block if quorum is met
	void confirm_if_quorum (nano::transaction const &);
	void log_votes (nano::tally_t const &);
	bool publish (std::shared_ptr<nano::block> block_a);
	size_t last_votes_size ();
	void stop ();
	nano::node & node;
	std::unordered_map<nano::account, nano::vote_info> last_votes;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks;
	std::chrono::steady_clock::time_point election_start;
	nano::election_status status;
	std::atomic<bool> confirmed;
	bool stopped;
	std::unordered_map<nano::block_hash, nano::uint128_t> last_tally;
	unsigned announcements;
};
class conflict_info
{
public:
	nano::uint512_union root;
	uint64_t difficulty;
	std::shared_ptr<nano::election> election;
};
// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions
{
public:
	active_transactions (nano::node &);
	~active_transactions ();
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	bool start (std::shared_ptr<nano::block>, std::function<void(std::shared_ptr<nano::block>)> const & = [](std::shared_ptr<nano::block>) {});
	// clang-format on
	// If this returns true, the vote is a replay
	// If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<nano::vote>, bool = false);
	// Is the root of this block in the roots container
	bool active (nano::block const &);
	void update_difficulty (nano::block const &);
	std::deque<std::shared_ptr<nano::block>> list_blocks (bool = false);
	void erase (nano::block const &);
	bool empty ();
	size_t size ();
	void stop ();
	bool publish (std::shared_ptr<nano::block> block_a);
	boost::multi_index_container<
	nano::conflict_info,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<
	boost::multi_index::member<nano::conflict_info, nano::uint512_union, &nano::conflict_info::root>>,
	boost::multi_index::ordered_non_unique<
	boost::multi_index::member<nano::conflict_info, uint64_t, &nano::conflict_info::difficulty>,
	std::greater<uint64_t>>>>
	roots;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> blocks;
	std::deque<nano::election_status> list_confirmed ();
	std::deque<nano::election_status> confirmed;
	nano::node & node;
	std::mutex mutex;
	// Maximum number of conflicts to vote on per interval, lowest root hash first
	static unsigned constexpr announcements_per_interval = 32;
	// Minimum number of block announcements
	static unsigned constexpr announcement_min = 2;
	// Threshold to start logging blocks haven't yet been confirmed
	static unsigned constexpr announcement_long = 20;
	static unsigned constexpr request_interval_ms = nano::is_test_network ? 10 : 16000;
	static size_t constexpr election_history_size = 2048;
	static size_t constexpr max_broadcast_queue = 1000;

private:
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	bool add (std::shared_ptr<nano::block>, std::function<void(std::shared_ptr<nano::block>)> const & = [](std::shared_ptr<nano::block>) {});
	// clang-format on
	void request_loop ();
	void request_confirm (std::unique_lock<std::mutex> &);
	std::condition_variable condition;
	bool started;
	bool stopped;
	boost::thread thread;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name);

class operation
{
public:
	bool operator> (nano::operation const &) const;
	std::chrono::steady_clock::time_point wakeup;
	std::function<void()> function;
};
class alarm
{
public:
	alarm (boost::asio::io_context &);
	~alarm ();
	void add (std::chrono::steady_clock::time_point const &, std::function<void()> const &);
	void run ();
	boost::asio::io_context & io_ctx;
	std::mutex mutex;
	std::condition_variable condition;
	std::priority_queue<operation, std::vector<operation>, std::greater<operation>> operations;
	boost::thread thread;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (alarm & alarm, const std::string & name);

class gap_information
{
public:
	std::chrono::steady_clock::time_point arrival;
	nano::block_hash hash;
	std::unordered_set<nano::account> voters;
};
class gap_cache
{
public:
	gap_cache (nano::node &);
	void add (nano::transaction const &, nano::block_hash const &, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now ());
	void vote (std::shared_ptr<nano::vote>);
	nano::uint128_t bootstrap_threshold (nano::transaction const &);
	size_t size ();
	boost::multi_index_container<
	nano::gap_information,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<gap_information, nano::block_hash, &gap_information::hash>>>>
	blocks;
	size_t const max = 256;
	std::mutex mutex;
	nano::node & node;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (gap_cache & gap_cache, const std::string & name);

class work_pool;
class send_info
{
public:
	uint8_t const * data;
	size_t size;
	nano::endpoint endpoint;
	std::function<void(boost::system::error_code const &, size_t)> callback;
};
class block_arrival_info
{
public:
	std::chrono::steady_clock::time_point arrival;
	nano::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (nano::block_hash const &);
	bool recent (nano::block_hash const &);
	boost::multi_index_container<
	nano::block_arrival_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<nano::block_arrival_info, std::chrono::steady_clock::time_point, &nano::block_arrival_info::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<nano::block_arrival_info, nano::block_hash, &nano::block_arrival_info::hash>>>>
	arrival;
	std::mutex mutex;
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_arrival & block_arrival, const std::string & name);

class online_reps
{
public:
	online_reps (nano::ledger &, nano::uint128_t);
	void observe (nano::account const &);
	void sample ();
	nano::uint128_t online_stake ();
	std::vector<nano::account> list ();
	static uint64_t constexpr weight_period = 5 * 60; // 5 minutes
	// The maximum amount of samples for a 2 week period on live or 3 days on beta
	static uint64_t constexpr weight_samples = nano::is_live_network ? 4032 : 864;

private:
	nano::uint128_t trend (nano::transaction &);
	std::mutex mutex;
	nano::ledger & ledger;
	std::unordered_set<nano::account> reps;
	nano::uint128_t online;
	nano::uint128_t minimum;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name);

class udp_data
{
public:
	uint8_t * buffer;
	size_t size;
	nano::endpoint endpoint;
};
/**
  * A circular buffer for servicing UDP datagrams. This container follows a producer/consumer model where the operating system is producing data in to buffers which are serviced by internal threads.
  * If buffers are not serviced fast enough they're internally dropped.
  * This container has a maximum space to hold N buffers of M size and will allocate them in round-robin order.
  * All public methods are thread-safe
*/
class udp_buffer
{
public:
	// Stats - Statistics
	// Size - Size of each individual buffer
	// Count - Number of buffers to allocate
	udp_buffer (nano::stat & stats, size_t, size_t);
	// Return a buffer where UDP data can be put
	// Method will attempt to return the first free buffer
	// If there are no free buffers, an unserviced buffer will be dequeued and returned
	// Function will block if there are no free or unserviced buffers
	// Return nullptr if the container has stopped
	nano::udp_data * allocate ();
	// Queue a buffer that has been filled with UDP data and notify servicing threads
	void enqueue (nano::udp_data *);
	// Return a buffer that has been filled with UDP data
	// Function will block until a buffer has been added
	// Return nullptr if the container has stopped
	nano::udp_data * dequeue ();
	// Return a buffer to the freelist after is has been serviced
	void release (nano::udp_data *);
	// Stop container and notify waiting threads
	void stop ();

private:
	nano::stat & stats;
	std::mutex mutex;
	std::condition_variable condition;
	boost::circular_buffer<nano::udp_data *> free;
	boost::circular_buffer<nano::udp_data *> full;
	std::vector<uint8_t> slab;
	std::vector<nano::udp_data> entries;
	bool stopped;
};
class network
{
public:
	network (nano::node &, uint16_t);
	~network ();
	void receive ();
	void process_packets ();
	void start ();
	void stop ();
	void receive_action (nano::udp_data *, nano::endpoint const &);
	void rpc_action (boost::system::error_code const &, size_t);
	void republish_vote (std::shared_ptr<nano::vote>);
	void republish_block (std::shared_ptr<nano::block>);
	void republish_block (std::shared_ptr<nano::block>, nano::endpoint const &);
	static unsigned const broadcast_interval_ms = 10;
	void republish_block_batch (std::deque<std::shared_ptr<nano::block>>, unsigned = broadcast_interval_ms);
	void republish (nano::block_hash const &, std::shared_ptr<std::vector<uint8_t>>, nano::endpoint);
	void confirm_send (nano::confirm_ack const &, std::shared_ptr<std::vector<uint8_t>>, nano::endpoint const &);
	void merge_peers (std::array<nano::endpoint, 8> const &);
	void send_keepalive (nano::endpoint const &);
	void send_node_id_handshake (nano::endpoint const &, boost::optional<nano::uint256_union> const & query, boost::optional<nano::uint256_union> const & respond_to);
	void broadcast_confirm_req (std::shared_ptr<nano::block>);
	void broadcast_confirm_req_base (std::shared_ptr<nano::block>, std::shared_ptr<std::vector<nano::peer_information>>, unsigned, bool = false);
	void broadcast_confirm_req_batch (std::unordered_map<nano::endpoint, std::vector<std::pair<nano::block_hash, nano::block_hash>>>, unsigned = broadcast_interval_ms, bool = false);
	void broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<nano::peer_information>>>>, unsigned = broadcast_interval_ms);
	void send_confirm_req (nano::endpoint const &, std::shared_ptr<nano::block>);
	void send_confirm_req_hashes (nano::endpoint const &, std::vector<std::pair<nano::block_hash, nano::block_hash>> const &);
	void confirm_hashes (nano::transaction const &, nano::endpoint const &, std::vector<nano::block_hash>);
	bool send_votes_cache (nano::block_hash const &, nano::endpoint const &);
	void send_buffer (uint8_t const *, size_t, nano::endpoint const &, std::function<void(boost::system::error_code const &, size_t)>);
	nano::endpoint endpoint ();
	nano::udp_buffer buffer_container;
	boost::asio::ip::udp::socket socket;
	std::mutex socket_mutex;
	boost::asio::ip::udp::resolver resolver;
	std::vector<boost::thread> packet_processing_threads;
	nano::node & node;
	std::atomic<bool> on;
	static uint16_t const node_port = nano::is_live_network ? 7075 : 54000;
	static size_t const buffer_size = 512;
	static size_t const confirm_req_hashes_max = 6;
};

class node_init
{
public:
	node_init ();
	bool error ();
	bool block_store_init;
	bool wallets_store_init;
	bool wallet_init;
};
class node_observers
{
public:
	nano::observer_set<std::shared_ptr<nano::block>, nano::account const &, nano::uint128_t const &, bool> blocks;
	nano::observer_set<bool> wallet;
	nano::observer_set<nano::transaction const &, std::shared_ptr<nano::vote>, nano::endpoint const &> vote;
	nano::observer_set<nano::account const &, bool> account_balance;
	nano::observer_set<nano::endpoint const &> endpoint;
	nano::observer_set<> disconnect;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (node_observers & node_observers, const std::string & name);

class vote_processor
{
public:
	vote_processor (nano::node &);
	void vote (std::shared_ptr<nano::vote>, nano::endpoint);
	// node.active.mutex lock required
	nano::vote_code vote_blocking (nano::transaction const &, std::shared_ptr<nano::vote>, nano::endpoint, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<nano::vote>, nano::endpoint>> &);
	void flush ();
	void calculate_weights ();
	nano::node & node;
	void stop ();

private:
	void process_loop ();
	std::deque<std::pair<std::shared_ptr<nano::vote>, nano::endpoint>> votes;
	// Representatives levels for random early detection
	std::unordered_set<nano::account> representatives_1;
	std::unordered_set<nano::account> representatives_2;
	std::unordered_set<nano::account> representatives_3;
	std::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool active;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);

// The network is crawled for representatives by occasionally sending a unicast confirm_req for a specific block and watching to see if it's acknowledged with a vote.
class rep_crawler
{
public:
	void add (nano::block_hash const &);
	void remove (nano::block_hash const &);
	bool exists (nano::block_hash const &);
	std::mutex mutex;
	std::unordered_set<nano::block_hash> active;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_crawler & rep_crawler, const std::string & name);
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name);

class node : public std::enable_shared_from_this<nano::node>
{
public:
	node (nano::node_init &, boost::asio::io_context &, uint16_t, boost::filesystem::path const &, nano::alarm &, nano::logging const &, nano::work_pool &);
	node (nano::node_init &, boost::asio::io_context &, boost::filesystem::path const &, nano::alarm &, nano::node_config const &, nano::work_pool &, nano::node_flags = nano::node_flags ());
	~node ();
	template <typename T>
	void background (T action_a)
	{
		alarm.io_ctx.post (action_a);
	}
	void send_keepalive (nano::endpoint const &);
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t, bool = false);
	void start ();
	void stop ();
	std::shared_ptr<nano::node> shared ();
	int store_version ();
	void process_confirmed (std::shared_ptr<nano::block>, uint8_t = 0);
	void process_message (nano::message &, nano::endpoint const &);
	void process_active (std::shared_ptr<nano::block>);
	nano::process_return process (nano::block const &);
	void keepalive_preconfigured (std::vector<std::string> const &);
	nano::block_hash latest (nano::account const &);
	nano::uint128_t balance (nano::account const &);
	std::shared_ptr<nano::block> block (nano::block_hash const &);
	std::pair<nano::uint128_t, nano::uint128_t> balance_pending (nano::account const &);
	nano::uint128_t weight (nano::account const &);
	nano::account representative (nano::account const &);
	void ongoing_keepalive ();
	void ongoing_syn_cookie_cleanup ();
	void ongoing_rep_crawl ();
	void ongoing_rep_calculation ();
	void ongoing_bootstrap ();
	void ongoing_store_flush ();
	void ongoing_peer_store ();
	void ongoing_unchecked_cleanup ();
	void backup_wallet ();
	void search_pending ();
	void bootstrap_wallet ();
	void unchecked_cleanup ();
	int price (nano::uint128_t const &, int);
	void work_generate_blocking (nano::block &, uint64_t = nano::work_pool::publish_threshold);
	uint64_t work_generate_blocking (nano::uint256_union const &, uint64_t = nano::work_pool::publish_threshold);
	void work_generate (nano::uint256_union const &, std::function<void(uint64_t)>, uint64_t = nano::work_pool::publish_threshold);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<nano::block>);
	void process_fork (nano::transaction const &, std::shared_ptr<nano::block>);
	bool validate_block_by_previous (nano::transaction const &, std::shared_ptr<nano::block>);
	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string>, std::shared_ptr<std::string>, std::shared_ptr<boost::asio::ip::tcp::resolver>);
	nano::uint128_t delta ();
	void ongoing_online_weight_calculation ();
	void ongoing_online_weight_calculation_queue ();
	boost::asio::io_context & io_ctx;
	nano::node_config config;
	nano::node_flags flags;
	nano::alarm & alarm;
	nano::work_pool & work;
	boost::log::sources::logger_mt log;
	std::unique_ptr<nano::block_store> store_impl;
	nano::block_store & store;
	std::unique_ptr<nano::wallets_store> wallets_store_impl;
	nano::wallets_store & wallets_store;
	nano::gap_cache gap_cache;
	nano::ledger ledger;
	nano::active_transactions active;
	nano::network network;
	nano::bootstrap_initiator bootstrap_initiator;
	nano::bootstrap_listener bootstrap;
	nano::peer_container peers;
	boost::filesystem::path application_path;
	nano::node_observers observers;
	nano::wallets wallets;
	nano::port_mapping port_mapping;
	nano::signature_checker checker;
	nano::vote_processor vote_processor;
	nano::rep_crawler rep_crawler;
	unsigned warmed_up;
	nano::block_processor block_processor;
	boost::thread block_processor_thread;
	nano::block_arrival block_arrival;
	nano::online_reps online_reps;
	nano::votes_cache votes_cache;
	nano::stat stats;
	nano::keypair node_id;
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer;
	const std::chrono::steady_clock::time_point startup_time;
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	static std::chrono::seconds constexpr period = nano::is_test_network ? std::chrono::seconds (1) : std::chrono::seconds (60);
	static std::chrono::seconds constexpr cutoff = period * 5;
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
	static std::chrono::seconds constexpr search_pending_interval = nano::is_test_network ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	static std::chrono::seconds constexpr peer_interval = search_pending_interval;
	static std::chrono::hours constexpr unchecked_cleanup_interval = std::chrono::hours (1);
	static std::chrono::milliseconds constexpr process_confirmed_interval = nano::is_test_network ? std::chrono::milliseconds (50) : std::chrono::milliseconds (500);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (node & node, const std::string & name);

class thread_runner
{
public:
	thread_runner (boost::asio::io_context &, unsigned);
	~thread_runner ();
	void join ();
	std::vector<boost::thread> threads;
};
class inactive_node
{
public:
	inactive_node (boost::filesystem::path const & path = nano::working_path (), uint16_t = 24000);
	~inactive_node ();
	boost::filesystem::path path;
	std::shared_ptr<boost::asio::io_context> io_context;
	nano::alarm alarm;
	nano::logging logging;
	nano::node_init init;
	nano::work_pool work;
	uint16_t peering_port;
	std::shared_ptr<nano::node> node;
};
}
