#pragma once

#include <rai/secure.hpp>

#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <boost/asio.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
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
}

namespace rai
{
class node;
class election : public std::enable_shared_from_this <rai::election>
{
public:
    election (std::shared_ptr <rai::node>, rai::block const &);
    void start ();
    void vote (rai::vote const &);
    void announce_vote ();
    void timeout_action ();
    void start_request (rai::block const &);
    rai::uint128_t uncontested_threshold (rai::ledger &);
    rai::uint128_t contested_threshold (rai::ledger &);
    rai::votes votes;
    std::weak_ptr <rai::node> node;
    std::chrono::system_clock::time_point last_vote;
	std::unique_ptr <rai::block> last_winner;
    bool confirmed;
};
class conflicts
{
public:
    conflicts (rai::node &);
    void start (rai::block const &, bool);
    bool no_conflict (rai::block_hash const &);
    void update (rai::vote const &);
    void stop (rai::block_hash const &);
    std::unordered_map <rai::block_hash, std::shared_ptr <rai::election>> roots;
    rai::node & node;
    std::mutex mutex;
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
    bool deserialize (rai::stream &);
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
    bool deserialize (rai::stream &);
    void serialize (rai::stream &) override;
    bool operator == (rai::publish const &) const;
    std::unique_ptr <rai::block> block;
};
class confirm_req : public message
{
public:
    confirm_req ();
    confirm_req (std::unique_ptr <rai::block>);
    bool deserialize (rai::stream &);
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::confirm_req const &) const;
    std::unique_ptr <rai::block> block;
};
class confirm_ack : public message
{
public:
	confirm_ack (bool &, rai::stream &);
    confirm_ack (rai::account const &, rai::private_key const &, uint64_t, std::unique_ptr <rai::block>);
    bool deserialize (rai::stream &);
    void serialize (rai::stream &) override;
    void visit (rai::message_visitor &) const override;
    bool operator == (rai::confirm_ack const &) const;
    rai::vote vote;
};
class frontier_req : public message
{
public:
    frontier_req ();
    bool deserialize (rai::stream &);
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
    bool deserialize (rai::stream &);
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
    bool deserialize (rai::stream &);
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
// The fan spreads a key out over the heap to decrease the likelyhood of it being recovered by memory inspection
class fan
{
public:
    fan (rai::uint256_union const &, size_t);
    rai::uint256_union value ();
    void value_set (rai::uint256_union const &);
    std::vector <std::unique_ptr <rai::uint256_union>> values;
};
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (MDB_val const &);
	wallet_value (rai::uint256_union const &);
	rai::mdb_val val () const;
	rai::private_key key;
	uint64_t work;
};
class wallet_store
{
public:
    wallet_store (bool &, MDB_txn *, std::string const &);
    wallet_store (bool &, MDB_txn *, std::string const &, std::string const &);
    void initialize (MDB_txn *, bool &, std::string const &);
    rai::uint256_union check (MDB_txn *);
    bool rekey (MDB_txn *, std::string const &);
    bool valid_password (MDB_txn *);
    void enter_password (MDB_txn *, std::string const &);
    rai::uint256_union wallet_key (MDB_txn *);
    rai::uint256_union salt (MDB_txn *);
    bool is_representative (MDB_txn *);
    rai::account representative (MDB_txn *);
    void representative_set (MDB_txn *, rai::account const &);
    rai::public_key insert (MDB_txn *, rai::private_key const &);
    void erase (MDB_txn *, rai::public_key const &);
	rai::wallet_value entry_get_raw (MDB_txn *, rai::public_key const &);
	void entry_put_raw (MDB_txn *, rai::public_key const &, rai::wallet_value const &);
    bool fetch (MDB_txn *, rai::public_key const &, rai::private_key &);
    bool exists (MDB_txn *, rai::public_key const &);
	void destroy (MDB_txn *);
    rai::store_iterator find (MDB_txn *, rai::uint256_union const &);
    rai::store_iterator begin (MDB_txn *);
    rai::store_iterator end ();
    rai::uint256_union derive_key (MDB_txn *, std::string const &);
    rai::uint128_t balance (MDB_txn *, rai::ledger &);
    void serialize_json (MDB_txn *, std::string &);
	void write_backup (MDB_txn *, boost::filesystem::path const &);
    bool move (MDB_txn *, rai::wallet_store &, std::vector <rai::public_key> const &);
	bool import (MDB_txn *, rai::wallet_store &);
	bool work_get (MDB_txn *, rai::public_key const &, uint64_t &);
	void work_put (MDB_txn *, rai::public_key const &, uint64_t);
    rai::fan password;
    static rai::uint256_union const version_1;
    static rai::uint256_union const version_current;
    static rai::uint256_union const version_special;
    static rai::uint256_union const wallet_key_special;
    static rai::uint256_union const salt_special;
    static rai::uint256_union const check_special;
    static rai::uint256_union const representative_special;
    static int const special_count;
    static size_t const kdf_full_work = 8 * 1024 * 1024; // 8 * 8 * 1024 * 1024 = 64 MB memory to derive key
    static size_t const kdf_test_work = 1024;
    static size_t const kdf_work = rai::rai_network == rai::rai_networks::rai_test_network ? kdf_test_work : kdf_full_work;
	MDB_env * environment;
    MDB_dbi handle;
};
// A wallet is a set of account keys encrypted by a common encryption key
class wallet : public std::enable_shared_from_this <rai::wallet>
{
public:
    wallet (bool &, MDB_txn *, rai::node &, std::string const &);
    wallet (bool &, MDB_txn *, rai::node &, std::string const &, std::string const &);
	void enter_initial_password (MDB_txn *);
	rai::public_key insert (rai::private_key const &);
    bool exists (rai::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change (rai::account const &, rai::account const &);
    bool receive (rai::send_block const &, rai::private_key const &, rai::account const &);
	// Send from a specific account in the wallet
	bool send (rai::account const &, rai::account const &, rai::uint128_t const &);
	// Send from any of the accounts in the wallet
    bool send_all (rai::account const &, rai::uint128_t const &);
    void work_generate (rai::account const &, rai::block_hash const &);
    void work_update (MDB_txn *, rai::account const &, rai::block_hash const &, uint64_t);
    uint64_t work_fetch (MDB_txn *, rai::account const &, rai::block_hash const &);
    rai::wallet_store store;
    rai::node & node;
};
// The wallets set is all the wallets a node controls.  A node may contain multiple wallets independently encrypted and operated.
class wallets
{
public:
	wallets (bool &, rai::node &);
	std::shared_ptr <rai::wallet> open (rai::uint256_union const &);
	std::shared_ptr <rai::wallet> create (rai::uint256_union const &);
	void destroy (rai::uint256_union const &);
	void cache_work (rai::account const &);
	std::unordered_map <rai::uint256_union, std::shared_ptr <rai::wallet>> items;
	MDB_dbi handle;
	rai::node & node;
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
    std::unique_ptr <rai::block> get (rai::block_hash const &);
    void vote (MDB_txn *, rai::vote const &);
    rai::uint128_t bootstrap_threshold ();
    boost::multi_index_container
    <
        rai::gap_information,
        boost::multi_index::indexed_by
        <
            boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::required>>,
            boost::multi_index::ordered_non_unique <boost::multi_index::member <gap_information, std::chrono::system_clock::time_point, &gap_information::arrival>>,
            boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::hash>>
        >
    > blocks;
    size_t const max = 128;
    std::mutex mutex;
    rai::node & node;
};
class block_synchronization
{
public:
    block_synchronization (std::function <void (rai::block const &)> const &, rai::block_store &);
    ~block_synchronization ();
    // Return true if target already has block
    virtual bool synchronized (rai::block_hash const &) = 0;
    virtual std::unique_ptr <rai::block> retrieve (rai::block_hash const &) = 0;
    // return true if all dependencies are synchronized
    bool add_dependency (rai::block const &);
    bool fill_dependencies ();
    bool synchronize_one ();
    bool synchronize (rai::block_hash const &);
    std::stack <rai::block_hash> blocks;
    std::unordered_set <rai::block_hash> sent;
    std::function <void (rai::block const &)> target;
    rai::block_store & store;
};
class pull_synchronization : public rai::block_synchronization
{
public:
    pull_synchronization (std::function <void (rai::block const &)> const &, rai::block_store &);
    bool synchronized (rai::block_hash const &) override;
    std::unique_ptr <rai::block> retrieve (rai::block_hash const &) override;
};
class push_synchronization : public rai::block_synchronization
{
public:
    push_synchronization (std::function <void (rai::block const &)> const &, rai::block_store &);
    bool synchronized (rai::block_hash const &) override;
    std::unique_ptr <rai::block> retrieve (rai::block_hash const &) override;
};
class bootstrap_client : public std::enable_shared_from_this <bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr <rai::node>, std::function <void ()> const & = [] () {});
    ~bootstrap_client ();
    void run (rai::tcp_endpoint const &);
    void connect_action (boost::system::error_code const &);
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
	rai::block_hash first ();
    std::array <uint8_t, 200> receive_buffer;
    std::shared_ptr <rai::frontier_req_client> connection;
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
class message_parser
{
public:
    message_parser (rai::message_visitor &);
    void deserialize_buffer (uint8_t const *, size_t);
    void deserialize_keepalive (uint8_t const *, size_t);
    void deserialize_publish (uint8_t const *, size_t);
    void deserialize_confirm_req (uint8_t const *, size_t);
    void deserialize_confirm_ack (uint8_t const *, size_t);
    bool at_end (rai::bufferstream &);
    rai::message_visitor & visitor;
    bool error;
    bool insufficient_work;
};
class peer_information
{
public:
	rai::endpoint endpoint;
	std::chrono::system_clock::time_point last_contact;
	std::chrono::system_clock::time_point last_attempt;
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
	void random_fill (std::array <rai::endpoint, 8> &);
	std::vector <peer_information> list ();
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
	void confirm_block (rai::private_key const &, rai::public_key const &, std::unique_ptr <rai::block>, uint64_t, rai::endpoint const &, size_t);
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
class rpc
{
public:
    rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, boost::asio::ip::address_v6 const &, uint16_t, rai::node &, bool);
    void start ();
    void stop ();
    boost::network::http::server <rai::rpc> server;
    void operator () (boost::network::http::server <rai::rpc>::request const &, boost::network::http::server <rai::rpc>::response &);
    void log (const char *) {}
    rai::node & node;
    bool on;
    bool enable_control;
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
};
class node_init
{
public:
    node_init ();
    bool error ();
    bool block_store_init;
    bool wallet_init;
};
class node : public std::enable_shared_from_this <rai::node>
{
public:
    node (rai::node_init &, boost::shared_ptr <boost::asio::io_service>, uint16_t, boost::filesystem::path const &, rai::processor_service &, rai::logging const &);
    ~node ();
	template <typename T>
	void background (T action_a)
	{
		service.add (std::chrono::system_clock::now (), action_a);
	}
    void send_keepalive (rai::endpoint const &);
    void start ();
    void stop ();
    std::shared_ptr <rai::node> shared ();
    bool representative_vote (rai::election &, rai::block const &);
    void vote (rai::vote const &);
    void search_pending ();
    void process_confirmed (rai::block const &);
	void process_message (rai::message &, rai::endpoint const &);
    void process_confirmation (rai::block const &, rai::endpoint const &);
    void process_receive_republish (std::unique_ptr <rai::block>, size_t);
    rai::process_return process_receive (rai::block const &);
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
    rai::processor_service & service;
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
	rai::logging const & logging;
	boost::filesystem::path application_path;
    std::vector <std::function <void (rai::block const &, rai::account const &)>> observers;
    std::vector <std::function <void (rai::vote const &)>> vote_observers;
    std::vector <std::string> preconfigured_peers;
	std::vector <std::function <void (rai::endpoint const &)>> endpoint_observers;
	std::vector <std::function <void ()>> disconnect_observers;
	static double constexpr price_max = 1024.0;
	static double constexpr free_cutoff = 4096.0;
    static std::chrono::seconds constexpr period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr cutoff = period * 5;
	static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
	static unsigned constexpr packet_delay_microseconds = 5000;
	static unsigned constexpr supply_fraction_numerator = 1;
};
class system
{
public:
    system (uint16_t, size_t);
    ~system ();
    void generate_activity (rai::node &);
    void generate_mass_activity (uint32_t, rai::node &);
    void generate_usage_traffic (uint32_t, uint32_t, size_t);
    void generate_usage_traffic (uint32_t, uint32_t);
    rai::uint128_t get_random_amount (MDB_txn *, rai::node &);
    void generate_send_new (rai::node &);
    void generate_send_existing (rai::node &);
    std::shared_ptr <rai::wallet> wallet (size_t);
    rai::account account (MDB_txn *, size_t);
	void poll ();
    boost::shared_ptr <boost::asio::io_service> service;
    rai::processor_service processor;
    std::vector <std::shared_ptr <rai::node>> nodes;
	rai::logging logging;
};
class landing_store
{
public:
	landing_store ();
	landing_store (rai::account const &, rai::account const &, uint64_t, uint64_t);
	landing_store (bool &, std::istream &);
	rai::account source;
	rai::account destination;
	uint64_t start;
	uint64_t last;
	bool deserialize (std::istream &);
	void serialize (std::ostream &) const;
	bool operator == (rai::landing_store const &) const;
};
class landing
{
public:
	landing (rai::node &, std::shared_ptr <rai::wallet>, rai::landing_store &, boost::filesystem::path const &);
	void write_store ();
	rai::uint128_t distribution_amount (uint64_t);
	uint64_t seconds_since_epoch ();
	void distribute_one ();
	void distribute_ongoing ();
	boost::filesystem::path path;
	rai::landing_store & store;
	std::shared_ptr <rai::wallet> wallet;
	rai::node & node;
	static int constexpr interval_exponent = 6;
	static std::chrono::seconds constexpr distribution_interval = std::chrono::seconds (1 << interval_exponent); // 64 seconds
	static std::chrono::seconds constexpr sleep_seconds = std::chrono::seconds (7);
};
extern std::chrono::milliseconds const confirm_wait;
}
