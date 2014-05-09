#include <mu_coin/block.hpp>
#include <cryptopp/sha.h>

CryptoPP::RandomNumberGenerator & mu_coin::pool ()
{
    static CryptoPP::AutoSeededRandomPool result;
    return result;
}

CryptoPP::OID & mu_coin::curve ()
{
    static CryptoPP::OID result (CryptoPP::ASN1::secp256k1 ());
    return result;
};

mu_coin::entry::entry (boost::multiprecision::uint256_t const & coins_a, boost::multiprecision::uint256_t const & previous_a) :
previous (previous_a),
coins (coins_a)
{
}

mu_coin::uint256_union::uint256_union (boost::multiprecision::uint256_t const & number_a)
{
    boost::multiprecision::uint256_t number_l (number_a);
    qwords [0] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [1] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [2] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [3] = number_l.convert_to <uint64_t> ();
    std::reverse (&bytes [0], &bytes [32]);
}

mu_coin::uint512_union::uint512_union (boost::multiprecision::uint512_t const & number_a)
{
    boost::multiprecision::uint512_t number_l (number_a);
    qwords [0] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [1] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [2] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [3] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [4] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [5] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [6] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [7] = number_l.convert_to <uint64_t> ();
    std::reverse (&bytes [0], &bytes [64]);
}

void mu_coin::uint256_union::clear ()
{
    bytes.fill (0);
}

void mu_coin::uint512_union::clear ()
{
    bytes.fill (0);
}

void hash_number (CryptoPP::SHA256 & hash_a, boost::multiprecision::uint256_t const & number_a)
{
    mu_coin::uint256_union bytes (number_a);
    hash_a.Update (bytes.bytes.data (), sizeof (bytes));
}

boost::multiprecision::uint256_t mu_coin::transaction_block::hash () const
{
    CryptoPP::SHA256 hash;
    mu_coin::uint256_union digest;
    for (std::vector <mu_coin::entry>::const_iterator i (inputs.begin ()), j (inputs.end ()); i != j; ++i)
    {
        hash_number (hash, i->previous);
        hash_number (hash, i->coins);
    }
    for (std::vector <mu_coin::entry>::const_iterator i (outputs.begin ()), j (outputs.end ()); i != j; ++i)
    {
        hash_number (hash, i->previous);
        hash_number (hash, i->coins);
    }
    hash.Final (digest.bytes.data ());
    return digest.number ();
}

boost::multiprecision::uint256_t mu_coin::uint256_union::number ()
{
    mu_coin::uint256_union temp (*this);
    std::reverse (&temp.bytes [0], &temp.bytes [32]);
    boost::multiprecision::uint256_t result (temp.qwords [3]);
    result <<= 64;
    result |= temp.qwords [2];
    result <<= 64;
    result |= temp.qwords [1];
    result <<= 64;
    result |= temp.qwords [0];
    return result;
}

boost::multiprecision::uint512_t mu_coin::uint512_union::number ()
{
    mu_coin::uint512_union temp (*this);
    std::reverse (&temp.bytes [0], &temp.bytes [64]);
    boost::multiprecision::uint512_t result (temp.qwords [7]);
    result <<= 64;
    result |= temp.qwords [6];
    result <<= 64;
    result |= temp.qwords [5];
    result <<= 64;
    result |= temp.qwords [4];
    result <<= 64;
    result |= temp.qwords [3];
    result <<= 64;
    result |= temp.qwords [2];
    result <<= 64;
    result |= temp.qwords [1];
    result <<= 64;
    result |= temp.qwords [0];
    return result;
}

bool mu_coin::transaction_block::balanced () const
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
    return input_sum - fee () == output_sum;
}

boost::multiprecision::uint256_t mu_coin::transaction_block::fee () const
{
    return 1;
}

void mu_coin::transaction_block::sign (EC::PrivateKey const & private_key)
{
    EC::Signer signer (private_key);
    mu_coin::uint256_union number (hash ());
    mu_coin::uint512_union signature_l;
    signer.SignMessage (pool (), number.bytes.data (), sizeof (number), signature_l.bytes.data ());
}