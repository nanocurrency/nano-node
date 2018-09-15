#pragma once

#include <galileo/lib/work.hpp>
#include <galileo/node/bootstrap.hpp>
#include <galileo/node/stats.hpp>
#include <galileo/node/wallet.hpp>
#include <galileo/secure/ledger.hpp>

#include <condition_variable>

#include <boost/iostreams/device/array.hpp>
#include <boost/log/trivial.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <miniupnpc.h>

namespace boost
{
namespace program_options
{
	class options_description;
	class variables_map;
}
}

namespace galileo
{
galileo::endpoint map_endpoint_to_v6 (galileo::endpoint const &);
class node;
class election_status
{
public:
	std::shared_ptr<galileo::block> winner;
	galileo::amount tally;
};
class vote_info
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	galileo::block_hash hash;
};
class election_vote_result
{
public:
	election_vote_result ();
	election_vote_result (bool, bool);
	bool replay;
	bool processed;
};
class election : public std::enable_shared_from_this<galileo::election>
{
	std::function<void(std::shared_ptr<galileo::block>)> confirmation_action;
	void confirm_once (galileo::transaction const &);

public:
	election (galileo::node &, std::shared_ptr<galileo::block>, std::function<void(std::shared_ptr<galileo::block>)> const &);
	galileo::election_vote_result vote (galileo::account, uint64_t, galileo::block_hash);
	galileo::tally_t tally (galileo::transaction const &);
	// Check if we have vote quorum
	bool have_quorum (galileo::tally_t const &);
	// Change our winner to agree with the network
	void compute_rep_votes (galileo::transaction const &);
	// Confirm this block if quorum is met
	void confirm_if_quorum (galileo::transaction const &);
	void log_votes (galileo::tally_t const &);
	bool publish (std::shared_ptr<galileo::block> block_a);
	void abort ();
	galileo::node & node;
	std::unordered_map<galileo::account, galileo::vote_info> last_votes;
	std::unordered_map<galileo::block_hash, std::shared_ptr<galileo::block>> blocks;
	galileo::block_hash root;
	galileo::election_status status;
	std::atomic<bool> confirmed;
	bool aborted;
	std::unordered_map<galileo::block_hash, galileo::uint128_t> last_tally;
};
class conflict_info
{
public:
	galileo::block_hash root;
	std::shared_ptr<galileo::election> election;
	// Number of announcements in a row for this fork
	unsigned announcements;
	std::pair<std::shared_ptr<galileo::block>, std::shared_ptr<galileo::block>> confirm_req_options;
};
// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions
{
public:
	active_transactions (galileo::node &);
	~active_transactions ();
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	bool start (std::shared_ptr<galileo::block>, std::function<void(std::shared_ptr<galileo::block>)> const & = [](std::shared_ptr<galileo::block>) {});
	// Also supply alternatives to block, to confirm_req reps with if the boolean argument is true
	// Should only be used for old elections
	// The first block should be the one in the ledger
	bool start (std::pair<std::shared_ptr<galileo::block>, std::shared_ptr<galileo::block>>, std::function<void(std::shared_ptr<galileo::block>)> const & = [](std::shared_ptr<galileo::block>) {});
	// If this returns true, the vote is a replay
	// If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<galileo::vote>);
	// Is the root of this block in the roots container
	bool active (galileo::block const &);
	std::deque<std::shared_ptr<galileo::block>> list_blocks ();
	void erase (galileo::block const &);
	void stop ();
	bool publish (std::shared_ptr<galileo::block> block_a);
	boost::multi_index_container<
	galileo::conflict_info,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<galileo::conflict_info, galileo::block_hash, &galileo::conflict_info::root>>>>
	roots;
	std::unordered_map<galileo::block_hash, std::shared_ptr<galileo::election>> successors;
	std::deque<galileo::election_status> confirmed;
	galileo::node & node;
	std::mutex mutex;
	// Maximum number of conflicts to vote on per interval, lowest root hash first
	static unsigned constexpr announcements_per_interval = 32;
	// Minimum number of block announcements
	static unsigned constexpr announcement_min = 2;
	// Threshold to start logging blocks haven't yet been confirmed
	static unsigned constexpr announcement_long = 20;
	static unsigned constexpr announce_interval_ms = (galileo::galileo_network == galileo::galileo_networks::galileo_test_network) ? 10 : 16000;
	static size_t constexpr election_history_size = 2048;

private:
	void announce_loop ();
	void announce_votes ();
	std::condition_variable condition;
	bool started;
	bool stopped;
	std::thread thread;
};
class operation
{
public:
	bool operator> (galileo::operation const &) const;
	std::chrono::steady_clock::time_point wakeup;
	std::function<void()> function;
};
class alarm
{
public:
	alarm (boost::asio::io_service &);
	~alarm ();
	void add (std::chrono::steady_clock::time_point const &, std::function<void()> const &);
	void run ();
	boost::asio::io_service & service;
	std::mutex mutex;
	std::condition_variable condition;
	std::priority_queue<operation, std::vector<operation>, std::greater<operation>> operations;
	std::thread thread;
};
class gap_information
{
public:
	std::chrono::steady_clock::time_point arrival;
	galileo::block_hash hash;
	std::unordered_set<galileo::account> voters;
};
class gap_cache
{
public:
	gap_cache (galileo::node &);
	void add (galileo::transaction const &, std::shared_ptr<galileo::block>);
	void vote (std::shared_ptr<galileo::vote>);
	galileo::uint128_t bootstrap_threshold (galileo::transaction const &);
	boost::multi_index_container<
	galileo::gap_information,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<gap_information, galileo::block_hash, &gap_information::hash>>>>
	blocks;
	size_t const max = 256;
	std::mutex mutex;
	galileo::node & node;
};
class work_pool;
class peer_information
{
public:
	peer_information (galileo::endpoint const &, unsigned);
	peer_information (galileo::endpoint const &, std::chrono::steady_clock::time_point const &, std::chrono::steady_clock::time_point const &);
	galileo::endpoint endpoint;
	boost::asio::ip::address ip_address;
	std::chrono::steady_clock::time_point last_contact;
	std::chrono::steady_clock::time_point last_attempt;
	std::chrono::steady_clock::time_point last_bootstrap_attempt;
	std::chrono::steady_clock::time_point last_rep_request;
	std::chrono::steady_clock::time_point last_rep_response;
	galileo::amount rep_weight;
	galileo::account probable_rep_account;
	unsigned network_version;
	boost::optional<galileo::account> node_id;
};
class peer_attempt
{
public:
	galileo::endpoint endpoint;
	std::chrono::steady_clock::time_point last_attempt;
};
class syn_cookie_info
{
public:
	galileo::uint256_union cookie;
	std::chrono::steady_clock::time_point created_at;
};
class peer_by_ip_addr
{
};
class peer_container
{
public:
	peer_container (galileo::endpoint const &);
	// We were contacted by endpoint, update peers
	// Returns true if a Node ID handshake should begin
	bool contacted (galileo::endpoint const &, unsigned);
	// Unassigned, reserved, self
	bool not_a_peer (galileo::endpoint const &, bool);
	// Returns true if peer was already known
	bool known_peer (galileo::endpoint const &);
	// Notify of peer we received from
	bool insert (galileo::endpoint const &, unsigned);
	std::unordered_set<galileo::endpoint> random_set (size_t);
	void random_fill (std::array<galileo::endpoint, 8> &);
	// Request a list of the top known representatives
	std::vector<peer_information> representatives (size_t);
	// List of all peers
	std::deque<galileo::endpoint> list ();
	std::map<galileo::endpoint, unsigned> list_version ();
	std::vector<peer_information> list_vector ();
	// A list of random peers sized for the configured rebroadcast fanout
	std::deque<galileo::endpoint> list_fanout ();
	// Get the next peer for attempting bootstrap
	galileo::endpoint bootstrap_peer ();
	// Purge any peer where last_contact < time_point and return what was left
	std::vector<galileo::peer_information> purge_list (std::chrono::steady_clock::time_point const &);
	void purge_syn_cookies (std::chrono::steady_clock::time_point const &);
	std::vector<galileo::endpoint> rep_crawl ();
	bool rep_response (galileo::endpoint const &, galileo::account const &, galileo::amount const &);
	void rep_request (galileo::endpoint const &);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (galileo::endpoint const &);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	boost::optional<galileo::uint256_union> assign_syn_cookie (galileo::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate_syn_cookie (galileo::endpoint const &, galileo::account, galileo::signature);
	size_t size ();
	size_t size_sqrt ();
	galileo::uint128_t total_weight ();
	galileo::uint128_t online_weight_minimum;
	bool empty ();
	std::mutex mutex;
	galileo::endpoint self;
	boost::multi_index_container<
	peer_information,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_information, galileo::endpoint, &peer_information::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_contact>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_attempt>, std::greater<std::chrono::steady_clock::time_point>>,
	boost::multi_index::random_access<>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_bootstrap_attempt>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_rep_request>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, galileo::amount, &peer_information::rep_weight>, std::greater<galileo::amount>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<peer_by_ip_addr>, boost::multi_index::member<peer_information, boost::asio::ip::address, &peer_information::ip_address>>>>
	peers;
	boost::multi_index_container<
	peer_attempt,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_attempt, galileo::endpoint, &peer_attempt::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_attempt, std::chrono::steady_clock::time_point, &peer_attempt::last_attempt>>>>
	attempts;
	std::mutex syn_cookie_mutex;
	std::unordered_map<galileo::endpoint, syn_cookie_info> syn_cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> syn_cookies_per_ip;
	// Number of peers that don't support node ID
	size_t legacy_peers;
	// Called when a new peer is observed
	std::function<void(galileo::endpoint const &)> peer_observer;
	std::function<void()> disconnect_observer;
	// Number of peers to crawl for being a rep every period
	static size_t constexpr peers_per_crawl = 8;
	// Maximum number of peers per IP (includes legacy peers)
	static size_t constexpr max_peers_per_ip = 10;
	// Maximum number of legacy peers per IP
	static size_t constexpr max_legacy_peers_per_ip = 5;
	// Maximum number of peers that don't support node ID
	static size_t constexpr max_legacy_peers = 500;
};
class send_info
{
public:
	uint8_t const * data;
	size_t size;
	galileo::endpoint endpoint;
	std::function<void(boost::system::error_code const &, size_t)> callback;
};
class mapping_protocol
{
public:
	char const * name;
	int remaining;
	boost::asio::ip::address_v4 external_address;
	uint16_t external_port;
};
// These APIs aren't easy to understand so comments are verbose
class port_mapping
{
public:
	port_mapping (galileo::node &);
	void start ();
	void stop ();
	void refresh_devices ();
	// Refresh when the lease ends
	void refresh_mapping ();
	// Refresh occasionally in case router loses mapping
	void check_mapping_loop ();
	int check_mapping ();
	bool has_address ();
	std::mutex mutex;
	galileo::node & node;
	UPNPDev * devices; // List of all UPnP devices
	UPNPUrls urls; // Something for UPnP
	IGDdatas data; // Some other UPnP thing
	// Primes so they infrequently happen at the same time
	static int constexpr mapping_timeout = galileo::galileo_network == galileo::galileo_networks::galileo_test_network ? 53 : 3593;
	static int constexpr check_timeout = galileo::galileo_network == galileo::galileo_networks::galileo_test_network ? 17 : 53;
	boost::asio::ip::address_v4 address;
	std::array<mapping_protocol, 2> protocols;
	uint64_t check_count;
	bool on;
};
class block_arrival_info
{
public:
	std::chrono::steady_clock::time_point arrival;
	galileo::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (galileo::block_hash const &);
	bool recent (galileo::block_hash const &);
	boost::multi_index_container<
	galileo::block_arrival_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<galileo::block_arrival_info, std::chrono::steady_clock::time_point, &galileo::block_arrival_info::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<galileo::block_arrival_info, galileo::block_hash, &galileo::block_arrival_info::hash>>>>
	arrival;
	std::mutex mutex;
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};
class rep_last_heard_info
{
public:
	std::chrono::steady_clock::time_point last_heard;
	galileo::account representative;
};
class online_reps
{
public:
	online_reps (galileo::node &);
	void vote (std::shared_ptr<galileo::vote> const &);
	void recalculate_stake ();
	galileo::uint128_t online_stake ();
	galileo::uint128_t online_stake_total;
	std::deque<galileo::account> list ();
	boost::multi_index_container<
	galileo::rep_last_heard_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<galileo::rep_last_heard_info, std::chrono::steady_clock::time_point, &galileo::rep_last_heard_info::last_heard>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<galileo::rep_last_heard_info, galileo::account, &galileo::rep_last_heard_info::representative>>>>
	reps;

private:
	std::mutex mutex;
	galileo::node & node;
};
class network
{
public:
	network (galileo::node &, uint16_t);
	void receive ();
	void stop ();
	void receive_action (boost::system::error_code const &, size_t);
	void rpc_action (boost::system::error_code const &, size_t);
	void republish_vote (std::shared_ptr<galileo::vote>);
	void republish_block (galileo::transaction const &, std::shared_ptr<galileo::block>, bool = true);
	void republish (galileo::block_hash const &, std::shared_ptr<std::vector<uint8_t>>, galileo::endpoint);
	void publish_broadcast (std::vector<galileo::peer_information> &, std::unique_ptr<galileo::block>);
	void confirm_send (galileo::confirm_ack const &, std::shared_ptr<std::vector<uint8_t>>, galileo::endpoint const &);
	void merge_peers (std::array<galileo::endpoint, 8> const &);
	void send_keepalive (galileo::endpoint const &);
	void send_node_id_handshake (galileo::endpoint const &, boost::optional<galileo::uint256_union> const & query, boost::optional<galileo::uint256_union> const & respond_to);
	void broadcast_confirm_req (std::shared_ptr<galileo::block>);
	void broadcast_confirm_req_base (std::shared_ptr<galileo::block>, std::shared_ptr<std::vector<galileo::peer_information>>, unsigned, bool = false);
	void send_confirm_req (galileo::endpoint const &, std::shared_ptr<galileo::block>);
	void send_buffer (uint8_t const *, size_t, galileo::endpoint const &, std::function<void(boost::system::error_code const &, size_t)>);
	galileo::endpoint endpoint ();
	galileo::endpoint remote;
	std::array<uint8_t, 512> buffer;
	boost::asio::ip::udp::socket socket;
	std::mutex socket_mutex;
	boost::asio::ip::udp::resolver resolver;
	galileo::node & node;
	bool on;
	static uint16_t const node_port = galileo::galileo_network == galileo::galileo_networks::galileo_live_network ? 7075 : 54000;
};
class logging
{
public:
	logging ();
	void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	bool ledger_logging () const;
	bool ledger_duplicate_logging () const;
	bool vote_logging () const;
	bool network_logging () const;
	bool network_message_logging () const;
	bool network_publish_logging () const;
	bool network_packet_logging () const;
	bool network_keepalive_logging () const;
	bool network_node_id_handshake_logging () const;
	bool node_lifetime_tracing () const;
	bool insufficient_work_logging () const;
	bool log_rpc () const;
	bool bulk_pull_logging () const;
	bool callback_logging () const;
	bool work_generation_time () const;
	bool log_to_cerr () const;
	void init (boost::filesystem::path const &);

	bool ledger_logging_value;
	bool ledger_duplicate_logging_value;
	bool vote_logging_value;
	bool network_logging_value;
	bool network_message_logging_value;
	bool network_publish_logging_value;
	bool network_packet_logging_value;
	bool network_keepalive_logging_value;
	bool network_node_id_handshake_logging_value;
	bool node_lifetime_tracing_value;
	bool insufficient_work_logging_value;
	bool log_rpc_value;
	bool bulk_pull_logging_value;
	bool work_generation_time_value;
	bool log_to_cerr_value;
	bool flush;
	uintmax_t max_size;
	uintmax_t rotation_size;
	boost::log::sources::logger_mt log;
};
class node_init
{
public:
	node_init ();
	bool error ();
	bool block_store_init;
	bool wallet_init;
};
class node_config
{
public:
	node_config ();
	node_config (uint16_t, galileo::logging const &);
	void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	galileo::account random_representative ();
	uint16_t peering_port;
	galileo::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::string> preconfigured_peers;
	std::vector<galileo::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator;
	galileo::amount receive_minimum;
	galileo::amount online_weight_minimum;
	unsigned online_weight_quorum;
	unsigned password_fanout;
	unsigned io_threads;
	unsigned work_threads;
	bool enable_voting;
	unsigned bootstrap_connections;
	unsigned bootstrap_connections_max;
	std::string callback_address;
	uint16_t callback_port;
	std::string callback_target;
	int lmdb_max_dbs;
	galileo::stat_config stat_config;
	galileo::uint256_union epoch_block_link;
	galileo::account epoch_block_signer;
	std::chrono::system_clock::time_point generate_hash_votes_at;
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
};
class node_observers
{
public:
	galileo::observer_set<std::shared_ptr<galileo::block>, galileo::account const &, galileo::uint128_t const &, bool> blocks;
	galileo::observer_set<bool> wallet;
	galileo::observer_set<galileo::transaction const &, std::shared_ptr<galileo::vote>, galileo::endpoint const &> vote;
	galileo::observer_set<galileo::account const &, bool> account_balance;
	galileo::observer_set<galileo::endpoint const &> endpoint;
	galileo::observer_set<> disconnect;
	galileo::observer_set<> started;
};
class vote_processor
{
public:
	vote_processor (galileo::node &);
	void vote (std::shared_ptr<galileo::vote>, galileo::endpoint);
	galileo::vote_code vote_blocking (galileo::transaction const &, std::shared_ptr<galileo::vote>, galileo::endpoint);
	void flush ();
	galileo::node & node;
	void stop ();

private:
	void process_loop ();
	std::deque<std::pair<std::shared_ptr<galileo::vote>, galileo::endpoint>> votes;
	std::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool active;
	std::thread thread;
};
// The network is crawled for representatives by occasionally sending a unicast confirm_req for a specific block and watching to see if it's acknowledged with a vote.
class rep_crawler
{
public:
	void add (galileo::block_hash const &);
	void remove (galileo::block_hash const &);
	bool exists (galileo::block_hash const &);
	std::mutex mutex;
	std::unordered_set<galileo::block_hash> active;
};
// Processing blocks is a potentially long IO operation
// This class isolates block insertion from other operations like servicing network operations
class block_processor
{
public:
	block_processor (galileo::node &);
	~block_processor ();
	void stop ();
	void flush ();
	bool full ();
	void add (std::shared_ptr<galileo::block>, std::chrono::steady_clock::time_point);
	void force (std::shared_ptr<galileo::block>);
	bool should_log ();
	bool have_blocks ();
	void process_blocks ();
	galileo::process_return process_receive_one (galileo::transaction const &, std::shared_ptr<galileo::block>, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now ());

private:
	void queue_unchecked (galileo::transaction const &, galileo::block_hash const &);
	void process_receive_many (std::unique_lock<std::mutex> &);
	bool stopped;
	bool active;
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::pair<std::shared_ptr<galileo::block>, std::chrono::steady_clock::time_point>> blocks;
	std::unordered_set<galileo::block_hash> blocks_hashes;
	std::deque<std::shared_ptr<galileo::block>> forced;
	std::condition_variable condition;
	galileo::node & node;
	std::mutex mutex;
};
class node : public std::enable_shared_from_this<galileo::node>
{
public:
	node (galileo::node_init &, boost::asio::io_service &, uint16_t, boost::filesystem::path const &, galileo::alarm &, galileo::logging const &, galileo::work_pool &);
	node (galileo::node_init &, boost::asio::io_service &, boost::filesystem::path const &, galileo::alarm &, galileo::node_config const &, galileo::work_pool &);
	~node ();
	template <typename T>
	void background (T action_a)
	{
		alarm.service.post (action_a);
	}
	void send_keepalive (galileo::endpoint const &);
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<galileo::node> shared ();
	int store_version ();
	void process_confirmed (std::shared_ptr<galileo::block>);
	void process_message (galileo::message &, galileo::endpoint const &);
	void process_active (std::shared_ptr<galileo::block>);
	galileo::process_return process (galileo::block const &);
	void keepalive_preconfigured (std::vector<std::string> const &);
	galileo::block_hash latest (galileo::account const &);
	galileo::uint128_t balance (galileo::account const &);
	std::unique_ptr<galileo::block> block (galileo::block_hash const &);
	std::pair<galileo::uint128_t, galileo::uint128_t> balance_pending (galileo::account const &);
	galileo::uint128_t weight (galileo::account const &);
	galileo::account representative (galileo::account const &);
	void ongoing_keepalive ();
	void ongoing_syn_cookie_cleanup ();
	void ongoing_rep_crawl ();
	void ongoing_bootstrap ();
	void ongoing_store_flush ();
	void backup_wallet ();
	int price (galileo::uint128_t const &, int);
	void work_generate_blocking (galileo::block &);
	uint64_t work_generate_blocking (galileo::uint256_union const &);
	void work_generate (galileo::uint256_union const &, std::function<void(uint64_t)>);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<galileo::block>);
	void process_fork (galileo::transaction const &, std::shared_ptr<galileo::block>);
	bool validate_block_by_previous (galileo::transaction const &, std::shared_ptr<galileo::block>);
	galileo::uint128_t delta ();
	boost::asio::io_service & service;
	galileo::node_config config;
	galileo::alarm & alarm;
	galileo::work_pool & work;
	boost::log::sources::logger_mt log;
	std::unique_ptr<galileo::block_store> store_impl;
	galileo::block_store & store;
	galileo::gap_cache gap_cache;
	galileo::ledger ledger;
	galileo::active_transactions active;
	galileo::network network;
	galileo::bootstrap_initiator bootstrap_initiator;
	galileo::bootstrap_listener bootstrap;
	galileo::peer_container peers;
	boost::filesystem::path application_path;
	galileo::node_observers observers;
	galileo::wallets wallets;
	galileo::port_mapping port_mapping;
	galileo::vote_processor vote_processor;
	galileo::rep_crawler rep_crawler;
	unsigned warmed_up;
	galileo::block_processor block_processor;
	std::thread block_processor_thread;
	galileo::block_arrival block_arrival;
	galileo::online_reps online_reps;
	galileo::stat stats;
	galileo::keypair node_id;
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	static std::chrono::seconds constexpr period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr cutoff = period * 5;
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
};
class thread_runner
{
public:
	thread_runner (boost::asio::io_service &, unsigned);
	~thread_runner ();
	void join ();
	std::vector<std::thread> threads;
};
class inactive_node
{
public:
	inactive_node (boost::filesystem::path const & path = galileo::working_path ());
	~inactive_node ();
	boost::filesystem::path path;
	boost::shared_ptr<boost::asio::io_service> service;
	galileo::alarm alarm;
	galileo::logging logging;
	galileo::node_init init;
	galileo::work_pool work;
	std::shared_ptr<galileo::node> node;
};
}
