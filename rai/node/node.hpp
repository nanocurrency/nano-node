#pragma once

#include <rai/node/wallet.hpp>

#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <boost/asio.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/circular_buffer.hpp>

#include <xxhash/xxhash.h>

std::ostream & operator << (std::ostream &, std::chrono::system_clock::time_point const &);
namespace rai
{
using endpoint = boost::asio::ip::udp::endpoint;
bool parse_port (std::string const &, uint16_t &);
bool parse_address_port (std::string const &, boost::asio::ip::address &, uint16_t &);
using tcp_endpoint = boost::asio::ip::tcp::endpoint;
bool parse_endpoint (std::string const &, rai::endpoint &);
bool parse_tcp_endpoint (std::string const &, rai::tcp_endpoint &);
bool reserved_address (rai::endpoint const &);
}

static uint64_t endpoint_hash_raw (rai::endpoint const & endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	rai::uint128_union address;
	address.bytes = endpoint_a.address ().to_v6 ().to_bytes ();
	XXH64_state_t hash;
	XXH64_reset (&hash, 0);
	XXH64_update (&hash, address.bytes.data (), address.bytes.size ());
	auto port (endpoint_a.port ());
	XXH64_update (&hash, &port, sizeof (port));
	auto result (XXH64_digest (&hash));
	return result;
}

namespace std
{
template <size_t size>
struct endpoint_hash
{
};
template <>
struct endpoint_hash <8>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
		return endpoint_hash_raw (endpoint_a);
    }
};
template <>
struct endpoint_hash <4>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
		uint64_t big (endpoint_hash_raw (endpoint_a));
		uint32_t result (static_cast <uint32_t> (big) ^ static_cast <uint32_t> (big >> 32));
		return result;
    }
};
template <>
struct hash <rai::endpoint>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
        endpoint_hash <sizeof (size_t)> ehash;
        return ehash (endpoint_a);
    }
};
}
namespace boost
{
template <>
struct hash <rai::endpoint>
{
    size_t operator () (rai::endpoint const & endpoint_a) const
    {
        std::hash <rai::endpoint> hash;
        return hash (endpoint_a);
    }
};
namespace program_options
{
class options_description;
class variables_map;
}
}

namespace rai
{
class node;
class election : public std::enable_shared_from_this <rai::election>
{
public:
    election (std::shared_ptr <rai::node>, rai::block const &, std::function <void (rai::block &)> const &);
    void vote (rai::vote const &);
	void interval_action ();
    void start_request (rai::block const &);
	void confirm (bool);
    rai::uint128_t uncontested_threshold (MDB_txn *, rai::ledger &);
    rai::uint128_t contested_threshold (MDB_txn *, rai::ledger &);
    rai::votes votes;
    std::weak_ptr <rai::node> node;
    std::chrono::system_clock::time_point last_vote;
	std::unique_ptr <rai::block> last_winner;
    bool confirmed;
	std::function <void (rai::block &)> confirmation_action;
};
class conflict_info
{
public:
	rai::block_hash root;
	std::shared_ptr <rai::election> election;
	// Number of announcements in a row for this fork
	int announcements;
};
class conflicts
{
public:
    conflicts (rai::node &);
    void start (rai::block const &, std::function <void (rai::block &)> const &, bool);
    bool no_conflict (rai::block_hash const &);
    void update (rai::vote const &);
	void announce_votes ();
    boost::multi_index_container
	<
		rai::conflict_info,
		boost::multi_index::indexed_by
		<
			boost::multi_index::ordered_unique <boost::multi_index::member <rai::conflict_info, rai::block_hash, &rai::conflict_info::root>>
		>
	> roots;
    rai::node & node;
    std::mutex mutex;
	static size_t constexpr announcements_per_interval = 32;
	static size_t constexpr contigious_announcements = 4;
};
enum class message_type : uint8_t
{
    invalid,
    not_a_type,
    keepalive,
    publish,
    confirm_req,
    confirm_ack,
    bulk_pull,
    bulk_push,
    frontier_req
};
class message_visitor;
class message
{
public:
    message (rai::message_type);
	message (bool &, rai::stream &);
    virtual ~message () = default;
    void write_header (rai::stream &);
    static bool read_header (rai::stream &, uint8_t &, uint8_t &, uint8_t &, rai::message_type &, std::bitset <16> &);
    virtual void serialize (rai::stream &) = 0;
    virtual bool deserialize (rai::stream &) = 0;
    virtual void visit (rai::message_visitor &) const = 0;
    rai::block_type block_type () const;
    void block_type_set (rai::block_type);
    bool ipv4_only ();
    void ipv4_only_set (bool);
    static std::array <uint8_t, 2> constexpr magic_number = {{'R', rai::rai_network == rai::rai_networks::rai_test_network ? 'A' : rai::rai_network == rai::rai_networks::rai_beta_network ? 'B' : 'C'}};
    uint8_t version_max;
    uint8_t version_using;
    uint8_t version_min;
    rai::message_type type;
    std::bitset <16> extensions;
    static size_t constexpr ipv4_only_position = 1;
    static size_t constexpr bootstrap_server_position = 2;
    static std::bitset <16> constexpr block_type_mask = std::bitset <16> (0x0f00);
};
class keepalive : public message
{
public:
    keepalive ();
    void visit (rai::message_visitor &) const override;
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    bool operator == (rai::keepalive const &) const;
    std::array <rai::endpoint, 8> peers;
};
class publish : public message
{
public:
    publish ();
    publish (std::unique_ptr <rai::block>);
    void visit (rai::message_visitor &) const override;
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    bool operator == (rai::publish const &) const;
    std::unique_ptr <rai::block> block;
};
class confirm_req : public message
{
public:
    confirm_req ();
    confirm_req (std::unique_ptr <rai::block>);
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::confirm_req const &) const;
    std::unique_ptr <rai::block> block;
};
class confirm_ack : public message
{
public:
	confirm_ack (bool &, rai::stream &);
    confirm_ack (rai::account const &, rai::raw_key const &, uint64_t, std::unique_ptr <rai::block>);
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::confirm_ack const &) const;
    rai::vote vote;
};
class frontier_req : public message
{
public:
    frontier_req ();
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::frontier_req const &) const;
    rai::account start;
    uint32_t age;
    uint32_t count;
};
class bulk_pull : public message
{
public:
    bulk_pull ();
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    rai::uint256_union start;
    rai::block_hash end;
    uint32_t count;
};
class bulk_push : public message
{
public:
    bulk_push ();
    bool deserialize (rai::stream &) override;
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
};
class message_visitor
{
public:
    virtual void keepalive (rai::keepalive const &) = 0;
    virtual void publish (rai::publish const &) = 0;
    virtual void confirm_req (rai::confirm_req const &) = 0;
    virtual void confirm_ack (rai::confirm_ack const &) = 0;
    virtual void bulk_pull (rai::bulk_pull const &) = 0;
    virtual void bulk_push (rai::bulk_push const &) = 0;
    virtual void frontier_req (rai::frontier_req const &) = 0;
};
class operation
{
public:
    bool operator > (rai::operation const &) const;
    std::chrono::system_clock::time_point wakeup;
    std::function <void ()> function;
};
class processor_service
{
public:
    processor_service ();
    void run ();
    size_t poll ();
    size_t poll_one ();
    void add (std::chrono::system_clock::time_point const &, std::function <void ()> const &);
    void stop ();
    bool stopped ();
    size_t size ();
    bool done;
    std::mutex mutex;
    std::condition_variable condition;
    std::priority_queue <operation, std::vector <operation>, std::greater <operation>> operations;
};
class gap_information
{
public:
    std::chrono::system_clock::time_point arrival;
    rai::block_hash required;
    rai::block_hash hash;
	std::unique_ptr <rai::votes> votes;
    std::unique_ptr <rai::block> block;
};
class gap_cache
{
public:
    gap_cache (rai::node &);
    void add (rai::block const &, rai::block_hash);
    std::vector <std::unique_ptr <rai::block>> get (rai::block_hash const &);
    void vote (MDB_txn *, rai::vote const &);
    rai::uint128_t bootstrap_threshold (MDB_txn *);
    boost::multi_index_container
    <
        rai::gap_information,
        boost::multi_index::indexed_by
        <
            boost::multi_index::hashed_non_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::required>>,
            boost::multi_index::ordered_non_unique <boost::multi_index::member <gap_information, std::chrono::system_clock::time_point, &gap_information::arrival>>,
            boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::hash>>
        >
    > blocks;
    size_t const max = 16384;
    std::mutex mutex;
    rai::node & node;
};
class block_synchronization
{
public:
    block_synchronization (boost::log::sources::logger &, std::function <void (rai::transaction &, rai::block const &)> const &, rai::block_store &);
    ~block_synchronization ();
    // Return true if target already has block
    virtual bool synchronized (rai::transaction &, rai::block_hash const &) = 0;
    virtual std::unique_ptr <rai::block> retrieve (rai::transaction &, rai::block_hash const &) = 0;
    // return true if all dependencies are synchronized
    bool add_dependency (rai::transaction &, rai::block const &);
    bool fill_dependencies (rai::transaction &);
    bool synchronize_one (rai::transaction &);
    bool synchronize (rai::transaction &, rai::block_hash const &);
    std::stack <rai::block_hash> blocks;
    std::unordered_set <rai::block_hash> sent;
	boost::log::sources::logger & log;
    std::function <void (rai::transaction &, rai::block const &)> target;
    rai::block_store & store;
};
class pull_synchronization : public rai::block_synchronization
{
public:
    pull_synchronization (boost::log::sources::logger &, std::function <void (rai::transaction &, rai::block const &)> const &, rai::block_store &);
    bool synchronized (rai::transaction &, rai::block_hash const &) override;
    std::unique_ptr <rai::block> retrieve (rai::transaction &, rai::block_hash const &) override;
};
class push_synchronization : public rai::block_synchronization
{
public:
    push_synchronization (boost::log::sources::logger &, std::function <void (rai::transaction &, rai::block const &)> const &, rai::block_store &);
    bool synchronized (rai::transaction &, rai::block_hash const &) override;
    std::unique_ptr <rai::block> retrieve (rai::transaction &, rai::block_hash const &) override;
};
class bootstrap_client : public std::enable_shared_from_this <bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr <rai::node>, std::function <void ()> const & = [] () {});
    ~bootstrap_client ();
    void run (rai::tcp_endpoint const &);
    void connect_action ();
    void sent_request (boost::system::error_code const &, size_t);
    std::shared_ptr <rai::node> node;
    boost::asio::ip::tcp::socket socket;
	std::function <void ()> completion_action;
};
class frontier_req_client : public std::enable_shared_from_this <rai::frontier_req_client>
{
public:
    frontier_req_client (std::shared_ptr <rai::bootstrap_client> const &);
    ~frontier_req_client ();
    void receive_frontier ();
    void received_frontier (boost::system::error_code const &, size_t);
    void request_account (rai::account const &);
	void unsynced (MDB_txn *, rai::account const &, rai::block_hash const &);
    void completed_requests ();
    void completed_pulls ();
    void completed_pushes ();
	void next ();
    std::unordered_map <rai::account, rai::block_hash> pulls;
    std::array <uint8_t, 200> receive_buffer;
    std::shared_ptr <rai::bootstrap_client> connection;
	rai::account current;
	rai::account_info info;
};
class bulk_pull_client : public std::enable_shared_from_this <rai::bulk_pull_client>
{
public:
    bulk_pull_client (std::shared_ptr <rai::frontier_req_client> const &);
    ~bulk_pull_client ();
    void request ();
    void receive_block ();
    void received_type ();
    void received_block (boost::system::error_code const &, size_t);
    void process_end ();
	void block_flush ();
	rai::block_hash first ();
    std::array <uint8_t, 200> receive_buffer;
    std::shared_ptr <rai::frontier_req_client> connection;
	size_t const block_count = 4096;
	std::vector <std::unique_ptr <rai::block>> blocks;
    std::unordered_map <rai::account, rai::block_hash>::iterator current;
    std::unordered_map <rai::account, rai::block_hash>::iterator end;
};
class bulk_push_client : public std::enable_shared_from_this <rai::bulk_push_client>
{
public:
    bulk_push_client (std::shared_ptr <rai::frontier_req_client> const &);
    ~bulk_push_client ();
    void start ();
    void push ();
    void push_block (rai::block const &);
    void send_finished ();
    std::shared_ptr <rai::frontier_req_client> connection;
    rai::push_synchronization synchronization;
};
class work_pool;
class message_parser
{
public:
    message_parser (rai::message_visitor &, rai::work_pool &);
    void deserialize_buffer (uint8_t const *, size_t);
    void deserialize_keepalive (uint8_t const *, size_t);
    void deserialize_publish (uint8_t const *, size_t);
    void deserialize_confirm_req (uint8_t const *, size_t);
    void deserialize_confirm_ack (uint8_t const *, size_t);
    bool at_end (rai::bufferstream &);
    rai::message_visitor & visitor;
	rai::work_pool & pool;
    bool error;
    bool insufficient_work;
};
class peer_information
{
public:
	rai::endpoint endpoint;
	std::chrono::system_clock::time_point last_contact;
	std::chrono::system_clock::time_point last_attempt;
	std::chrono::system_clock::time_point last_bootstrap_failure;
	rai::block_hash most_recent;
};
class peer_container
{
public:
	peer_container (rai::endpoint const &);
	// We were contacted by endpoint, update peers
    void contacted (rai::endpoint const &);
	// Unassigned, reserved, self
	bool not_a_peer (rai::endpoint const &);
	// Returns true if peer was already known
	bool known_peer (rai::endpoint const &);
	// Notify of peer we received from
	bool insert (rai::endpoint const &);
	// Received from a peer and contained a block announcement
	bool insert (rai::endpoint const &, rai::block_hash const &);
	// Does this peer probably know about this block
	bool knows_about (rai::endpoint const &, rai::block_hash const &);
	// Notify of bootstrap failure
	void bootstrap_failed (rai::endpoint const &);
	void random_fill (std::array <rai::endpoint, 8> &);
	// List of all peers
	std::vector <peer_information> list ();
	// List of peers that haven't failed bootstrapping in a while
	std::vector <peer_information> bootstrap_candidates ();
	// Purge any peer where last_contact < time_point and return what was left
	std::vector <rai::peer_information> purge_list (std::chrono::system_clock::time_point const &);
	size_t size ();
	bool empty ();
	std::mutex mutex;
	rai::endpoint self;
	boost::multi_index_container
	<
		peer_information,
		boost::multi_index::indexed_by
		<
			boost::multi_index::hashed_unique <boost::multi_index::member <peer_information, rai::endpoint, &peer_information::endpoint>>,
			boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_contact>>,
			boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_attempt>, std::greater <std::chrono::system_clock::time_point>>
		>
	> peers;
	std::function <void (rai::endpoint const &)> peer_observer;
	std::function <void ()> disconnect_observer;
};
class send_info
{
public:
	uint8_t const * data;
	size_t size;
	rai::endpoint endpoint;
	size_t rebroadcast;
	std::function <void (boost::system::error_code const &, size_t)> callback;
};
class network
{
public:
    network (boost::asio::io_service &, uint16_t, rai::node &);
    void receive ();
    void stop ();
    void receive_action (boost::system::error_code const &, size_t);
    void rpc_action (boost::system::error_code const &, size_t);
    void republish_block (std::unique_ptr <rai::block>, size_t);
    void publish_broadcast (std::vector <rai::peer_information> &, std::unique_ptr <rai::block>);
    bool confirm_broadcast (std::vector <rai::peer_information> &, std::unique_ptr <rai::block>, uint64_t, size_t);
	void confirm_block (rai::raw_key const &, rai::public_key const &, std::unique_ptr <rai::block>, uint64_t, rai::endpoint const &, size_t);
    void merge_peers (std::array <rai::endpoint, 8> const &);
    void send_keepalive (rai::endpoint const &);
	void broadcast_confirm_req (rai::block const &);
    void send_confirm_req (rai::endpoint const &, rai::block const &);
	void initiate_send ();
    void send_buffer (uint8_t const *, size_t, rai::endpoint const &, size_t, std::function <void (boost::system::error_code const &, size_t)>);
    void send_complete (boost::system::error_code const &, size_t);
    rai::endpoint endpoint ();
    rai::endpoint remote;
    std::array <uint8_t, 512> buffer;
    boost::asio::ip::udp::socket socket;
    std::mutex socket_mutex;
    boost::asio::io_service & service;
    boost::asio::ip::udp::resolver resolver;
    rai::node & node;
    uint64_t bad_sender_count;
    std::queue <rai::send_info> sends;
    bool on;
    uint64_t keepalive_count;
    uint64_t publish_count;
    uint64_t confirm_req_count;
    uint64_t confirm_ack_count;
    uint64_t insufficient_work_count;
    uint64_t error_count;
    static uint16_t const node_port = rai::rai_network == rai::rai_networks::rai_live_network ? 7075 : 54000;
    static uint16_t const rpc_port = rai::rai_network == rai::rai_networks::rai_live_network ? 7076 : 55000;
};
class bootstrap_initiator
{
public:
	bootstrap_initiator (rai::node &);
	void warmup (rai::endpoint const &);
	void bootstrap (rai::endpoint const &);
    void bootstrap_any ();
	void initiate (rai::endpoint const &);
	void notify_listeners ();
	std::vector <std::function <void (bool)>> observers;
	std::mutex mutex;
	rai::node & node;
	bool in_progress;
	std::unordered_set <rai::endpoint> warmed_up;
};
class bootstrap_listener
{
public:
    bootstrap_listener (boost::asio::io_service &, uint16_t, rai::node &);
    void start ();
    void stop ();
    void accept_connection ();
    void accept_action (boost::system::error_code const &, std::shared_ptr <boost::asio::ip::tcp::socket>);
    rai::tcp_endpoint endpoint ();
    boost::asio::ip::tcp::acceptor acceptor;
    rai::tcp_endpoint local;
    boost::asio::io_service & service;
    rai::node & node;
    bool on;
};
class bootstrap_server : public std::enable_shared_from_this <rai::bootstrap_server>
{
public:
    bootstrap_server (std::shared_ptr <boost::asio::ip::tcp::socket>, std::shared_ptr <rai::node>);
    ~bootstrap_server ();
    void receive ();
    void receive_header_action (boost::system::error_code const &, size_t);
    void receive_bulk_pull_action (boost::system::error_code const &, size_t);
    void receive_frontier_req_action (boost::system::error_code const &, size_t);
    void receive_bulk_push_action ();
    void add_request (std::unique_ptr <rai::message>);
    void finish_request ();
    void run_next ();
    std::array <uint8_t, 128> receive_buffer;
    std::shared_ptr <boost::asio::ip::tcp::socket> socket;
    std::shared_ptr <rai::node> node;
    std::mutex mutex;
    std::queue <std::unique_ptr <rai::message>> requests;
};
class bulk_pull_server : public std::enable_shared_from_this <rai::bulk_pull_server>
{
public:
    bulk_pull_server (std::shared_ptr <rai::bootstrap_server> const &, std::unique_ptr <rai::bulk_pull>);
    void set_current_end ();
    std::unique_ptr <rai::block> get_next ();
    void send_next ();
    void sent_action (boost::system::error_code const &, size_t);
    void send_finished ();
    void no_block_sent (boost::system::error_code const &, size_t);
    std::shared_ptr <rai::bootstrap_server> connection;
    std::unique_ptr <rai::bulk_pull> request;
    std::vector <uint8_t> send_buffer;
    rai::block_hash current;
};
class bulk_push_server : public std::enable_shared_from_this <rai::bulk_push_server>
{
public:
    bulk_push_server (std::shared_ptr <rai::bootstrap_server> const &);
    void receive ();
    void receive_block ();
    void received_type ();
    void received_block (boost::system::error_code const &, size_t);
    void process_end ();
    std::array <uint8_t, 256> receive_buffer;
    std::shared_ptr <rai::bootstrap_server> connection;
};
class frontier_req_server : public std::enable_shared_from_this <rai::frontier_req_server>
{
public:
    frontier_req_server (std::shared_ptr <rai::bootstrap_server> const &, std::unique_ptr <rai::frontier_req>);
    void skip_old ();
    void send_next ();
    void sent_action (boost::system::error_code const &, size_t);
    void send_finished ();
    void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
    std::shared_ptr <rai::bootstrap_server> connection;
	rai::account current;
	rai::account_info info;
    std::unique_ptr <rai::frontier_req> request;
    std::vector <uint8_t> send_buffer;
    size_t count;
};
class logging
{
public:
	logging ();
    void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (boost::property_tree::ptree const &);
    bool ledger_logging () const;
    bool ledger_duplicate_logging () const;
    bool network_logging () const;
    bool network_message_logging () const;
    bool network_publish_logging () const;
    bool network_packet_logging () const;
    bool network_keepalive_logging () const;
    bool node_lifetime_tracing () const;
    bool insufficient_work_logging () const;
    bool log_rpc () const;
    bool bulk_pull_logging () const;
    bool work_generation_time () const;
    bool log_to_cerr () const;
	bool ledger_logging_value;
	bool ledger_duplicate_logging_value;
	bool network_logging_value;
	bool network_message_logging_value;
	bool network_publish_logging_value;
	bool network_packet_logging_value;
	bool network_keepalive_logging_value;
	bool node_lifetime_tracing_value;
	bool insufficient_work_logging_value;
	bool log_rpc_value;
	bool bulk_pull_logging_value;
	bool work_generation_time_value;
	bool log_to_cerr_value;
	uintmax_t max_size;
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
	node_config (uint16_t, rai::logging const &);
    void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (boost::property_tree::ptree const &);
	rai::account random_representative ();
	uint16_t peering_port;
	rai::logging logging;
	std::vector <std::string> preconfigured_peers;
	std::vector <rai::account> preconfigured_representatives;
	unsigned packet_delay_microseconds;
	unsigned bootstrap_fraction_numerator;
	unsigned creation_rebroadcast;
	unsigned rebroadcast_delay;
	rai::amount receive_minimum;
    static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
};
class node : public std::enable_shared_from_this <rai::node>
{
public:
    node (rai::node_init &, boost::shared_ptr <boost::asio::io_service>, uint16_t, boost::filesystem::path const &, rai::processor_service &, rai::logging const &, rai::work_pool &);
    node (rai::node_init &, boost::shared_ptr <boost::asio::io_service>, boost::filesystem::path const &, rai::processor_service &, rai::node_config const &, rai::work_pool &);
    ~node ();
	template <typename T>
	void background (T action_a)
	{
		service.add (std::chrono::system_clock::now (), action_a);
	}
    void send_keepalive (rai::endpoint const &);
	void keepalive (std::string const &, uint16_t);
    void start ();
    void stop ();
    std::shared_ptr <rai::node> shared ();
    bool representative_vote (rai::election &, rai::block const &);
	int store_version ();
    void vote (rai::vote const &);
    void process_confirmed (rai::block const &);
	void process_message (rai::message &, rai::endpoint const &);
    void process_confirmation (rai::block const &, rai::endpoint const &);
    void process_receive_republish (std::unique_ptr <rai::block>, size_t);
    void process_receive_many (rai::transaction &, rai::block const &, std::function <void (rai::process_return, rai::block const &)> = [] (rai::process_return, rai::block const &) {});
    rai::process_return process_receive_one (rai::transaction &, rai::block const &);
	rai::process_return process (rai::block const &);
    void keepalive_preconfigured (std::vector <std::string> const &);
	rai::block_hash latest (rai::account const &);
	rai::uint128_t balance (rai::account const &);
	rai::uint128_t weight (rai::account const &);
	rai::account representative (rai::account const &);
	void call_observers (rai::block const & block_a, rai::account const & account_a);
    void ongoing_keepalive ();
	void backup_wallet ();
	int price (rai::uint128_t const &, int);
	rai::node_config config;
    rai::processor_service & service;
	rai::work_pool & work;
    boost::log::sources::logger log;
    rai::block_store store;
    rai::gap_cache gap_cache;
    rai::ledger ledger;
    rai::conflicts conflicts;
    rai::wallets wallets;
    rai::network network;
	rai::bootstrap_initiator bootstrap_initiator;
    rai::bootstrap_listener bootstrap;
    rai::peer_container peers;
	boost::filesystem::path application_path;
    std::vector <std::function <void (rai::block const &, rai::account const &)>> observers;
	std::vector <std::function <void (rai::account const &, bool)>> wallet_observers;
    std::vector <std::function <void (rai::vote const &)>> vote_observers;
	std::vector <std::function <void (rai::endpoint const &)>> endpoint_observers;
	std::vector <std::function <void ()>> disconnect_observers;
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
    static std::chrono::seconds constexpr period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr cutoff = period * 5;
	static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
};
class thread_runner
{
public:
	thread_runner (boost::asio::io_service &, rai::processor_service &);
	void join ();
	std::vector <std::thread> threads;
};
void add_node_options (boost::program_options::options_description &);
bool handle_node_options (boost::program_options::variables_map &);
class inactive_node
{
public:
	inactive_node ();
	rai::processor_service processor;
	rai::logging logging;
	rai::node_init init;
	rai::work_pool work;
	std::shared_ptr <rai::node> node;
};
extern std::chrono::milliseconds const confirm_wait;
}
