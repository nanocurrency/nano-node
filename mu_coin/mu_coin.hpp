#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <ed25519-donna/ed25519.h>

#include <db_cxx.h>

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>

namespace mu_coin {
    class byte_read_stream
    {
    public:
        byte_read_stream (uint8_t const *, uint8_t const *);
        byte_read_stream (uint8_t const *, size_t const);
        template <typename T>
        bool read (T & value)
        {
            return read (reinterpret_cast <uint8_t *> (&value), sizeof (value));
        }
        bool read (uint8_t *, size_t);
        size_t size ();
        uint8_t const * data;
        uint8_t const * end;
    };
    class byte_write_stream
    {
    public:
        byte_write_stream ();
        ~byte_write_stream ();
        void extend (size_t);
        template <typename T>
        void write (T const & value)
        {
            write (reinterpret_cast <uint8_t const *> (&value), sizeof (value));
        }
        void write (uint8_t const *, size_t);
        void abandon ();
        uint8_t * data;
        size_t size;
    };
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
        mu_coin::uint256_union operator ^ (mu_coin::uint256_union const &) const;
        bool operator == (mu_coin::uint256_union const &) const;
        void encode_hex (std::string &);
        bool decode_hex (std::string const &);
        void encode_dec (std::string &);
        bool decode_dec (std::string const &);
        void serialize (mu_coin::byte_write_stream &) const;
        bool deserialize (mu_coin::byte_read_stream &);
        std::array <uint8_t, 32> bytes;
        std::array <uint64_t, 4> qwords;
        std::array <uint128_union, 2> owords;
        void clear ();
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

namespace mu_coin {
    class block_visitor;
    enum class block_type : uint8_t
    {
        transaction,
        send,
        receive
    };
    class block
    {
    public:
        virtual mu_coin::uint256_union hash () const = 0;
        virtual mu_coin::block_hash previous () const = 0;
        virtual void serialize (mu_coin::byte_write_stream &) const = 0;
        virtual void visit (mu_coin::block_visitor &) const = 0;
        virtual bool operator == (mu_coin::block const &) const = 0;
        virtual std::unique_ptr <mu_coin::block> clone () const = 0;
        virtual mu_coin::block_type type () const = 0;
    };
    class dbt
    {
    public:
        dbt () = default;
        dbt (bool);
        dbt (mu_coin::uint256_union const &);
        dbt (mu_coin::block const &);
        dbt (mu_coin::address const &, mu_coin::block_hash const &);
        dbt (mu_coin::private_key const &, mu_coin::secret_key const &, mu_coin::uint128_union const &);
        void key (mu_coin::uint256_union const &, mu_coin::uint128_union const &, mu_coin::private_key &);
        mu_coin::uint256_union uint256 () const;
        void adopt (mu_coin::byte_write_stream &);
        std::unique_ptr <mu_coin::block> block ();
        Dbt data;
    };
    std::unique_ptr <mu_coin::block> deserialize_block (mu_coin::byte_read_stream &);
    void serialize_block (mu_coin::byte_write_stream &, mu_coin::block const &);
    void sign_message (mu_coin::private_key const &, mu_coin::public_key const &, mu_coin::uint256_union const &, mu_coin::uint512_union &);
    bool validate_message (mu_coin::public_key const &, mu_coin::uint256_union const &, mu_coin::uint512_union const &);
    class send_hashables
    {
    public:
        mu_coin::uint256_union hash () const;
        mu_coin::address destination;
        mu_coin::block_hash previous;
        mu_coin::uint256_union balance;
    };
    class send_block : public mu_coin::block
    {
    public:
        send_block () = default;
        send_block (send_block const &);
        mu_coin::uint256_union hash () const override;
        mu_coin::block_hash previous () const override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &);
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
        mu_coin::uint256_union hash () const;
        mu_coin::block_hash previous;
        mu_coin::block_hash source;
    };
    class receive_block : public mu_coin::block
    {
    public:
        mu_coin::uint256_union hash () const override;
        mu_coin::block_hash previous () const override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &);
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
    class block_visitor
    {
    public:
        virtual void send_block (mu_coin::send_block const &) = 0;
        virtual void receive_block (mu_coin::receive_block const &) = 0;
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
        
        void genesis_put (mu_coin::public_key const &, uint256_union const & = uint256_union (std::numeric_limits <uint256_t>::max () >> 1));
        
        void block_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> block_get (mu_coin::block_hash const &);
        block_iterator blocks_begin ();
        block_iterator blocks_end ();
        
        void latest_put (mu_coin::address const &, mu_coin::block_hash const &);
        bool latest_get (mu_coin::address const &, mu_coin::block_hash &);
        account_iterator latest_begin ();
        account_iterator latest_end ();
        
        void pending_put (mu_coin::identifier const &);
        void pending_del (mu_coin::identifier const &);
        bool pending_get (mu_coin::identifier const &);
        
    private:
        // address -> block_hash                // Each address has one head block
        // block_hash -> block                  // Mapping block hash to contents
        // block_hash ->                        // Pending blocks
        Db addresses;
        Db blocks;
        Db pending;
    };
    enum class process_result
    {
        progress, // Hasn't been seen before, signed correctly
        owned, // Progress and we own the address
        out_of_chain, // Packet does not follow previous, forged or out of order
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
        mu_coin::uint256_t balance (mu_coin::address const &);
        mu_coin::uint256_t supply ();
        mu_coin::process_result process (mu_coin::block const &);
        mu_coin::block_store & store;
    };
    class ledger_processor : public block_visitor
    {
    public:
        ledger_processor (mu_coin::ledger &);
        void send_block (mu_coin::send_block const &);
        void receive_block (mu_coin::receive_block const &);
        mu_coin::ledger & ledger;
        mu_coin::process_result result;
    };
    class keypair
    {
    public:
        keypair ();
        mu_coin::public_key pub;
        mu_coin::private_key prv;
    };
    class cached_password_store
    {
    public:
        ~cached_password_store ();
        void decrypt (mu_coin::uint256_union const &, mu_coin::uint256_union &);
        void encrypt (mu_coin::uint256_union const &, mu_coin::uint256_union const &);
        void clear ();
        mu_coin::uint256_union password;
    };
    enum class message_type : uint8_t
    {
        keepalive_req,
        keepalive_ack,
        publish_req,
        publish_con,
        publish_dup,
        publish_unk,
        publish_nak
    };
    class authorization
    {
    public:
        mu_coin::address address;
        mu_coin::signature signature;
        bool operator == (mu_coin::authorization const &) const;
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
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
    };
    class keepalive_ack : public message
    {
    public:
        void visit (mu_coin::message_visitor &) override;
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
    };
    class publish_req : public message
    {
    public:
        publish_req () = default;
        publish_req (std::unique_ptr <mu_coin::block>);
        void visit (mu_coin::message_visitor &) override;
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
        std::unique_ptr <mu_coin::block> block;
    };
    class publish_con : public message
    {
    public:
        publish_con () = default;
        publish_con (mu_coin::block_hash const &);
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
        void visit (mu_coin::message_visitor &) override;
        bool operator == (mu_coin::publish_con const &) const;
        mu_coin::block_hash block;
        std::vector <mu_coin::authorization> authorizations;
    };
    class publish_dup : public message
    {
    public:
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
        void visit (mu_coin::message_visitor &) override;
        mu_coin::block_hash block;
        std::vector <mu_coin::authorization> authorizations;
    };
    class publish_unk : public message
    {
    public:
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
        void visit (mu_coin::message_visitor &) override;
    };
    class publish_nak : public message
    {
    public:
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
        void visit (mu_coin::message_visitor &) override;
        std::unique_ptr <mu_coin::block> block; // Observed fork block
        std::vector <mu_coin::authorization> authorizations;
    };
    class message_visitor
    {
    public:
        virtual void keepalive_req (mu_coin::keepalive_req const &) = 0;
        virtual void keepalive_ack (mu_coin::keepalive_ack const &) = 0;
        virtual void publish_req (mu_coin::publish_req const &) = 0;
        virtual void publish_con (mu_coin::publish_con const &) = 0;
        virtual void publish_dup (mu_coin::publish_dup const &) = 0;
        virtual void publish_unk (mu_coin::publish_unk const &) = 0;
        virtual void publish_nak (mu_coin::publish_nak const &) = 0;
    };
    struct wallet_temp_t
    {
    };
    extern wallet_temp_t wallet_temp;
    class key_iterator
    {
    public:
        key_iterator (Dbc *);
        key_iterator (mu_coin::key_iterator const &) = default;
        key_iterator & operator ++ ();
        mu_coin::public_key operator * ();
        bool operator == (mu_coin::key_iterator const &) const;
        bool operator != (mu_coin::key_iterator const &) const;
        Dbc * cursor;
        dbt key;
        dbt data;
    };
    class wallet
    {
    public:
        wallet (wallet_temp_t const &);
        wallet (boost::filesystem::path const &);
        void insert (mu_coin::public_key const &, mu_coin::private_key const &, mu_coin::secret_key const &);
        void insert (mu_coin::private_key const &, mu_coin::secret_key const &);
        bool fetch (mu_coin::public_key const &, mu_coin::secret_key const &, mu_coin::private_key &);
        bool generate_send (mu_coin::ledger &, mu_coin::public_key const &, mu_coin::uint256_t const &, mu_coin::uint256_union const &, std::vector <std::unique_ptr <mu_coin::send_block>> &);
        key_iterator begin ();
        key_iterator end ();
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
    class processor
    {
    public:
        processor (mu_coin::processor_service &, mu_coin::client &);
        void publish (std::unique_ptr <mu_coin::block>);
        bool process_publish (std::unique_ptr <mu_coin::publish_req>);
        void process_receivable (std::unique_ptr <mu_coin::publish_req>);
        mu_coin::processor_service & service;
        mu_coin::client & client;
    };
    using session = std::function <void (std::unique_ptr <mu_coin::message>, mu_coin::endpoint const &)>;
    class network
    {
    public:
        network (boost::asio::io_service &, uint16_t, mu_coin::client &);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void send_keepalive (mu_coin::endpoint const &);
        void publish_block (mu_coin::endpoint const &, std::unique_ptr <mu_coin::block>);
        void add_publish_listener (mu_coin::block_hash const &, session const &);
        void remove_publish_listener (mu_coin::block_hash const &);
        size_t publish_listener_size ();
        mu_coin::endpoint remote;
        std::array <uint8_t, 4000> buffer;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        mu_coin::client & client;
        uint64_t keepalive_req_count;
        uint64_t keepalive_ack_count;
        uint64_t publish_req_count;
        uint64_t publish_con_count;
        uint64_t publish_dup_count;
        uint64_t publish_unk_count;
        uint64_t publish_nak_count;
        uint64_t unknown_count;
        bool on;
    private:
        std::mutex mutex;
        std::unordered_map <mu_coin::block_hash, session> publish_listeners;
    };
    class peer_container
    {
    public:
        void add_peer (boost::asio::ip::udp::endpoint const &);
        std::vector <boost::asio::ip::udp::endpoint> list ();
    private:
        std::mutex mutex;
        std::unordered_set <boost::asio::ip::udp::endpoint> peers;
    };    
    class receivable_processor : public std::enable_shared_from_this <receivable_processor>
    {
    public:
        receivable_processor (std::unique_ptr <mu_coin::publish_req> incoming_a, mu_coin::client & client_a);
        void run ();
        void publish_con (std::unique_ptr <mu_coin::message> message, mu_coin::endpoint const & source);
        void timeout_action ();
        void advance_timeout ();
        mu_coin::uint256_t acknowledged;
        mu_coin::uint256_t nacked;
        mu_coin::uint256_t threshold;
        std::chrono::system_clock::time_point timeout;
        std::unique_ptr <mu_coin::publish_req> incoming;
        mu_coin::client & client;
        std::mutex mutex;
        bool complete;
    };
    class client
    {
    public:
        client (boost::asio::io_service &, uint16_t, boost::filesystem::path const &, boost::filesystem::path const &, mu_coin::processor_service &);
        client (boost::asio::io_service &, uint16_t, mu_coin::processor_service &);
        bool send (mu_coin::public_key const &, mu_coin::uint256_t const &, mu_coin::uint256_union const &);
        mu_coin::block_store store;
        mu_coin::ledger ledger;
        mu_coin::wallet wallet;
        mu_coin::network network;
        mu_coin::processor processor;
        mu_coin::peer_container peers;
    };
}