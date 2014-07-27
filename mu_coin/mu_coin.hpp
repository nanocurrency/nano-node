#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

#include <ed25519-donna/ed25519.h>

#include <db_cxx.h>

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>

namespace CryptoPP
{
    class SHA3;
}

namespace mu_coin {
    using stream = std::basic_streambuf <uint8_t>;
    using bufferstream = boost::iostreams::stream_buffer <boost::iostreams::basic_array_source <uint8_t>>;
    using vectorstream = boost::iostreams::stream_buffer <boost::iostreams::back_insert_device <std::vector <uint8_t>>>;
    template <typename T>
    bool read (mu_coin::stream & stream_a, T & value)
    {
        auto amount_read (stream_a.sgetn (reinterpret_cast <uint8_t *> (&value), sizeof (value)));
        return amount_read != sizeof (value);
    }
    template <typename T>
    void write (mu_coin::stream & stream_a, T const & value)
    {
        auto amount_written (stream_a.sputn (reinterpret_cast <uint8_t const *> (&value), sizeof (value)));
        assert (amount_written == sizeof (value));
    }
    using uint128_t = boost::multiprecision::uint128_t;
    using uint256_t = boost::multiprecision::uint256_t;
    using uint512_t = boost::multiprecision::uint512_t;
    union uint128_union
    {
    public:
        uint128_union () = default;
        uint128_union (mu_coin::uint128_union const &) = default;
        uint128_union (mu_coin::uint128_t const &);
        std::array <uint8_t, 16> bytes;
        std::array <uint64_t, 2> qwords;
    };
    union uint256_union
    {
        uint256_union () = default;
        uint256_union (uint64_t);
        uint256_union (mu_coin::uint256_t const &);
        uint256_union (std::string const &);
        uint256_union (mu_coin::uint256_union const &, mu_coin::uint256_union const &, uint128_union const &);
        uint256_union prv (uint256_union const &, uint128_union const &) const;
        bool operator == (mu_coin::uint256_union const &) const;
        bool operator != (mu_coin::uint256_union const &) const;
        bool operator < (mu_coin::uint256_union const &) const;
        void encode_hex (std::string &) const;
        bool decode_hex (std::string const &);
        void encode_dec (std::string &) const;
        bool decode_dec (std::string const &);
        void encode_base58check (std::string &) const;
        bool decode_base58check (std::string const &);
        void serialize (mu_coin::stream &) const;
        bool deserialize (mu_coin::stream &);
        std::array <uint8_t, 32> bytes;
        std::array <uint64_t, 4> qwords;
        std::array <uint128_union, 2> owords;
        void clear ();
        bool is_zero () const;
        std::string to_string () const;
        mu_coin::uint256_t number () const;
    };
    using block_hash = uint256_union;
    using identifier = uint256_union;
    using address = uint256_union;
    using balance = uint256_union;
    using amount = uint256_union;
    using public_key = uint256_union;
    using private_key = uint256_union;
    using secret_key = uint256_union;
    union uint512_union
    {
        uint512_union () = default;
        uint512_union (mu_coin::uint512_t const &);
        bool operator == (mu_coin::uint512_union const &) const;
        void encode_hex (std::string &);
        bool decode_hex (std::string const &);
        std::array <uint8_t, 64> bytes;
        std::array <uint64_t, 8> qwords;
        std::array <uint256_union, 2> uint256s;
        void clear ();
        boost::multiprecision::uint512_t number ();
    };
    using signature = uint512_union;
    using endpoint = boost::asio::ip::udp::endpoint;
    using tcp_endpoint = boost::asio::ip::tcp::endpoint;
    bool parse_endpoint (std::string const &, mu_coin::endpoint &);
    bool parse_tcp_endpoint (std::string const &, mu_coin::tcp_endpoint &);
}

namespace std
{
    template <>
    struct hash <mu_coin::uint256_union>
    {
        size_t operator () (mu_coin::uint256_union const & data_a) const
        {
            return *reinterpret_cast <size_t const *> (data_a.bytes.data ());
        }
    };
    template <>
    struct hash <mu_coin::uint256_t>
    {
        size_t operator () (mu_coin::uint256_t const & number_a) const
        {
            return number_a.convert_to <size_t> ();
        }
    };
    template <size_t size>
    struct endpoint_hash
    {
    };
    template <>
    struct endpoint_hash <4>
    {
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            auto result (endpoint_a.address ().to_v4 ().to_ulong () ^ endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct endpoint_hash <8>
    {
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            auto result ((endpoint_a.address ().to_v4 ().to_ulong () << 2) | endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct hash <mu_coin::endpoint>
    {
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            endpoint_hash <sizeof (size_t)> ehash;
            return ehash (endpoint_a);
        }
    };
}
namespace boost
{
    template <>
    struct hash <mu_coin::endpoint>
    {
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            std::hash <mu_coin::endpoint> hash;
            return hash (endpoint_a);
        }
    };
}

namespace mu_coin {
    class block_visitor;
    enum class block_type : uint8_t
    {
        invalid,
        not_a_block,
        send,
        receive,
        open,
        change
    };
    class block
    {
    public:
        mu_coin::uint256_union hash () const;
        virtual void hash (CryptoPP::SHA3 &) const = 0;
        virtual mu_coin::block_hash previous () const = 0;
        virtual void serialize (mu_coin::stream &) const = 0;
        virtual void visit (mu_coin::block_visitor &) const = 0;
        virtual bool operator == (mu_coin::block const &) const = 0;
        virtual std::unique_ptr <mu_coin::block> clone () const = 0;
        virtual mu_coin::block_type type () const = 0;
    };
    class dbt
    {
    public:
        dbt () = default;
        dbt (mu_coin::uint256_union const &);
        dbt (mu_coin::block const &);
        dbt (mu_coin::address const &, mu_coin::block_hash const &);
        dbt (mu_coin::private_key const &, mu_coin::secret_key const &, mu_coin::uint128_union const &);
        void adopt ();
        void key (mu_coin::uint256_union const &, mu_coin::uint128_union const &, mu_coin::private_key &);
        mu_coin::uint256_union uint256 () const;
        std::unique_ptr <mu_coin::block> block ();
        std::vector <uint8_t> bytes;
        Dbt data;
    };
    std::unique_ptr <mu_coin::block> deserialize_block (mu_coin::stream &);
    void serialize_block (mu_coin::stream &, mu_coin::block const &);
    void sign_message (mu_coin::private_key const &, mu_coin::public_key const &, mu_coin::uint256_union const &, mu_coin::uint512_union &);
    bool validate_message (mu_coin::public_key const &, mu_coin::uint256_union const &, mu_coin::uint512_union const &);
    class send_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::address destination;
        mu_coin::block_hash previous;
        mu_coin::uint256_union balance;
    };
    class send_block : public mu_coin::block
    {
    public:
        send_block () = default;
        send_block (send_block const &);
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::send_block const &) const;
        send_hashables hashables;
        mu_coin::signature signature;
    };
    class receive_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::block_hash previous;
        mu_coin::block_hash source;
    };
    class receive_block : public mu_coin::block
    {
    public:
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        void sign (mu_coin::private_key const &, mu_coin::public_key const &, mu_coin::uint256_union const &);
        bool validate (mu_coin::public_key const &, mu_coin::uint256_t const &) const;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::receive_block const &) const;
        receive_hashables hashables;
        uint512_union signature;
    };
    class open_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::address representative;
        mu_coin::block_hash source;
    };
    class open_block : public mu_coin::block
    {
    public:
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::open_block const &) const;
        mu_coin::open_hashables hashables;
        mu_coin::uint512_union signature;
    };
    class change_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::address representative;
        mu_coin::block_hash previous;
    };
    class change_block : public mu_coin::block
    {
    public:
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::change_block const &) const;
        mu_coin::change_hashables hashables;
        mu_coin::uint512_union signature;
    };
    class block_visitor
    {
    public:
        virtual void send_block (mu_coin::send_block const &) = 0;
        virtual void receive_block (mu_coin::receive_block const &) = 0;
        virtual void open_block (mu_coin::open_block const &) = 0;
        virtual void change_block (mu_coin::change_block const &) = 0;
    };
    struct block_store_temp_t
    {
    };
    class account_entry
    {
    public:
        account_entry * operator -> ();
        mu_coin::address first;
        mu_coin::block_hash second;
    };
    class account_iterator
    {
    public:
        account_iterator (Dbc *);
        account_iterator (Dbc *, mu_coin::address const &);
        account_iterator (mu_coin::account_iterator &&) = default;
        account_iterator (mu_coin::account_iterator const &) = default;
        account_iterator & operator ++ ();
        account_entry & operator -> ();
        bool operator == (mu_coin::account_iterator const &) const;
        bool operator != (mu_coin::account_iterator const &) const;
        Dbc * cursor;
        dbt key;
        dbt data;
        mu_coin::account_entry current;
    };
    class block_entry
    {
    public:
        block_entry * operator -> ();
        mu_coin::block_hash first;
        std::unique_ptr <mu_coin::block> second;
    };
    class block_iterator
    {
    public:
        block_iterator (Dbc *);
        block_iterator (mu_coin::block_iterator &&) = default;
        block_iterator (mu_coin::block_iterator const &) = default;
        block_iterator & operator ++ ();
        block_entry & operator -> ();
        bool operator == (mu_coin::block_iterator const &) const;
        bool operator != (mu_coin::block_iterator const &) const;
        Dbc * cursor;
        dbt key;
        dbt data;
        mu_coin::block_entry current;
    };
    extern block_store_temp_t block_store_temp;
    class block_store
    {
    public:
        block_store (block_store_temp_t const &);
        block_store (boost::filesystem::path const &);
        
        void block_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> block_get (mu_coin::block_hash const &);
		void block_del (mu_coin::block_hash const &);
        bool block_exists (mu_coin::block_hash const &);
        block_iterator blocks_begin ();
        block_iterator blocks_end ();
        
        void latest_put (mu_coin::address const &, mu_coin::block_hash const &);
        bool latest_get (mu_coin::address const &, mu_coin::block_hash &);
		void latest_del (mu_coin::address const &);
        account_iterator latest_begin (mu_coin::address const &);
        account_iterator latest_begin ();
        account_iterator latest_end ();
        
        void pending_put (mu_coin::block_hash const &);
        void pending_del (mu_coin::identifier const &);
        bool pending_get (mu_coin::identifier const &);
        
        mu_coin::uint256_t representation_get (mu_coin::address const &);
        void representation_put (mu_coin::address const &, mu_coin::uint256_t const &);
        
        void fork_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> fork_get (mu_coin::block_hash const &);
        
        void bootstrap_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> bootstrap_get (mu_coin::block_hash const &);
        void bootstrap_del (mu_coin::block_hash const &);
        
        void successor_put (mu_coin::block_hash const &, mu_coin::block_hash const &);
        bool successor_get (mu_coin::block_hash const &, mu_coin::block_hash &);
        void successor_del (mu_coin::block_hash const &);
        
    private:
        // address -> block_hash                // Each address has one head block
        Db addresses;
        // block_hash -> block                  // Mapping block hash to contents
        Db blocks;
        // block_hash ->                        // Pending blocks
        Db pending;
        // address -> weight                    // Representation
        Db representation;
        // block_hash -> block                  // Fork proof
        Db forks;
        // block_hash -> block                  // Unchecked bootstrap blocks
        Db bootstrap;
        // block_hash -> block_hash             // Tracking successors for bootstrapping
        Db successors;
    };
    enum class process_result
    {
        progress, // Hasn't been seen before, signed correctly
        owned, // Progress and we own the address
        bad_signature, // One or more signatures was bad, forged or transmission error
        old, // Already seen and was valid
        overspend, // Malicious attempt to overspend
        overreceive, // Malicious attempt to receive twice
        fork, // Malicious fork of existing block
        gap, // Block marked as previous isn't in store
        not_receive_from_send // Receive does not have a send source
    };
    class ledger
    {
    public:
        ledger (mu_coin::block_store &);
		mu_coin::address account (mu_coin::block_hash const &);
		mu_coin::uint256_t amount (mu_coin::block_hash const &);
        mu_coin::uint256_t balance (mu_coin::block_hash const &);
		mu_coin::uint256_t account_balance (mu_coin::address const &);
        mu_coin::uint256_t weight (mu_coin::address const &);
		mu_coin::block_hash latest (mu_coin::address const &);
        mu_coin::address representative (mu_coin::block_hash const &);
        mu_coin::uint256_t supply ();
        mu_coin::process_result process (mu_coin::block const &);
        void rollback (mu_coin::block_hash const &);
		void move_representation (mu_coin::address const &, mu_coin::address const &, mu_coin::uint256_t const &);
        mu_coin::block_store & store;
    };
    class keypair
    {
    public:
        keypair ();
        keypair (std::string const &);
        mu_coin::public_key pub;
        mu_coin::private_key prv;
    };
    enum class message_type : uint8_t
    {
        invalid,
        not_a_type,
        keepalive_req,
        keepalive_ack,
        publish_req,
        publish_ack,
        publish_err,
        publish_nak,
        confirm_req,
        confirm_ack,
        confirm_nak,
        confirm_unk,
        bulk_req,
        bulk_fin
    };
    class message_visitor;
    class message
    {
    public:
        virtual ~message () = default;
        virtual void visit (mu_coin::message_visitor &) = 0;
    };
    class keepalive_req : public message
    {
    public:
        void visit (mu_coin::message_visitor &) override;
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
    };
    class keepalive_ack : public message
    {
    public:
        void visit (mu_coin::message_visitor &) override;
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
    };
    class publish_req : public message
    {
    public:
        publish_req () = default;
        publish_req (std::unique_ptr <mu_coin::block>);
        void visit (mu_coin::message_visitor &) override;
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        std::unique_ptr <mu_coin::block> block;
    };
    class publish_ack : public message
    {
    public:
        publish_ack () = default;
        publish_ack (mu_coin::block_hash const &);
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
        bool operator == (mu_coin::publish_ack const &) const;
        mu_coin::block_hash block;
    };
    class publish_err : public message
    {
    public:
        publish_err () = default;
        publish_err (mu_coin::block_hash const &);
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
        mu_coin::block_hash block;
    };
    class publish_nak : public message
    {
    public:
        publish_nak () = default;
        publish_nak (mu_coin::block_hash const &);
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
        mu_coin::block_hash block;
        std::unique_ptr <mu_coin::block> conflict;
    };
    class confirm_req : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
        bool operator == (mu_coin::publish_ack const &) const;
		mu_coin::uint256_union session;
        std::unique_ptr <mu_coin::block> block;
    };
    class confirm_ack : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
        bool operator == (mu_coin::confirm_ack const &) const;
		mu_coin::uint256_union hash () const;
        mu_coin::uint256_union session;
        mu_coin::address address;
        mu_coin::signature signature;
    };
    class confirm_nak : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
		mu_coin::uint256_union hash () const;
        mu_coin::uint256_union session;
        mu_coin::address address;
        mu_coin::signature signature;
        std::unique_ptr <mu_coin::block> block;
    };
    class confirm_unk : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
		mu_coin::uint256_union hash () const;
        mu_coin::address rep_hint;
        mu_coin::uint256_union session;
    };
    class bulk_req : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &);
        void visit (mu_coin::message_visitor &) override;
        mu_coin::address start;
        mu_coin::block_hash end;
        
    };
    class bulk_fin : public message
    {
    public:
    };
    class message_visitor
    {
    public:
        virtual void keepalive_req (mu_coin::keepalive_req const &) = 0;
        virtual void keepalive_ack (mu_coin::keepalive_ack const &) = 0;
        virtual void publish_req (mu_coin::publish_req const &) = 0;
        virtual void publish_ack (mu_coin::publish_ack const &) = 0;
        virtual void publish_err (mu_coin::publish_err const &) = 0;
        virtual void publish_nak (mu_coin::publish_nak const &) = 0;
        virtual void confirm_req (mu_coin::confirm_req const &) = 0;
        virtual void confirm_ack (mu_coin::confirm_ack const &) = 0;
        virtual void confirm_nak (mu_coin::confirm_nak const &) = 0;
        virtual void confirm_unk (mu_coin::confirm_unk const &) = 0;
        virtual void bulk_req (mu_coin::bulk_req const &) = 0;
    };
    struct wallet_temp_t
    {
    };
    extern wallet_temp_t wallet_temp;
    class key_entry
    {
    public:
        mu_coin::key_entry * operator -> ();
        mu_coin::public_key first;
        mu_coin::private_key second;
    };
    class key_iterator
    {
    public:
        key_iterator (Dbc *);
        key_iterator (mu_coin::key_iterator const &) = default;
        void clear ();
        key_iterator & operator ++ ();
        mu_coin::key_entry & operator -> ();
        bool operator == (mu_coin::key_iterator const &) const;
        bool operator != (mu_coin::key_iterator const &) const;
        mu_coin::key_entry current;
        Dbc * cursor;
        dbt key;
        dbt data;
    };
    class wallet
    {
    public:
        wallet (mu_coin::uint256_union const &, wallet_temp_t const &);
        wallet (mu_coin::uint256_union const &, boost::filesystem::path const &);
        void insert (mu_coin::public_key const &, mu_coin::private_key const &, mu_coin::secret_key const &);
        void insert (mu_coin::private_key const &, mu_coin::secret_key const &);
        bool fetch (mu_coin::public_key const &, mu_coin::secret_key const &, mu_coin::private_key &);
        bool generate_send (mu_coin::ledger &, mu_coin::public_key const &, mu_coin::uint256_t const &, mu_coin::uint256_union const &, std::vector <std::unique_ptr <mu_coin::send_block>> &);
        key_iterator find (mu_coin::uint256_union const &);
        key_iterator begin ();
        key_iterator end ();
        mu_coin::uint256_union password;
    private:
        Db handle;
    };
    class operation
    {
    public:
        bool operator < (mu_coin::operation const &) const;
        std::chrono::system_clock::time_point wakeup;
        std::function <void ()> function;
    };
    class processor_service
    {
    public:
        processor_service ();
        void run ();
        void add (std::chrono::system_clock::time_point const &, std::function <void ()> const &);
        void stop ();
        bool stopped ();
        size_t size ();
    private:
        bool done;
        std::mutex mutex;
        std::condition_variable condition;
        std::priority_queue <operation> operations;
    };
    class client;
    using session = std::function <void (std::unique_ptr <mu_coin::message>, mu_coin::endpoint const &)>;
    class processor
    {
    public:
        processor (mu_coin::client &);
        void bootstrap (mu_coin::tcp_endpoint const &);
        void publish (std::unique_ptr <mu_coin::block>, mu_coin::endpoint const &);
        mu_coin::process_result process_publish (std::unique_ptr <mu_coin::publish_req>, mu_coin::endpoint const &);
        void process_receivable (std::unique_ptr <mu_coin::publish_req>, mu_coin::endpoint const &);
		void process_unknown (mu_coin::uint256_union const &, mu_coin::vectorstream &);
        void process_confirmation (mu_coin::uint256_union const &, mu_coin::block_hash const &, mu_coin::endpoint const &);
        void add_confirm_listener (mu_coin::block_hash const &, session const &);
        void remove_confirm_listener (mu_coin::block_hash const &);
        size_t publish_listener_size ();
		void confirm_ack (std::unique_ptr <mu_coin::confirm_ack>, mu_coin::endpoint const &);
		void confirm_nak (std::unique_ptr <mu_coin::confirm_nak>, mu_coin::endpoint const &);
        mu_coin::client & client;
    private:
        std::mutex mutex;
        std::unordered_map <mu_coin::uint256_union, session> confirm_listeners;
    };
    class bootstrap_iterator
    {
    public:
        bootstrap_iterator (mu_coin::block_store &);
        bootstrap_iterator & operator ++ ();
        void observed_block (mu_coin::block const &);
        mu_coin::block_store & store;
        std::pair <mu_coin::address, mu_coin::block_hash> current;
        std::set <mu_coin::address> observed;
    };
    class bootstrap_processor : public std::enable_shared_from_this <bootstrap_processor>
    {
    public:
        bootstrap_processor (mu_coin::client &);
        ~bootstrap_processor ();
        void run (mu_coin::tcp_endpoint const &);
        void connect_action (boost::system::error_code const &);
        void fill_queue ();
        void send_request (std::pair <mu_coin::address, mu_coin::block_hash> const &);
        void send_action (boost::system::error_code const &, size_t);
        void receive_block ();
        void received_type (boost::system::error_code const &, size_t);
        void received_block (boost::system::error_code const &, size_t);
        bool process_block (mu_coin::block const &);
        bool process_end ();
        void stop_blocks ();
        mu_coin::bootstrap_iterator iterator;
        std::queue <std::pair <mu_coin::address, mu_coin::block_hash>> requests;
        mu_coin::block_hash expecting;
        std::array <uint8_t, 4000> buffer;
        mu_coin::client & client;
        boost::asio::ip::tcp::socket socket;
        static size_t const max_queue_size = 10;
    };
    class network
    {
    public:
        network (boost::asio::io_service &, uint16_t, mu_coin::client &);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void rpc_action (boost::system::error_code const &, size_t);
        void send_keepalive (mu_coin::endpoint const &);
        void publish_block (mu_coin::endpoint const &, std::unique_ptr <mu_coin::block>);
        void confirm_block (mu_coin::endpoint const &, mu_coin::uint256_union const & session_a, std::unique_ptr <mu_coin::block>);
        mu_coin::endpoint endpoint ();
        mu_coin::endpoint remote;
        std::array <uint8_t, 4000> buffer;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        mu_coin::client & client;
        uint64_t keepalive_req_count;
        uint64_t keepalive_ack_count;
        uint64_t publish_req_count;
        uint64_t publish_ack_count;
        uint64_t publish_err_count;
        uint64_t publish_nak_count;
        uint64_t confirm_req_count;
        uint64_t confirm_ack_count;
        uint64_t confirm_nak_count;
        uint64_t confirm_unk_count;
        uint64_t unknown_count;
        uint64_t error_count;
        bool on;
    };
    class bootstrap
    {
    public:
        bootstrap (boost::asio::io_service &, uint16_t, mu_coin::client &);
        void accept ();
        void stop ();
        void accept_action (boost::system::error_code const &, std::shared_ptr <boost::asio::ip::tcp::socket>);
        mu_coin::tcp_endpoint endpoint ();
        boost::asio::ip::tcp::acceptor acceptor;
        mu_coin::tcp_endpoint local;
        boost::asio::io_service & service;
        mu_coin::client & client;
        bool on;
    };
    class bootstrap_connection : public std::enable_shared_from_this <bootstrap_connection>
    {
    public:
        bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket>, mu_coin::client &);
        ~bootstrap_connection ();
        void receive ();
        void receive_type_action (boost::system::error_code const &, size_t);
        void receive_req_action (boost::system::error_code const &, size_t);
        std::pair <mu_coin::block_hash, mu_coin::block_hash> process_bulk_req (mu_coin::bulk_req const &);
        std::unique_ptr <mu_coin::block> get_next ();
        void send_next ();
        void sent_action (boost::system::error_code const &, size_t);
        void send_finished ();
        void no_block_sent (boost::system::error_code const &, size_t);
        std::array <uint8_t, 128> receive_buffer;
        std::vector <uint8_t> send_buffer;
        std::shared_ptr <boost::asio::ip::tcp::socket> socket;
        mu_coin::client & client;
        std::queue <std::pair <mu_coin::block_hash, mu_coin::block_hash>> requests;
    };
    class rpc
    {
    public:
        rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, mu_coin::client &);
        void listen ();
        boost::network::http::server <mu_coin::rpc> server;
        void operator () (boost::network::http::server <mu_coin::rpc>::request const &, boost::network::http::server <mu_coin::rpc>::response &);
        void log (const char *) {}
        mu_coin::client & client;
        bool on;
    };
    class peer_information
    {
    public:
        mu_coin::endpoint endpoint;
        std::chrono::system_clock::time_point last_contact;
        std::chrono::system_clock::time_point last_attempt;
    };
    class peer_container
    {
    public:
        peer_container (mu_coin::client &);
        void start ();
        void incoming_from_peer (mu_coin::endpoint const &);
        std::vector <peer_information> list ();
        void refresh_action ();
        void queue_next_refresh ();
        mu_coin::client & client;
        std::mutex mutex;
        boost::multi_index_container
        <peer_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <peer_information, mu_coin::endpoint, &peer_information::endpoint>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_contact>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_attempt>, std::greater <std::chrono::system_clock::time_point>>
            >
        > peers;
        std::chrono::system_clock::duration const period;
        std::chrono::system_clock::duration const cutoff;
    };
    class peer_refresh
    {
    public:
        peer_refresh (mu_coin::peer_container & container_a);
        void refresh_action ();
        void prune_disconnected ();
        void send_keepalives ();
        mu_coin::peer_container & container;
        std::chrono::system_clock::time_point const now;
    };   
    class receivable_processor : public std::enable_shared_from_this <receivable_processor>
    {
    public:
        receivable_processor (std::unique_ptr <mu_coin::publish_req> incoming_a, mu_coin::endpoint const &, mu_coin::client & client_a);
        void run ();
        void process_acknowledged (mu_coin::uint256_t const &);
        void confirm_ack (std::unique_ptr <mu_coin::message> message, mu_coin::endpoint const & source);
        void timeout_action ();
        void advance_timeout ();
        mu_coin::uint256_t acknowledged;
        mu_coin::uint256_t nacked;
        mu_coin::uint256_t threshold;
		mu_coin::uint256_union session;
        std::chrono::system_clock::time_point timeout;
        std::unique_ptr <mu_coin::publish_req> incoming;
        mu_coin::endpoint sender;
        mu_coin::client & client;
        std::mutex mutex;
        bool complete;
    };
    class genesis
    {
    public:
        explicit genesis (mu_coin::address const &, mu_coin::uint256_t const & = std::numeric_limits <uint256_t>::max ());
        void initialize (mu_coin::block_store &) const;
        mu_coin::block_hash hash () const;
        mu_coin::send_block send1;
        mu_coin::send_block send2;
        mu_coin::open_block open;
    };
    class client
    {
    public:
        client (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, uint16_t, boost::filesystem::path const &, boost::filesystem::path const &, mu_coin::processor_service &, mu_coin::address const &, mu_coin::genesis const &);
        client (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, uint16_t, mu_coin::processor_service &, mu_coin::address const &, mu_coin::genesis const &);
        bool send (mu_coin::public_key const &, mu_coin::uint256_t const &, mu_coin::secret_key const &);
        void start ();
        mu_coin::genesis const & genesis;
        mu_coin::address representative;
        mu_coin::block_store store;
        mu_coin::ledger ledger;
        mu_coin::wallet wallet;
        mu_coin::network network;
        mu_coin::bootstrap bootstrap;
        mu_coin::rpc rpc;
        mu_coin::processor processor;
        mu_coin::peer_container peers;
        mu_coin::processor_service & service;
    };
    class system
    {
    public:
        system (size_t, uint16_t, uint16_t, size_t, mu_coin::uint256_t const &);
        void generate_transaction (uint32_t);
        mu_coin::keypair test_genesis_address;
        mu_coin::genesis genesis;
        boost::shared_ptr <boost::asio::io_service> service;
        boost::shared_ptr <boost::network::utils::thread_pool> pool;
        mu_coin::processor_service processor;
        std::vector <std::unique_ptr <mu_coin::client>> clients;
    };
}