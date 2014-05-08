#include <mu_coin/ledger.hpp>
#include <mu_coin/block.hpp>

mu_coin::block * mu_coin::ledger::previous (mu_coin::address const & address_a)
{
    assert (has_balance (address_a));
    auto existing (entries.find (address_a));
    return existing->second;
}

bool mu_coin::ledger::has_balance (mu_coin::address const & address_a)
{
    return entries.find (address_a) != entries.end ();
}