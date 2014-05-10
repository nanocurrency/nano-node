#include <mu_coin/ledger.hpp>
#include <mu_coin/block.hpp>

mu_coin::transaction_block * mu_coin::ledger::previous (mu_coin::address const & address_a)
{
    assert (has_balance (address_a));
    auto existing (latest.find (address_a));
    return existing->second;
}

bool mu_coin::ledger::has_balance (mu_coin::address const & address_a)
{
    return latest.find (address_a) != latest.end ();
}

void mu_coin::ledger::replace (mu_coin::address const & address_a, mu_coin::transaction_block * block_a)
{
}