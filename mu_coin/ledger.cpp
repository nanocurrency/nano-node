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
    boost::multiprecision::uint256_t inputs;
    for (auto i (block_a->inputs.begin ()), j (block_a->inputs.end ()); !result && i != j; ++i)
    {
        auto existing (latest.find (i->first));
        if (existing != latest.end ())
        {
            if (existing->second->hash () == i->second.previous)
            {
                auto last (existing->second->outputs.find (i->first));
                if (last != existing->second->outputs.end ())
                {
                    if (last->second.coins > i->second.coins)
                    {
                        inputs += last->second.coins - i->second.coins;
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
        boost::multiprecision::uint256_t outputs;
        for (auto i (block_a->outputs.begin ()), j (block_a->outputs.end ()); !result && i != j; ++i)
        {
            auto existing (latest.find (i->first));
            if (existing != latest.end ())
            {
                if (existing->second->hash() == i->second.previous)
                {
                    auto last (existing->second->outputs.find (i->first));
                    if (last != existing->second->outputs.end ())
                    {
                        if (i->second.coins > last->second.coins)
                        {
                            outputs += last->second.coins - i->second.coins;
                        }
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
            else
            {
                result = true;
            }
        }
    }
}