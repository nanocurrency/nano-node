#pragma once
#include <mu_coin/address.hpp>
#include <unordered_map>

namespace mu_coin {
class block;
class ledger
{
public:
    mu_coin::block * previous (mu_coin::address const &);
    bool has_balance (mu_coin::address const &);
    std::unordered_map <mu_coin::address, mu_coin::block *> entries;
};
}