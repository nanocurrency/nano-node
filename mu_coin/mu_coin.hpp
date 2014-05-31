#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <cryptopp/eccrypto.h>
#include <cryptopp/eccrypto.h>
#include <cryptopp/osrng.h>
#include <cryptopp/oids.h>

#include <db_cxx.h>

#include <unordered_map>
#include <memory>

namespace mu_coin {
    class byte_read_stream
    {
    public:
        byte_read_stream (uint8_t *, uint8_t *);
        byte_read_stream (uint8_t *, size_t);
        template <typename T>
        bool read (T & value)
        {
            return read (reinterpret_cast <uint8_t *> (&value), sizeof (value));
        }
        void abandon ();
        bool read (uint8_t *, size_t);
        size_t size ();
        uint8_t * data;
        uint8_t * end;
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
    using EC = CryptoPP::ECDSA <CryptoPP::ECP, CryptoPP::SHA256>;
    CryptoPP::OID & oid ();
    CryptoPP::RandomNumberGenerator & pool ();
    CryptoPP::ECP const & curve ();
    union uint128_union
    {
    public:
        uint128_union () = default;
        uint128_union (mu_coin::uint128_union const &) = default;
        uint128_union (mu_coin::uint128_t const &);
        union
        {
            std::array <uint8_t, 16> bytes;
            std::array <uint64_t, 2> qwords;
        };
    };
    union uint256_union
    {
        uint256_union () = default;
        uint256_union (mu_coin::uint256_t const &);
        uint256_union (std::string const &);
        uint256_union (EC::PrivateKey const &);
        uint256_union (EC::PrivateKey const &, uint256_union const &, uint128_union const &);
        EC::PrivateKey key (uint256_union const &, uint128_union const &);
        EC::PrivateKey key ();
        bool operator == (mu_coin::uint256_union const &) const;
        void encode_hex (std::string &);
        bool decode_hex (std::string const &);
        void encode_dec (std::string &);
        bool decode_dec (std::string const &);
        std::array <uint8_t, 32> bytes;
        std::array <uint64_t, 4> qwords;
        void clear ();
        boost::multiprecision::uint256_t number () const;
    };
    struct point_encoding
    {
        point_encoding () = default;
        point_encoding (EC::PublicKey const &);
        point_encoding (uint8_t, uint256_union const &);
        bool validate ();
        void assign (uint8_t, uint256_union const &);
        void encode_hex (std::string &);
        bool decode_hex (std::string const &);
        std::array <uint8_t, 33> bytes;
        uint128_union iv () const;
        EC::PublicKey key () const;
        uint8_t type () const;
        uint256_union point () const;
    };
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
    class address
    {
    public:
        address () = default;
        address (EC::PublicKey const &);
        address (point_encoding const &);
        bool operator == (mu_coin::address const &) const;
        void serialize (mu_coin::byte_write_stream &) const;
        bool deserialize (mu_coin::byte_read_stream &);
        point_encoding point;
    };
}

namespace std
{
    template <>
    struct hash <mu_coin::address>
    {
        size_t operator () (mu_coin::address const & address_a) const
        {
            size_t hash (*reinterpret_cast <size_t const *> (address_a.point.bytes.data ()));
            return hash;
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
        virtual mu_coin::uint256_t hash () const = 0;
        virtual bool balance (mu_coin::address const &, mu_coin::uint256_t &, uint16_t &) = 0;
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
        dbt (mu_coin::address const &);
        dbt (mu_coin::block const &);
        dbt (mu_coin::EC::PublicKey const &);
        dbt (mu_coin::EC::PrivateKey const &, mu_coin::uint256_union const &, mu_coin::uint128_union const &);
        void key (mu_coin::uint256_union const &, mu_coin::uint128_union const &, mu_coin::EC::PrivateKey &, bool &);
        mu_coin::EC::PublicKey key ();
        void adopt (mu_coin::byte_write_stream &);
        std::unique_ptr <mu_coin::block> block ();
        Dbt data;
    };
    std::unique_ptr <mu_coin::block> deserialize_block (mu_coin::byte_read_stream &);
    void serialize_block (mu_coin::byte_write_stream &, mu_coin::block const &);
    class send_input
    {
    public:
        send_input () = default;
        send_input (mu_coin::uint256_union const &, mu_coin::uint256_union const &);
        bool operator == (mu_coin::send_input const &) const;
        mu_coin::EC::PublicKey key () const;
        mu_coin::uint256_union previous;
        mu_coin::uint256_union coin_balance;
    };
    class send_output
    {
    public:
        send_output () = default;
        send_output (EC::PublicKey const &, mu_coin::uint256_union const &);
        bool operator == (mu_coin::send_output const &) const;
        mu_coin::address destination;
        mu_coin::uint256_union coin_diff;
    };
    class send_block : public mu_coin::block
    {
    public:
        send_block () = default;
        send_block (send_block const &);
        mu_coin::uint256_t fee () const override;
        mu_coin::uint256_t hash () const override;
        bool balance (mu_coin::address const &, mu_coin::uint256_t &, uint16_t &) override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::send_block const &) const;
        bool validate ();
        std::vector <mu_coin::send_input> inputs;
        std::vector <mu_coin::send_output> outputs;
        std::vector <mu_coin::uint512_union> signatures;
    };
    class receive_block : public mu_coin::block
    {
    public:
        mu_coin::uint256_t fee () const override;
        mu_coin::uint256_t hash () const override;
        bool balance (mu_coin::address const &, mu_coin::uint256_t &, uint16_t &) override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        void sign (EC::PrivateKey const &, mu_coin::uint256_union const &);
        bool validate (mu_coin::uint256_union const &) const;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::receive_block const &) const;
        uint512_union signature;
        mu_coin::uint256_union source;
        mu_coin::uint256_union previous;
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
        std::unique_ptr <mu_coin::block> latest (mu_coin::address const &);
        std::unique_ptr <mu_coin::block> block (mu_coin::uint256_union const &);
        void insert_block (mu_coin::block const &);
        void insert_send (mu_coin::address const &, mu_coin::send_block const &);
        std::unique_ptr <mu_coin::send_block> send (mu_coin::address const &, mu_coin::uint256_union const &);
        void clear (mu_coin::address const &, mu_coin::uint256_union const &);
    private:
        Db handle;
    };
    class ledger
    {
    public:
        ledger (mu_coin::block_store &);
        std::unique_ptr <mu_coin::block> previous (mu_coin::address const &);
        mu_coin::uint256_union balance (mu_coin::address const &);
        mu_coin::address owner (mu_coin::uint256_union const &);
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
        mu_coin::EC::PublicKey pub;
        mu_coin::EC::PrivateKey prv;
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
        mu_coin::EC::PublicKey operator * ();
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
        void insert (mu_coin::EC::PublicKey const &, mu_coin::EC::PrivateKey const &, mu_coin::uint256_union const &);
        void insert (mu_coin::EC::PrivateKey const &, mu_coin::uint256_union const &);
        void fetch (mu_coin::EC::PublicKey const &, mu_coin::uint256_union const &, mu_coin::EC::PrivateKey &, bool &);
        std::unique_ptr <mu_coin::send_block> send (mu_coin::ledger &, mu_coin::address const &, mu_coin::uint256_t const &, mu_coin::uint256_union const &);
        key_iterator begin ();
        key_iterator end ();
    private:
        Db handle;
    };
    class client
    {
    public:
        client (boost::asio::io_service &, uint16_t, boost::filesystem::path const &, boost::filesystem::path const &);
        mu_coin::block_store store;
        mu_coin::ledger ledger;
        mu_coin::wallet wallet;
        mu_coin::node network;
    };
}