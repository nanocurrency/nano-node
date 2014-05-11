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

bool mu_coin::ledger::process (mu_coin::transaction_block * block_a)
{
    auto result (false);
    boost::multiprecision::uint256_t previous;
    boost::multiprecision::uint256_t next;
    for (auto i (block_a->entries.begin ()), j (block_a->entries.end ()); !result && i != j; ++i)
    {
        auto existing (latest.find (i->first));
        if (existing != latest.end ())
        {
            if (existing->second->hash () == i->second.previous)
            {
                auto last (existing->second->entries.find (i->first));
                if (last != existing->second->entries.end ())
                {
                    previous += last->second.coins;
                    next += i->second.coins;
                }
                else
                {
                    result = false;
                }
            }
            else
            {
                result = false;
            }
        }
        else
        {
            result = true;
        }
    }
    if (!result)
    {
        if (next < previous)
        {
            if (next + block_a->fee () == previous)
            {
                
            }
            else
            {
                result = true;
            }
        }
        else
        {
            result = true;
        }
    }
    return result;
}