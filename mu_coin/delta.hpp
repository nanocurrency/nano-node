#pragma once
#include <boost/multiprecision/cpp_int.hpp>

namespace mu_coin {
    class delta
    {
    public:
        delta (boost::multiprecision::uint256_t const &, boost::multiprecision::uint256_t const &);
        boost::multiprecision::uint256_t coins;
        boost::multiprecision::uint256_t votes;
    };
}