#pragma once

#include <rai/lib/work.hpp>
#include <rai/node/bootstrap.hpp>
#include <rai/node/logging.hpp>
#include <rai/node/nodeconfig.hpp>
#include <rai/node/peers.hpp>
#include <rai/node/portmapping.hpp>
#include <rai/node/stats.hpp>
#include <rai/node/voting.hpp>
#include <rai/node/wallet.hpp>
#include <rai/secure/ledger.hpp>

#include <condition_variable>

#include <boost/iostreams/device/array.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

namespace rai
{
class node;
class election_status
{
public:
	std::shared_ptr<rai::block> winner;
	rai::amount tally;
	std::chrono::milliseconds election_end;
	std::chrono::milliseconds election_duration;
};
class vote_info
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	rai::block_hash hash;
};
class election_vote_result
{
public:
	election_vote_result ();
	election_vote_result (bool, bool);
	bool replay;
	bool processed;
};
class election : public std::enable_shared_from_this<rai::election>
{
	std::function<void(std::shared_ptr<rai::block>)> confirmation_action;
	void confirm_once (rai::transaction const &);
	void confirm_back (rai::transaction const &);

public:
	election (rai::node &, std::shared_ptr<rai::block>, std::function<void(std::shared_ptr<rai::block>)> const &);
	rai::election_vote_result vote (rai::account, uint64_t, rai::block_hash);
	rai::tally_t tally (rai::transaction const &);
	// Check if we have vote quorum
	bool have_quorum (rai::tally_t const &, rai::uint128_t);
	// Change our winner to agree with the network
	void compute_rep_votes (rai::transaction const &);
	// Confirm this block if quorum is met
	void confirm_if_quorum (rai::transaction const &);
	void log_votes (rai::tally_t const &);
	bool publish (std::shared_ptr<rai::block> block_a);
	void stop ();
	rai::node & node;
	std::unordered_map<rai::account, rai::vote_info> last_votes;
	std::unordered_map<rai::block_hash, std::shared_ptr<rai::block>> blocks;
	rai::block_hash root;
	std::chrono::steady_clock::time_point election_start;
	rai::election_status status;
	std::atomic<bool> confirmed;
	bool stopped;
	std::unordered_map<rai::block_hash, rai::uint128_t> last_tally;
	unsigned announcements;
};
class conflict_info
{
public:
	rai::block_hash root;
	uint64_t difficulty;
	std::shared_ptr<rai::election> election;
};
// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions
{
public:
	active_transactions (rai::node &);
	~active_transactions ();
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	bool start (std::shared_ptr<rai::block>, std::function<void(std::shared_ptr<rai::block>)> const & = [](std::shared_ptr<rai::block>) {});
	// If this returns true, the vote is a replay
	// If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<rai::vote>, bool = false);
	// Is the root of this block in the roots container
	bool active (rai::block const &);
	void update_difficulty (rai::block const &);
	std::deque<std::shared_ptr<rai::block>> list_blocks (bool = false);
	void erase (rai::block const &);
	void stop ();
	bool publish (std::shared_ptr<rai::block> block_a);
	boost::multi_index_container<
	rai::conflict_info,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<
	boost::multi_index::member<rai::conflict_info, rai::block_hash, &rai::conflict_info::root>>,
	boost::multi_index::ordered_non_unique<
	boost::multi_index::member<rai::conflict_info, uint64_t, &rai::conflict_info::difficulty>,
	std::greater<uint64_t>>>>
	roots;
	std::unordered_map<rai::block_hash, std::shared_ptr<rai::election>> blocks;
	std::deque<rai::election_status> confirmed;
	rai::node & node;
	std::mutex mutex;
	// Maximum number of conflicts to vote on per interval, lowest root hash first
	static unsigned constexpr announcements_per_interval = 32;
	// Minimum number of block announcements
	static unsigned constexpr announcement_min = 2;
	// Threshold to start logging blocks haven't yet been confirmed
	static unsigned constexpr announcement_long = 20;
	static unsigned constexpr announce_interval_ms = (rai::rai_network == rai::rai_networks::rai_test_network) ? 10 : 16000;
	static size_t constexpr election_history_size = 2048;
	static size_t constexpr max_broadcast_queue = 1000;

private:
	// Call action with confirmed block, may be different than what we started with
	bool add (std::shared_ptr<rai::block>, std::function<void(std::shared_ptr<rai::block>)> const & = [](std::shared_ptr<rai::block>) {});
	void announce_loop ();
	void announce_votes (std::unique_lock<std::mutex> &);
	std::condition_variable condition;
	bool started;
	bool stopped;
	boost::thread thread;
};
class operation
{
public:
	bool operator> (rai::operation const &) const;
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
class gap_information
{
public:
	std::chrono::steady_clock::time_point arrival;
	rai::block_hash hash;
	std::unordered_set<rai::account> voters;
};
class gap_cache
{
public:
	gap_cache (rai::node &);
	void add (rai::transaction const &, std::shared_ptr<rai::block>);
	void vote (std::shared_ptr<rai::vote>);
	rai::uint128_t bootstrap_threshold (rai::transaction const &);
	boost::multi_index_container<
	rai::gap_information,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<gap_information, rai::block_hash, &gap_information::hash>>>>
	blocks;
	size_t const max = 256;
	std::mutex mutex;
	rai::node & node;
};
class work_pool;
class send_info
{
public:
	uint8_t const * data;
	size_t size;
	rai::endpoint endpoint;
	std::function<void(boost::system::error_code const &, size_t)> callback;
};
class block_arrival_info
{
public:
	std::chrono::steady_clock::time_point arrival;
	rai::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (rai::block_hash const &);
	bool recent (rai::block_hash const &);
	boost::multi_index_container<
	rai::block_arrival_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<rai::block_arrival_info, std::chrono::steady_clock::time_point, &rai::block_arrival_info::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<rai::block_arrival_info, rai::block_hash, &rai::block_arrival_info::hash>>>>
	arrival;
	std::mutex mutex;
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};
class rep_last_heard_info
{
public:
	std::chrono::steady_clock::time_point last_heard;
	rai::account representative;
};
class online_reps
{
public:
	online_reps (rai::node &);
	void vote (std::shared_ptr<rai::vote> const &);
	void recalculate_stake ();
	rai::uint128_t online_stake ();
	rai::uint128_t online_stake_total;
	std::vector<rai::account> list ();
	boost::multi_index_container<
	rai::rep_last_heard_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<rai::rep_last_heard_info, std::chrono::steady_clock::time_point, &rai::rep_last_heard_info::last_heard>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<rai::rep_last_heard_info, rai::account, &rai::rep_last_heard_info::representative>>>>
	reps;

private:
	std::mutex mutex;
	rai::node & node;
};
class udp_data
{
public:
	uint8_t * buffer;
	size_t size;
	rai::endpoint endpoint;
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
	// Size - Size of each individual buffer
	// Count - Number of buffers to allocate
	// Stats - Statistics
	udp_buffer (rai::stat & stats, size_t, size_t);
	// Return a buffer where UDP data can be put
	// Method will attempt to return the first free buffer
	// If there are no free buffers, an unserviced buffer will be dequeued and returned
	// Function will block if there are no free or unserviced buffers
	// Return nullptr if the container has stopped
	rai::udp_data * allocate ();
	// Queue a buffer that has been filled with UDP data and notify servicing threads
	void enqueue (rai::udp_data *);
	// Return a buffer that has been filled with UDP data
	// Function will block until a buffer has been added
	// Return nullptr if the container has stopped
	rai::udp_data * dequeue ();
	// Return a buffer to the freelist after is has been serviced
	void release (rai::udp_data *);
	// Stop container and notify waiting threads
	void stop ();

private:
	rai::stat & stats;
	std::mutex mutex;
	std::condition_variable condition;
	boost::circular_buffer<rai::udp_data *> free;
	boost::circular_buffer<rai::udp_data *> full;
	std::vector<uint8_t> slab;
	std::vector<rai::udp_data> entries;
	bool stopped;
};
class network
{
public:
	network (rai::node &, uint16_t);
	~network ();
	void receive ();
	void process_packets ();
	void start ();
	void stop ();
	void receive_action (rai::udp_data *);
	void rpc_action (boost::system::error_code const &, size_t);
	void republish_vote (std::shared_ptr<rai::vote>);
	void republish_block (std::shared_ptr<rai::block>);
	static unsigned const broadcast_interval_ms = 10;
	void republish_block_batch (std::deque<std::shared_ptr<rai::block>>, unsigned = broadcast_interval_ms);
	void republish (rai::block_hash const &, std::shared_ptr<std::vector<uint8_t>>, rai::endpoint);
	void confirm_send (rai::confirm_ack const &, std::shared_ptr<std::vector<uint8_t>>, rai::endpoint const &);
	void merge_peers (std::array<rai::endpoint, 8> const &);
	void send_keepalive (rai::endpoint const &);
	void send_node_id_handshake (rai::endpoint const &, boost::optional<rai::uint256_union> const & query, boost::optional<rai::uint256_union> const & respond_to);
	void broadcast_confirm_req (std::shared_ptr<rai::block>);
	void broadcast_confirm_req_base (std::shared_ptr<rai::block>, std::shared_ptr<std::vector<rai::peer_information>>, unsigned, bool = false);
	void broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<rai::block>, std::shared_ptr<std::vector<rai::peer_information>>>>, unsigned = broadcast_interval_ms);
	void send_confirm_req (rai::endpoint const &, std::shared_ptr<rai::block>);
	void send_buffer (uint8_t const *, size_t, rai::endpoint const &, std::function<void(boost::system::error_code const &, size_t)>);
	rai::endpoint endpoint ();
	rai::udp_buffer buffer_container;
	boost::asio::ip::udp::socket socket;
	std::mutex socket_mutex;
	boost::asio::ip::udp::resolver resolver;
	std::vector<boost::thread> packet_processing_threads;
	rai::node & node;
	bool on;
	static uint16_t const node_port = rai::rai_network == rai::rai_networks::rai_live_network ? 7075 : 54000;
	static size_t const buffer_size = 512;
};

class node_init
{
public:
	node_init ();
	bool error ();
	bool block_store_init;
	bool wallet_init;
};
class node_observers
{
public:
	rai::observer_set<std::shared_ptr<rai::block>, rai::account const &, rai::uint128_t const &, bool> blocks;
	rai::observer_set<bool> wallet;
	rai::observer_set<rai::transaction const &, std::shared_ptr<rai::vote>, rai::endpoint const &> vote;
	rai::observer_set<rai::account const &, bool> account_balance;
	rai::observer_set<rai::endpoint const &> endpoint;
	rai::observer_set<> disconnect;
};
class vote_processor
{
public:
	vote_processor (rai::node &);
	void vote (std::shared_ptr<rai::vote>, rai::endpoint);
	// node.active.mutex lock required
	rai::vote_code vote_blocking (rai::transaction const &, std::shared_ptr<rai::vote>, rai::endpoint, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<rai::vote>, rai::endpoint>> &);
	void flush ();
	void calculate_weights ();
	rai::node & node;
	void stop ();

private:
	void process_loop ();
	std::deque<std::pair<std::shared_ptr<rai::vote>, rai::endpoint>> votes;
	// Representatives levels for random early detection
	std::unordered_set<rai::account> representatives_1;
	std::unordered_set<rai::account> representatives_2;
	std::unordered_set<rai::account> representatives_3;
	std::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool active;
	boost::thread thread;
};
// The network is crawled for representatives by occasionally sending a unicast confirm_req for a specific block and watching to see if it's acknowledged with a vote.
class rep_crawler
{
public:
	void add (rai::block_hash const &);
	void remove (rai::block_hash const &);
	bool exists (rai::block_hash const &);
	std::mutex mutex;
	std::unordered_set<rai::block_hash> active;
};
class block_processor;
class signature_check_set
{
public:
	size_t size;
	unsigned char const ** messages;
	size_t * message_lengths;
	unsigned char const ** pub_keys;
	unsigned char const ** signatures;
	int * verifications;
	std::promise<void> * promise;
};
class signature_checker
{
public:
	signature_checker ();
	~signature_checker ();
	void add (signature_check_set &);
	void stop ();
	void flush ();

private:
	void run ();
	void verify (rai::signature_check_set & check_a);
	std::deque<rai::signature_check_set> checks;
	bool started;
	bool stopped;
	std::mutex mutex;
	std::condition_variable condition;
	std::thread thread;
};
// Processing blocks is a potentially long IO operation
// This class isolates block insertion from other operations like servicing network operations
class block_processor
{
public:
	block_processor (rai::node &);
	~block_processor ();
	void stop ();
	void flush ();
	bool full ();
	void add (std::shared_ptr<rai::block>, std::chrono::steady_clock::time_point);
	void force (std::shared_ptr<rai::block>);
	bool should_log (bool);
	bool have_blocks ();
	void process_blocks ();
	rai::process_return process_receive_one (rai::transaction const &, std::shared_ptr<rai::block>, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now (), bool = false);

private:
	void queue_unchecked (rai::transaction const &, rai::block_hash const &);
	void verify_state_blocks (std::unique_lock<std::mutex> &, size_t = std::numeric_limits<size_t>::max ());
	void process_receive_many (std::unique_lock<std::mutex> &);
	bool stopped;
	bool active;
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::pair<std::shared_ptr<rai::block>, std::chrono::steady_clock::time_point>> state_blocks;
	std::deque<std::pair<std::shared_ptr<rai::block>, std::chrono::steady_clock::time_point>> blocks;
	std::unordered_set<rai::block_hash> blocks_hashes;
	std::deque<std::shared_ptr<rai::block>> forced;
	std::condition_variable condition;
	rai::node & node;
	rai::vote_generator generator;
	std::mutex mutex;
};
class node : public std::enable_shared_from_this<rai::node>
{
public:
	node (rai::node_init &, boost::asio::io_context &, uint16_t, boost::filesystem::path const &, rai::alarm &, rai::logging const &, rai::work_pool &);
	node (rai::node_init &, boost::asio::io_context &, boost::filesystem::path const &, rai::alarm &, rai::node_config const &, rai::work_pool &);
	~node ();
	template <typename T>
	void background (T action_a)
	{
		alarm.io_ctx.post (action_a);
	}
	void send_keepalive (rai::endpoint const &);
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<rai::node> shared ();
	int store_version ();
	void process_confirmed (std::shared_ptr<rai::block>);
	void process_message (rai::message &, rai::endpoint const &);
	void process_active (std::shared_ptr<rai::block>);
	rai::process_return process (rai::block const &);
	void keepalive_preconfigured (std::vector<std::string> const &);
	rai::block_hash latest (rai::account const &);
	rai::uint128_t balance (rai::account const &);
	std::shared_ptr<rai::block> block (rai::block_hash const &);
	std::pair<rai::uint128_t, rai::uint128_t> balance_pending (rai::account const &);
	rai::uint128_t weight (rai::account const &);
	rai::account representative (rai::account const &);
	void ongoing_keepalive ();
	void ongoing_syn_cookie_cleanup ();
	void ongoing_rep_crawl ();
	void ongoing_rep_calculation ();
	void ongoing_bootstrap ();
	void ongoing_store_flush ();
	void backup_wallet ();
	void search_pending ();
	int price (rai::uint128_t const &, int);
	void work_generate_blocking (rai::block &, uint64_t = rai::work_pool::publish_threshold);
	uint64_t work_generate_blocking (rai::uint256_union const &, uint64_t = rai::work_pool::publish_threshold);
	void work_generate (rai::uint256_union const &, std::function<void(uint64_t)>, uint64_t = rai::work_pool::publish_threshold);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<rai::block>);
	void process_fork (rai::transaction const &, std::shared_ptr<rai::block>);
	bool validate_block_by_previous (rai::transaction const &, std::shared_ptr<rai::block>);
	rai::uint128_t delta ();
	boost::asio::io_context & io_ctx;
	rai::node_config config;
	rai::node_flags flags;
	rai::alarm & alarm;
	rai::work_pool & work;
	boost::log::sources::logger_mt log;
	std::unique_ptr<rai::block_store> store_impl;
	rai::block_store & store;
	rai::gap_cache gap_cache;
	rai::ledger ledger;
	rai::active_transactions active;
	rai::network network;
	rai::bootstrap_initiator bootstrap_initiator;
	rai::bootstrap_listener bootstrap;
	rai::peer_container peers;
	boost::filesystem::path application_path;
	rai::node_observers observers;
	rai::wallets wallets;
	rai::port_mapping port_mapping;
	rai::signature_checker checker;
	rai::vote_processor vote_processor;
	rai::rep_crawler rep_crawler;
	unsigned warmed_up;
	rai::block_processor block_processor;
	boost::thread block_processor_thread;
	rai::block_arrival block_arrival;
	rai::online_reps online_reps;
	rai::stat stats;
	rai::keypair node_id;
	rai::block_uniquer block_uniquer;
	rai::vote_uniquer vote_uniquer;
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	static std::chrono::seconds constexpr period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr cutoff = period * 5;
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
	static std::chrono::seconds constexpr search_pending_interval = (rai::rai_network == rai::rai_networks::rai_test_network) ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
};
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
	inactive_node (boost::filesystem::path const & path = rai::working_path ());
	~inactive_node ();
	boost::filesystem::path path;
	std::shared_ptr<boost::asio::io_context> io_context;
	rai::alarm alarm;
	rai::logging logging;
	rai::node_init init;
	rai::work_pool work;
	std::shared_ptr<rai::node> node;
};
}
