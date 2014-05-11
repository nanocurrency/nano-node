#pragma once
#include <mu_coin/address.hpp>
#include <unordered_map>

namespace std
{
    template <>
    struct hash <boost::multiprecision::uint256_t>
    {
        size_t operator () (boost::multiprecision::uint256_t const & number_a) const
        {
            return number_a.convert_to <size_t> ();
        }
    };
}
namespace mu_coin {
    class transaction_block;
    class ledger
    {
    public:
        mu_coin::transaction_block * previous (mu_coin::address const &);
        mu_coin::transaction_block * block (boost::multiprecision::uint256_t const &);
        bool has_balance (mu_coin::address const &);
        bool process (mu_coin::transaction_block *);
        std::unordered_map <mu_coin::address, mu_coin::transaction_block *> latest;
        std::unordered_map <boost::multiprecision::uint256_t, mu_coin::transaction_block *> blocks;
    };
}