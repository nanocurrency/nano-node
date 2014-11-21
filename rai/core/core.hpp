#pragma once

#include <rai/secure.hpp>

#include <boost/asio.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/circular_buffer.hpp>

#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace CryptoPP
{
    class SHA3;
}

std::ostream & operator << (std::ostream &, std::chrono::system_clock::time_point const &);
namespace rai {
    using endpoint = boost::asio::ip::udp::endpoint;
    using tcp_endpoint = boost::asio::ip::tcp::endpoint;
    bool parse_endpoint (std::string const &, rai::endpoint &);
    bool parse_tcp_endpoint (std::string const &, rai::tcp_endpoint &);
	bool reserved_address (rai::endpoint const &);
}

namespace std
{
    template <size_t size>
    struct endpoint_hash
    {
    };
    template <>
    struct endpoint_hash <4>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            assert (endpoint_a.address ().is_v6 ());
            rai::uint128_union address;
            address.bytes = endpoint_a.address ().to_v6 ().to_bytes ();
            auto result (address.dwords [0] ^ address.dwords [1] ^ address.dwords [2] ^ address.dwords [3] ^ endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct endpoint_hash <8>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            assert (endpoint_a.address ().is_v6 ());
            rai::uint128_union address;
            address.bytes = endpoint_a.address ().to_v6 ().to_bytes ();
            auto result (address.qwords [0] ^ address.qwords [1] ^ endpoint_a.port ());
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

namespace rai {
    class client;
    class destructable
    {
    public:
        destructable (std::function <void ()>);
        ~destructable ();
        std::function <void ()> operation;
    };
	class election : public std::enable_shared_from_this <rai::election>
	{
	public:
        election (std::shared_ptr <rai::client>, rai::block const &);
        void start ();
        void vote (rai::vote const &);
        void announce_vote ();
        void timeout_action (std::shared_ptr <rai::destructable>);
        void start_request (rai::block const &);
		rai::uint256_t uncontested_threshold ();
		rai::uint256_t contested_threshold ();
		rai::votes votes;
        std::shared_ptr <rai::client> client;
		std::chrono::system_clock::time_point last_vote;
		bool confirmed;
	};
    class conflicts
    {
    public:
		conflicts (rai::client &);
        void start (rai::block const &, bool);
		void update (rai::vote const &);
        void stop (rai::block_hash const &);
        std::unordered_map <rai::block_hash, std::shared_ptr <rai::election>> roots;
		rai::client & client;
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
        confirm_unk,
        bulk_pull,
        bulk_push,
		frontier_req
    };
    class message_visitor;
    class message
    {
    public:
        message (rai::message_type);
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
        static std::array <uint8_t, 2> constexpr magic_number = {{'R', 'A'}};
        uint8_t version_max;
        uint8_t version_using;
        uint8_t version_min;
        rai::message_type type;
        std::bitset <16> extensions;
        static size_t constexpr test_network_position = 0;
        static size_t constexpr ipv4_only_position = 1;
        static size_t constexpr bootstrap_receiver_position = 2;
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
        confirm_ack ();
        confirm_ack (std::unique_ptr <rai::block>);
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        bool operator == (rai::confirm_ack const &) const;
        rai::vote vote;
    };
    class confirm_unk : public message
    {
    public:
        confirm_unk ();
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
		rai::uint256_union hash () const;
        rai::account rep_hint;
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
        virtual void confirm_unk (rai::confirm_unk const &) = 0;
        virtual void bulk_pull (rai::bulk_pull const &) = 0;
        virtual void bulk_push (rai::bulk_push const &) = 0;
        virtual void frontier_req (rai::frontier_req const &) = 0;
    };
    class key_entry
    {
    public:
        rai::key_entry * operator -> ();
        rai::public_key first;
        rai::private_key second;
    };
    class key_iterator
    {
    public:
        key_iterator (leveldb::DB *); // Begin iterator
        key_iterator (leveldb::DB *, std::nullptr_t); // End iterator
        key_iterator (leveldb::DB *, rai::uint256_union const &);
        key_iterator (rai::key_iterator &&) = default;
        void set_current ();
        key_iterator & operator ++ ();
        rai::key_entry & operator -> ();
        bool operator == (rai::key_iterator const &) const;
        bool operator != (rai::key_iterator const &) const;
        key_iterator & operator = (rai::key_iterator &&);
        rai::key_entry current;
        std::unique_ptr <leveldb::Iterator> iterator;
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
    class wallet
    {
    public:
        wallet (bool &, boost::filesystem::path const &);
        rai::uint256_union check ();
        bool rekey (std::string const &);
        rai::uint256_union wallet_key ();
        rai::uint256_union salt ();
        void insert (rai::private_key const &);
        bool fetch (rai::public_key const &, rai::private_key &);
        bool generate_send (rai::ledger &, rai::public_key const &, rai::uint128_t const &, std::vector <std::unique_ptr <rai::send_block>> &);
		bool valid_password ();
        key_iterator find (rai::uint256_union const &);
        key_iterator begin ();
        key_iterator end ();
        rai::uint256_union derive_key (std::string const &);
        rai::fan password;
        static rai::uint256_union const version_1;
        static rai::uint256_union const version_current;
        static rai::uint256_union const version_special;
        static rai::uint256_union const wallet_key_special;
        static rai::uint256_union const salt_special;
        static rai::uint256_union const check_special;
        static int const special_count;
    private:
        std::unique_ptr <leveldb::DB> handle;
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
        rai::block_hash hash;
        std::unique_ptr <rai::block> block;
    };
    class gap_cache
    {
    public:
        gap_cache ();
        void add (rai::block const &, rai::block_hash);
        std::unique_ptr <rai::block> get (rai::block_hash const &);
        boost::multi_index_container
        <
            gap_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::hash>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <gap_information, std::chrono::system_clock::time_point, &gap_information::arrival>>
            >
        > blocks;
        size_t const max;
    };
    using session = std::function <void (rai::confirm_ack const &, rai::endpoint const &)>;
    class processor
    {
    public:
        processor (rai::client &);
        void stop ();
        void contacted (rai::endpoint const &);
        void warmup (rai::endpoint const &);
        void find_network (std::vector <std::pair <std::string, std::string>> const &);
        void bootstrap (rai::tcp_endpoint const &);
        void connect_bootstrap (std::vector <std::string> const &);
        rai::process_result process_receive (rai::block const &);
        void process_receive_republish (std::unique_ptr <rai::block>, rai::endpoint const &);
        void republish (std::unique_ptr <rai::block>, rai::endpoint const &);
		void process_message (rai::message &, rai::endpoint const &);
		void process_unknown (rai::vectorstream &);
        void process_confirmation (rai::block const &, rai::endpoint const &);
        void process_confirmed (rai::block const &);
        void ongoing_keepalive ();
        std::unique_ptr <std::set <rai::endpoint>> bootstrapped;
        rai::client & client;
        static size_t constexpr bootstrap_max = 16;
        static std::chrono::seconds constexpr period = std::chrono::seconds (60);
        static std::chrono::seconds constexpr cutoff = period * 5;
        std::mutex mutex;
    };
    class transactions
    {
    public:
        transactions (rai::client &);
        bool receive (rai::send_block const &, rai::private_key const &, rai::account const &);
        bool send (rai::account const &, rai::uint128_t const &);
        void vote (rai::vote const &);
        bool rekey (std::string const &);
        std::mutex mutex;
        rai::client & client;
    };
    class bootstrap_client : public std::enable_shared_from_this <bootstrap_client>
    {
    public:
        bootstrap_client (std::shared_ptr <rai::client>);
        ~bootstrap_client ();
        void run (rai::tcp_endpoint const &);
        void connect_action (boost::system::error_code const &);
        void sent_request (boost::system::error_code const &, size_t);
        std::shared_ptr <rai::client> client;
        boost::asio::ip::tcp::socket socket;
    };
    class frontier_req_client : public std::enable_shared_from_this <rai::frontier_req_client>
    {
    public:
        frontier_req_client (std::shared_ptr <rai::bootstrap_client> const &);
        ~frontier_req_client ();
        void receive_frontier ();
        void received_frontier (boost::system::error_code const &, size_t);
        void request_account (rai::account const &);
        void completed_requests ();
        void completed_pulls ();
        void completed_pushes ();
        std::unordered_map <rai::account, rai::block_hash> pulls;
        std::unordered_map <rai::account, rai::block_hash> pushes;
        std::array <uint8_t, 4000> receive_buffer;
        std::shared_ptr <rai::bootstrap_client> connection;
        rai::account_iterator current;
        rai::account_iterator end;
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
        std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
        std::array <uint8_t, 4000> receive_buffer;
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
        void push_block ();
        void send_finished ();
        std::shared_ptr <rai::frontier_req_client> connection;
        std::unordered_map <rai::block_hash, std::unique_ptr <rai::block>> blocks;
        std::vector <std::unique_ptr <rai::block>> path;
    };
    class work
    {
    public:
        work ();
        uint64_t generate (rai::uint256_union const &, uint64_t);
        uint64_t create (rai::uint256_union const &);
        bool validate (rai::uint256_union const &, uint64_t);
        uint64_t threshold_requirement;
        size_t const entry_requirement;
        uint32_t const iteration_requirement;
        std::vector <uint64_t> entries;
    };
    class network
    {
    public:
        network (boost::asio::io_service &, uint16_t, rai::client &);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void rpc_action (boost::system::error_code const &, size_t);
        void publish_block (rai::endpoint const &, std::unique_ptr <rai::block>);
        void confirm_block (std::unique_ptr <rai::block>, uint64_t);
        void merge_peers (std::array <rai::endpoint, 8> const &);
        void send_keepalive (rai::endpoint const &);
        void send_confirm_req (rai::endpoint const &, rai::block const &);
        void send_buffer (uint8_t const *, size_t, rai::endpoint const &, std::function <void (boost::system::error_code const &, size_t)>);
        void send_complete (boost::system::error_code const &, size_t);
        rai::endpoint endpoint ();
        rai::endpoint remote;
        std::array <uint8_t, 512> buffer;
        rai::work work;
        std::mutex work_mutex;
        boost::asio::ip::udp::socket socket;
        std::mutex socket_mutex;
        boost::asio::io_service & service;
        boost::asio::ip::udp::resolver resolver;
        rai::client & client;
        std::queue <std::tuple <uint8_t const *, size_t, rai::endpoint, std::function <void (boost::system::error_code const &, size_t)>>> sends;
        uint64_t keepalive_count;
        uint64_t publish_req_count;
        uint64_t confirm_req_count;
        uint64_t confirm_ack_count;
        uint64_t confirm_unk_count;
        uint64_t bad_sender_count;
        uint64_t unknown_count;
        uint64_t error_count;
        uint64_t insufficient_work_count;
        bool on;
    };
    class bootstrap_listener
    {
    public:
        bootstrap_listener (boost::asio::io_service &, uint16_t, rai::client &);
        void start ();
        void stop ();
        void accept_connection ();
        void accept_action (boost::system::error_code const &, std::shared_ptr <boost::asio::ip::tcp::socket>);
        rai::tcp_endpoint endpoint ();
        boost::asio::ip::tcp::acceptor acceptor;
        rai::tcp_endpoint local;
        boost::asio::io_service & service;
        rai::client & client;
        bool on;
    };
    class bootstrap_server : public std::enable_shared_from_this <rai::bootstrap_server>
    {
    public:
        bootstrap_server (std::shared_ptr <boost::asio::ip::tcp::socket>, std::shared_ptr <rai::client>);
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
        std::shared_ptr <rai::client> client;
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
        std::pair <rai::uint256_union, rai::uint256_union> get_next ();
        std::shared_ptr <rai::bootstrap_server> connection;
		account_iterator iterator;
        std::unique_ptr <rai::frontier_req> request;
        std::vector <uint8_t> send_buffer;
        size_t count;
    };
    class rpc
    {
    public:
        rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, boost::asio::ip::address_v6 const &, uint16_t, rai::client &, bool);
        void start ();
        void stop ();
        boost::network::http::server <rai::rpc> server;
        void operator () (boost::network::http::server <rai::rpc>::request const &, boost::network::http::server <rai::rpc>::response &);
        void log (const char *) {}
        rai::client & client;
        bool on;
        bool enable_control;
    };
    class peer_information
    {
    public:
        rai::endpoint endpoint;
        std::chrono::system_clock::time_point last_contact;
        std::chrono::system_clock::time_point last_attempt;
    };
    class peer_container
    {
    public:
		peer_container (rai::endpoint const &);
        bool not_a_peer (rai::endpoint const &);
        bool known_peer (rai::endpoint const &);
        // Returns true if peer was already known
		bool insert_peer (rai::endpoint const &);
		void random_fill (std::array <rai::endpoint, 8> &);
        std::vector <peer_information> list ();
        void refresh_action ();
        void queue_next_refresh ();
        std::vector <rai::peer_information> purge_list (std::chrono::system_clock::time_point const &);
        size_t size ();
        bool empty ();
        std::mutex mutex;
		rai::endpoint self;
        boost::multi_index_container
        <peer_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <peer_information, rai::endpoint, &peer_information::endpoint>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_contact>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_attempt>, std::greater <std::chrono::system_clock::time_point>>
            >
        > peers;
    };
    class log
    {
    public:
        log ();
        void add (std::string const &);
        void dump_cerr ();
        boost::circular_buffer <std::pair <std::chrono::system_clock::time_point, std::string>> items;
    };
    class client_init
    {
    public:
        client_init ();
        bool error ();
        leveldb::Status block_store_init;
        bool wallet_init;
        bool ledger_init;
    };
    class client : public std::enable_shared_from_this <rai::client>
    {
    public:
        client (rai::client_init &, boost::shared_ptr <boost::asio::io_service>, uint16_t, boost::filesystem::path const &, rai::processor_service &, rai::account const &);
        client (rai::client_init &, boost::shared_ptr <boost::asio::io_service>, uint16_t, rai::processor_service &, rai::account const &);
        ~client ();
        bool send (rai::public_key const &, rai::uint128_t const &);
        void send_keepalive (rai::endpoint const &);
        rai::uint256_t balance ();
        void start ();
        void stop ();
        std::shared_ptr <rai::client> shared ();
        bool is_representative ();
		void representative_vote (rai::election &, rai::block const &);
        rai::log log;
        rai::account representative;
        rai::block_store store;
        rai::gap_cache gap_cache;
        rai::ledger ledger;
        rai::conflicts conflicts;
        rai::wallet wallet;
        rai::network network;
        rai::bootstrap_listener bootstrap;
        rai::processor processor;
        rai::transactions transactions;
        rai::peer_container peers;
        rai::processor_service & service;
    };
    class system
    {
    public:
        system (uint16_t, size_t);
        ~system ();
        void generate_activity (rai::client &);
        void generate_mass_activity (uint32_t, rai::client &);
        void generate_usage_traffic (uint32_t, uint32_t, size_t);
        void generate_usage_traffic (uint32_t, uint32_t);
        rai::uint128_t get_random_amount (rai::client &);
        void generate_send_new (rai::client &);
        void generate_send_existing (rai::client &);
        boost::shared_ptr <boost::asio::io_service> service;
        rai::processor_service processor;
        std::vector <std::shared_ptr <rai::client>> clients;
    };
}