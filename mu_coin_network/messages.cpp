#include <mu_coin_network/messages.hpp>
#include <messages.pb.h>
#include <mu_coin/mu_coin.hpp>
#include <string>

void operator << (mu_coin_network::address & destination, mu_coin::address const & source)
{
    destination.mutable_point ()->assign (source.point.bytes.begin (), source.point.bytes.end ());
}

void operator << (mu_coin_network::block_id & destination, mu_coin::block_id const & source)
{
    *destination.mutable_address () << source.address;
    destination.set_sequence (source.sequence);
}

void operator << (mu_coin_network::entry & destination, mu_coin::entry const & source)
{
    destination.mutable_signature ()->assign (source.signature.bytes.begin (), source.signature.bytes.end ());
    destination.mutable_coins ()->assign (source.coins.bytes.begin (), source.coins.bytes.end ());
    *destination.mutable_id () << source.id;
}

bool operator << (mu_coin::address & destination, mu_coin_network::address const & source)
{
    auto result (false);
    if (source.point ().size () == 33)
    {
        std::move (source.point ().begin (), source.point ().end (), destination.point.bytes.data());
    }
    else
    {
        result = true;
    }
    return result;
}

bool operator << (mu_coin::block_id & destination, mu_coin_network::block_id const & source)
{
    auto result (destination.address << source.address());
    if (!result)
    {
        if (source.sequence () <= std::numeric_limits <uint16_t>::max ())
        {
            destination.sequence = source.sequence ();
        }
        else
        {
            result = true;
        }
    }
    return result;
}

bool operator << (mu_coin::entry & destination, mu_coin_network::entry const & source)
{
    auto result (destination.id << source.id ());
    if (!result)
    {
        if (source.coins ().size () == 32)
        {
            std::move (source.coins ().begin (), source.coins ().end (), destination.coins.bytes.begin ());
            if (source.signature ().size () == 64)
            {
                std::move (source.signature().begin (), source.signature ().end (), destination.signature.bytes.begin ());
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

void operator << (mu_coin_network::transaction_block & destination, mu_coin::transaction_block const & source)
{
    for (auto const & i: source.entries)
    {
        auto new_entry (destination.add_entries ());
        *new_entry << i;
    }
}

bool operator << (mu_coin::transaction_block & destination, mu_coin_network::transaction_block const & source)
{
    auto result (false);
    for (auto i (source.entries ().begin ()), j (source.entries ().end ()); i != j && !result; ++i)
    {
        destination.entries.push_back (mu_coin::entry ());
        auto & new_entry (destination.entries.back ());
        result = new_entry << *i;
    }
    return result;
}