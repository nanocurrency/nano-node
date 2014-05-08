#include <mu_coin/delta.hpp>

mu_coin::delta::delta (boost::multiprecision::uint256_t const & coins_a, boost::multiprecision::uint256_t const & votes_a) :
coins (coins_a),
votes (votes_a)
{
}