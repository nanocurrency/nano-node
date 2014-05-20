#pragma once
#include <boost/multiprecision/cpp_int.hpp>

#include <cryptopp/eccrypto.h>
#include <cryptopp/eccrypto.h>
#include <cryptopp/osrng.h>
#include <cryptopp/oids.h>

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
        uint256_union (boost::multiprecision::uint256_t const &);
        uint256_union (std::string const &);
        uint256_union (EC::PrivateKey const &);
        uint256_union (EC::PrivateKey const &, uint256_union const &, uint128_union const &);
        EC::PrivateKey key (uint256_union const &, uint128_union const &);
        EC::PrivateKey key ();
        bool operator == (mu_coin::uint256_union const &) const;
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
        std::array <uint8_t, 33> bytes;
        uint128_union iv () const;
        EC::PublicKey key () const;
        uint8_t type () const;
        uint256_union point () const;
    };
    union uint512_union
    {
        uint512_union () = default;
        uint512_union (boost::multiprecision::uint512_t const &);
        bool operator == (mu_coin::uint512_union const &) const;
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
        virtual void serialize (mu_coin::byte_write_stream &) const = 0;
        virtual bool deserialize (mu_coin::byte_read_stream &) = 0;
        virtual void visit (mu_coin::block_visitor &) const = 0;
    };
    class block_id
    {
    public:
        block_id () = default;
        block_id (EC::PublicKey const &, uint16_t);
        block_id (mu_coin::address const &, uint16_t);
        void serialize (mu_coin::byte_write_stream &) const;
        bool deserialize (mu_coin::byte_read_stream &);
        bool operator == (mu_coin::block_id const &) const;
        mu_coin::address address;
        uint16_t sequence;
    };
    class entry
    {
    public:
        entry () = default;
        entry (EC::PublicKey const &, mu_coin::uint256_t const &, uint16_t);
        void sign (EC::PrivateKey const &, mu_coin::uint256_union const &);
        bool validate (mu_coin::uint256_union const &) const;
        bool operator == (mu_coin::entry const &) const;
        mu_coin::EC::PublicKey key () const;
        uint512_union signature;
        mu_coin::uint256_union coins;
        mu_coin::block_id id;
    };
    class transaction_block : public mu_coin::block
    {
    public:
        boost::multiprecision::uint256_t fee () const override;
        boost::multiprecision::uint256_t hash () const override;
        bool operator == (mu_coin::transaction_block const &) const;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &) override;
        void visit (mu_coin::block_visitor &) const override;
        std::vector <entry> entries;
    };
    class send_input
    {
    public:
        send_input () = default;
        send_input (EC::PublicKey const &, mu_coin::uint256_t const &, uint16_t);
        void sign (EC::PrivateKey const &, mu_coin::uint256_union const &);
        bool validate (mu_coin::uint256_union const &) const;
        bool operator == (mu_coin::send_input const &) const;
        mu_coin::EC::PublicKey key () const;
        uint512_union signature;
        mu_coin::block_id source;
        mu_coin::uint256_union coins;
    };
    class send_output
    {
    public:
        send_output () = default;
        send_output (EC::PublicKey const &, mu_coin::uint256_t const &);
        bool operator == (mu_coin::send_output const &) const;
        mu_coin::address address;
        mu_coin::uint256_union coins;
    };
    class send_block : public mu_coin::block
    {
    public:
        mu_coin::uint256_t fee () const override;
        mu_coin::uint256_t hash () const override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &) override;
        void visit (mu_coin::block_visitor &) const override;
        bool operator == (mu_coin::send_block const &) const;
        std::vector <mu_coin::send_input> inputs;
        std::vector <mu_coin::send_output> outputs;
    };
    class receive_block : public mu_coin::block
    {
    public:
        mu_coin::uint256_t fee () const override;
        mu_coin::uint256_t hash () const override;
        void serialize (mu_coin::byte_write_stream &) const override;
        bool deserialize (mu_coin::byte_read_stream &) override;
        void visit (mu_coin::block_visitor &) const override;
        void sign (EC::PrivateKey const &, mu_coin::uint256_union const &);
        bool validate (mu_coin::uint256_union const &) const;
        bool operator == (mu_coin::receive_block const &) const;
        uint512_union signature;
        mu_coin::block_id source;
        mu_coin::block_id output;
        mu_coin::uint256_union coins;
    };
    class block_visitor
    {
    public:
        virtual void send_block (mu_coin::send_block const &) = 0;
        virtual void receive_block (mu_coin::receive_block const &) = 0;
        virtual void transaction_block (mu_coin::transaction_block const &) = 0;
    };
    class block_store
    {
    public:
        virtual std::unique_ptr <mu_coin::transaction_block> latest (mu_coin::address const &) = 0;
        virtual void insert (mu_coin::block_id const &, mu_coin::transaction_block const &) = 0;
        virtual std::unique_ptr <mu_coin::transaction_block> block (mu_coin::block_id const &) = 0;
    };
    class ledger
    {
    public:
        ledger (mu_coin::block_store &);
        std::unique_ptr <mu_coin::transaction_block> previous (mu_coin::address const &);
        mu_coin::transaction_block * block (boost::multiprecision::uint256_t const &);
        mu_coin::uint256_union balance (mu_coin::address const &);
        bool has_balance (mu_coin::address const &);
        bool process (mu_coin::block const &);
        mu_coin::block_store & store;
    };
    class ledger_processor : public block_visitor
    {
    public:
        ledger_processor (mu_coin::ledger &);
        void send_block (mu_coin::send_block const &);
        void receive_block (mu_coin::receive_block const &);
        void transaction_block (mu_coin::transaction_block const &);
        mu_coin::ledger & ledger;
        bool result;
    };
    class block_store_memory : public block_store
    {
    public:
        std::unique_ptr <mu_coin::transaction_block> latest (mu_coin::address const &) override;
        void insert (mu_coin::block_id const &, mu_coin::transaction_block const &) override;
        std::unique_ptr <mu_coin::transaction_block> block (mu_coin::block_id const &) override;
    private:
        std::unordered_map <mu_coin::address, std::vector <mu_coin::transaction_block> *> blocks;
    };
    class keypair
    {
    public:
        keypair ();
        mu_coin::EC::PublicKey pub;
        mu_coin::EC::PrivateKey prv;
    };
}