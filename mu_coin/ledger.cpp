#include <mu_coin/ledger.hpp>

mu_coin::balance & mu_coin::ledger_memory::balance (mu_coin::address const & address_a)
{
    auto existing (entries.find (address_a));
    if (existing != entries.end ())
    {
        return *existing->second;
    }
    else
    {
        return empty_balance;
    }
}