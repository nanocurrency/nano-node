#pragma once
#include <boost/multiprecision/cpp_int.hpp>

namespace mu_coin {
    class address
    {
    public:
        address () = default;
        address (boost::multiprecision::uint256_t const &);
        bool operator == (mu_coin::address const &) const;
        boost::multiprecision::uint256_t number;
    };
}
namespace std {
    template <>
    struct hash <mu_coin::address>
    {
        size_t operator () (mu_coin::address const & address_a) const
        {
            size_t hash (address_a.number.convert_to <size_t> ());
            return hash;
        }
    };
}