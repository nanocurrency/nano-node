#include <mu_coin/block.hpp>
#include <cryptopp/sha.h>

mu_coin::entry::entry (boost::multiprecision::uint256_t const & coins_a, boost::multiprecision::uint256_t const & previous_a) :
previous (previous_a),
coins (coins_a)
{
}

void hash_number (CryptoPP::SHA256 & hash_a, boost::multiprecision::uint256_t const & number_a)
{
    uint64_t bytes;
    uint8_t * first (reinterpret_cast <uint8_t *> (&bytes));
    uint8_t * last (reinterpret_cast <uint8_t *> (&bytes + 1));
    bytes = (number_a >> 192).convert_to <uint64_t> ();
    std::reverse (first, last);
    hash_a.Update (first, sizeof (bytes));
    bytes = (number_a >> 128).convert_to <uint64_t> ();
    std::reverse (first, last);
    hash_a.Update (first, sizeof (bytes));
    bytes = (number_a >> 64).convert_to <uint64_t> ();
    std::reverse (first, last);
    hash_a.Update (first, sizeof (bytes));
    bytes = number_a.convert_to <uint64_t> ();
    std::reverse (first, last);
    hash_a.Update (first, sizeof (bytes));
}

boost::multiprecision::uint256_t mu_coin::block::hash () const
{
    CryptoPP::SHA256 hash;
    uint8_t digest [32];
    for (std::vector <mu_coin::entry>::const_iterator i (inputs.begin ()), j (inputs.end ()); i != j; ++i)
    {
        hash_number (hash, i->address.number);
        hash_number (hash, i->previous);
        hash_number (hash, i->coins);
    }
    for (std::vector <mu_coin::entry>::const_iterator i (outputs.begin ()), j (outputs.end ()); i != j; ++i)
    {
        hash_number (hash, i->address.number);
        hash_number (hash, i->previous);
        hash_number (hash, i->coins);
    }
    hash.Final (digest);
    uint64_t bytes;
    uint8_t * first (reinterpret_cast <uint8_t *> (&bytes));
    uint8_t * last (reinterpret_cast <uint8_t *> (&bytes + 1));
    bytes = *reinterpret_cast <uint64_t *> (digest);
    std::reverse (first, last);
    boost::multiprecision::uint256_t result (bytes);
    bytes = *reinterpret_cast <uint64_t *> (digest + 8);
    std::reverse (first, last);
    result = (result << 64) | bytes;
    bytes = *reinterpret_cast <uint64_t *> (digest + 16);
    std::reverse (first, last);
    result = (result << 64) | bytes;
    bytes = *reinterpret_cast <uint64_t *> (digest + 24);
    std::reverse (first, last);
    result = (result << 64) | bytes;
    return result;
}

bool mu_coin::block::balanced () const
{
    boost::multiprecision::uint256_t input_sum;
    for (std::vector <mu_coin::entry>::const_iterator i (inputs.begin ()), j (inputs.end ()); i != j; ++i)
    {
        input_sum += i->coins;
    }
    boost::multiprecision::uint256_t output_sum;
    for (std::vector <mu_coin::entry>::const_iterator i (outputs.begin ()), j (outputs.end ()); i != j; ++i)
    {
        output_sum += i->coins;
    }
    return input_sum == output_sum;
}