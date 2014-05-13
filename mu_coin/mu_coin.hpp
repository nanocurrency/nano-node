#pragma once
#include <boost/multiprecision/cpp_int.hpp>

#include <cryptopp/eccrypto.h>
#include <cryptopp/eccrypto.h>
#include <cryptopp/osrng.h>
#include <cryptopp/oids.h>

#include <unordered_map>

namespace mu_coin {
    using uint256_t = boost::multiprecision::uint256_t;
    using EC = CryptoPP::ECDSA <CryptoPP::ECP, CryptoPP::SHA256>;
    CryptoPP::OID & oid ();
    CryptoPP::RandomNumberGenerator & pool ();
    CryptoPP::ECP const & curve ();
    union uint256_union
    {
        uint256_union () = default;
        uint256_union (boost::multiprecision::uint256_t const &);
        std::array <uint8_t, 32> bytes;
        std::array <uint64_t, 4> qwords;
        void clear ();
        boost::multiprecision::uint256_t number () const;
    };
    struct point_encoding
    {
        point_encoding (EC::PublicKey const &);
        point_encoding (uint8_t, uint256_union const &);
        std::array <uint8_t, 33> bytes;
        EC::PublicKey key () const;
        uint8_t type () const;
        uint256_union point () const;
    };
    union uint512_union
    {
        uint512_union () = default;
        uint512_union (boost::multiprecision::uint512_t const &);
        std::array <uint8_t, 64> bytes;
        std::array <uint64_t, 8> qwords;
        void clear ();
        boost::multiprecision::uint512_t number ();
    };
    class address
    {
    public:
        address () = default;
        address (uint256_union const &);
        bool operator == (mu_coin::address const &) const;
        uint256_union number;
    };
}

namespace std
{
    template <>
    struct hash <mu_coin::address>
    {
        size_t operator () (mu_coin::address const & address_a) const
        {
            size_t hash (address_a.number.qwords [0]);
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
    class block
    {
    public:
        virtual boost::multiprecision::uint256_t fee () const = 0;
        virtual boost::multiprecision::uint256_t hash () const = 0;
    };
    class entry
    {
    public:
        entry () = default;
        entry (EC::PublicKey const &, mu_coin::uint256_t const &, uint16_t);
        void sign (EC::PrivateKey const &, mu_coin::uint256_union const &);
        bool validate (EC::PublicKey const &, mu_coin::uint256_union const &);
        mu_coin::EC::PublicKey key () const;
        uint512_union signature;
        mu_coin::address address;
        mu_coin::uint256_t coins;
        uint16_t sequence;
        uint8_t point_type;
    };
    class transaction_block : public mu_coin::block
    {
    public:
        boost::multiprecision::uint256_t fee () const override;
        boost::multiprecision::uint256_t hash () const override;
        std::vector <entry> entries;
    };
    class ledger
    {
    public:
        mu_coin::transaction_block * previous (mu_coin::address const &);
        mu_coin::transaction_block * block (boost::multiprecision::uint256_t const &);
        bool has_balance (mu_coin::address const &);
        bool process (mu_coin::transaction_block *);
        std::unordered_map <mu_coin::address, mu_coin::transaction_block *> latest;
    };
}