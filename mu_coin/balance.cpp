#include <mu_coin/balance.hpp>
#include <mu_coin/block.hpp>

void mu_coin::balance_memory::operator += (mu_coin::block const & block_a)
{
    coins_m += block_a.coins ();
    votes_m += block_a.votes ();
}

void mu_coin::balance_memory::operator -= (mu_coin::block const & block_a)
{
    coins_m -= block_a.coins ();
    votes_m -= block_a.votes ();
}

boost::multiprecision::uint256_t mu_coin::balance_memory::coins () const
{
    return coins_m;
}

boost::multiprecision::uint256_t mu_coin::balance_memory::votes () const
{
    return votes_m;
}