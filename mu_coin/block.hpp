#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <mu_coin/address.hpp>
#include <mu_coin/delta.hpp>

namespace mu_coin {
    class entry
    {
    public:
        entry () = default;
        entry (boost::multiprecision::uint256_t const &, boost::multiprecision::uint256_t const &);
        mu_coin::address address;
        boost::multiprecision::uint256_t previous;
        boost::multiprecision::uint256_t coins;
    };
    class block
    {
    public:
        std::vector <entry> inputs;
        std::vector <entry> outputs;
        bool balanced () const;
        boost::multiprecision::uint256_t fee () const;
        boost::multiprecision::uint256_t hash () const;
    };
}