#pragma once
#include <mu_coin/balance.hpp>
#include <mu_coin/address.hpp>
#include <unordered_map>

namespace mu_coin {
    class ledger
    {
    public:
        virtual mu_coin::balance & balance (mu_coin::address const &) = 0;
    };

    class ledger_memory : public ledger
    {
    public:
        mu_coin::balance & balance (mu_coin::address const &) override;
        mu_coin::balance_memory empty_balance;
        std::unordered_map <mu_coin::address, mu_coin::balance_memory *> entries;
    };
}