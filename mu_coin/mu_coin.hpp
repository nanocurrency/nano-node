#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <ed25519-donna/ed25519.h>

#include <db_cxx.h>

#include <unordered_map>
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
        void abandon ();
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
        virtual mu_coin::uint256_t fee () const = 0;
        virtual mu_coin::uint256_union hash () const = 0;
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
    class send_input
    {
    public:
        send_input () = default;
        send_input (mu_coin::public_key const &, mu_coin::block_hash const &, mu_coin::balance const &);
        bool operator == (mu_coin::send_input const &) const;
        mu_coin::identifier previous;
        mu_coin::uint256_union coins;
    };
    class send_output
    {
    public:
        send_output () = default;
        send_output (mu_coin::public_key const &, mu_coin::uint256_union const &);
        bool operator == (mu_coin::send_output const &) const;
        mu_coin::address destination;
        mu_coin::uint256_union coins;
    };
    class send_block : public mu_coin::block
    {
    public:
        send_block () = default;
        send_block (send_block const &);
        mu_coin::uint256_t fee () const override;
        mu_coin::uint256_union hash () const override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::send_block const &) const;
        std::vector <mu_coin::send_input> inputs;
        std::vector <mu_coin::send_output> outputs;
        std::vector <mu_coin::uint512_union> signatures;
    };
    class receive_block : public mu_coin::block
    {
    public:
        mu_coin::uint256_t fee () const override;
        mu_coin::uint256_union hash () const override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        void sign (mu_coin::private_key const &, mu_coin::public_key const &, mu_coin::uint256_union const &);
        bool validate (mu_coin::public_key const &, mu_coin::uint256_t const &) const;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::receive_block const &) const;
        uint512_union signature;
        mu_coin::block_hash source;
        mu_coin::identifier previous;
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
    extern block_store_temp_t block_store_temp;
    class block_store
    {
    public:
        block_store (block_store_temp_t const &);
        block_store (boost::filesystem::path const &);
        
        void genesis_put (mu_coin::public_key const &, uint256_union const & = uint256_union (std::numeric_limits <uint256_t>::max () >> 1));
        
        void identifier_put (mu_coin::identifier const &, mu_coin::block_hash const &);
        bool identifier_get (mu_coin::identifier const &, mu_coin::block_hash &);
        
        void block_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> block_get (mu_coin::block_hash const &);
        
        void latest_put (mu_coin::address const &, mu_coin::block_hash const &);
        bool latest_get (mu_coin::address const &, mu_coin::block_hash &);
        
        void pending_put (mu_coin::address const &, mu_coin::block_hash const &);
        void pending_del (mu_coin::address const &, mu_coin::block_hash const &);
        bool pending_get (mu_coin::address const &, mu_coin::block_hash const &);
        
    private:
        // identifier = block_hash ^ address
        // identifier -> block_hash
        // block_hash -> block
        // address -> block_hash
        // (address, block_hash) ->
        Db handle;
    };
    class ledger
    {
    public:
        ledger (mu_coin::block_store &);
        mu_coin::uint256_t balance (mu_coin::address const &);
        bool process (mu_coin::block const &);
        mu_coin::block_store & store;
    };
    class ledger_processor : public block_visitor
    {
    public:
        ledger_processor (mu_coin::ledger &);
        void send_block (mu_coin::send_block const &);
        void receive_block (mu_coin::receive_block const &);
        mu_coin::ledger & ledger;
        bool result;
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
    enum class type : uint16_t
    {
        keepalive_req,
        keepalive_ack,
        publish_req,
        publish_ack,
        publish_nak
    };
    class message
    {
    public:
        virtual ~message ();
    };
    class keepalive_req : public message
    {
    public:
        keepalive_req ();
        std::array <boost::asio::const_buffer, 1> buffers;
        uint16_t type;
    };
    class keepalive_ack : public message
    {
    public:
        keepalive_ack ();
        std::array <boost::asio::const_buffer, 1> buffers;
        uint16_t type;
    };
    class publish_req : public message
    {
    public:
        publish_req ();
        publish_req (std::unique_ptr <mu_coin::block>);
        void build_buffers ();
        bool deserialize (mu_coin::byte_read_stream &);
        void serialize (mu_coin::byte_write_stream &);
        uint16_t type;
        std::unique_ptr <mu_coin::block> block;
    };
    class publish_ack : public message
    {
    public:
        publish_ack ();
        std::array <boost::asio::const_buffer, 1> buffers;
        uint16_t type;
    };
    class publish_nak : public message
    {
    public:
        publish_nak ();
        std::array <boost::asio::const_buffer, 1> buffers;
        uint16_t type;
    };
    class node
    {
    public:
        node (boost::asio::io_service &, uint16_t, mu_coin::ledger &);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void send_keepalive (boost::asio::ip::udp::endpoint const &);
        void publish_block (boost::asio::ip::udp::endpoint const &, std::unique_ptr <mu_coin::block>);
        boost::asio::ip::udp::endpoint remote;
        std::array <uint8_t, 4000> buffer;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        mu_coin::ledger & ledger;
        uint64_t keepalive_req_count;
        uint64_t keepalive_ack_count;
        uint64_t publish_req_count;
        uint64_t publish_ack_count;
        uint64_t publish_nak_count;
        uint64_t unknown_count;
        bool on;
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
        std::unique_ptr <mu_coin::send_block> send (mu_coin::ledger &, mu_coin::public_key const &, mu_coin::uint256_t const &, mu_coin::uint256_union const &);
        key_iterator begin ();
        key_iterator end ();
    private:
        Db handle;
    };
    class processor_service
    {
    public:
        processor_service ();
        void run ();
        void add (std::function <void ()> const &);
        void stop ();
        bool stopped ();
    private:
        bool done;
        std::mutex mutex;
        std::condition_variable condition;
        std::queue <std::function <void ()>> operations;
    };
    class processor
    {
    public:
        processor (mu_coin::processor_service &);
        void process_receivable ();
        mu_coin::processor_service & service;
    };
    class client
    {
    public:
        client (boost::asio::io_service &, uint16_t, boost::filesystem::path const &, boost::filesystem::path const &, mu_coin::processor_service &);
        mu_coin::processor processor;
        mu_coin::block_store store;
        mu_coin::ledger ledger;
        mu_coin::wallet wallet;
        mu_coin::node network;
    };
}