#include <mu_coin/address.hpp>

bool mu_coin::address::operator == (mu_coin::address const & other_a) const
{
    return number == other_a.number;
}

mu_coin::address::address (boost::multiprecision::uint256_t const & number_a) :
number (number_a)
{
}