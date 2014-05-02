#include <mu_coin/block.hpp>

mu_coin::address mu_coin::block_memory::address () const
{
    return address_m;
}

mu_coin::block_hash mu_coin::block_memory::previous () const
{
    return previous_m;
}

boost::multiprecision::uint256_t mu_coin::block_memory::coins () const
{
    return coins_m;
}

boost::multiprecision::uint256_t mu_coin::block_memory::votes () const
{
    return votes_m;
}

mu_coin::block_memory::block_memory (boost::multiprecision::uint256_t const & coins_a, boost::multiprecision::uint256_t const & votes_a) :
coins_m (coins_a),
votes_m (votes_a)
{
}