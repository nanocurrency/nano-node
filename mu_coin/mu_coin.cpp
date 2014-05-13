#include <mu_coin/mu_coin.hpp>
#include <cryptopp/sha.h>

bool mu_coin::address::operator == (mu_coin::address const & other_a) const
{
    return number.bytes == other_a.number.bytes;
}

mu_coin::address::address (uint256_union const & number_a) :
number (number_a)
{
}

CryptoPP::OID & mu_coin::oid ()
{
    static CryptoPP::OID result (CryptoPP::ASN1::secp256k1 ());
    return result;
}

CryptoPP::RandomNumberGenerator & mu_coin::pool ()
{
    static CryptoPP::AutoSeededRandomPool result;
    return result;
}

CryptoPP::ECP const & mu_coin::curve ()
{
    static CryptoPP::DL_GroupParameters_EC <CryptoPP::ECP> result (oid ());
    return result.GetCurve ();
};

mu_coin::entry::entry (EC::PublicKey const & pub, mu_coin::uint256_t const & coins_a, uint16_t sequence_a) :
coins (coins_a),
sequence (sequence_a)
{
    mu_coin::point_encoding encoding (pub);
    point_type = encoding.type ();
    address.number = encoding.point ();
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
    for (auto i (entries.begin ()), j (entries.end ()); i != j; ++i)
    {
        hash_number (hash, i->address.number.number ());
        hash_number (hash, i->coins);
        hash.Update (reinterpret_cast <uint8_t const *> (&i->sequence), sizeof (decltype (i->sequence)));
    }
    hash.Final (digest.bytes.data ());
    return digest.number ();
}

boost::multiprecision::uint256_t mu_coin::uint256_union::number () const
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

boost::multiprecision::uint256_t mu_coin::transaction_block::fee () const
{
    return 1;
}

void mu_coin::entry::sign (EC::PrivateKey const & private_key, mu_coin::uint256_union const & message)
{
    EC::Signer signer (private_key);
    signer.SignMessage (pool (), message.bytes.data (), sizeof (message), signature.bytes.data ());
}

bool mu_coin::entry::validate (EC::PublicKey const & public_key, mu_coin::uint256_union const & message)
{
    EC::Verifier verifier (public_key);
    auto result (verifier.VerifyMessage (message.bytes.data (), sizeof (message), signature.bytes.data (), sizeof (signature)));
    return result;
}

mu_coin::point_encoding::point_encoding (mu_coin::EC::PublicKey const & pub)
{
    curve ().EncodePoint (bytes.data (), pub.GetPublicElement(), true);
}

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
    mu_coin::uint256_t message (block_a->hash ());
    boost::multiprecision::uint256_t previous;
    boost::multiprecision::uint256_t next;
    for (auto i (block_a->entries.begin ()), j (block_a->entries.end ()); !result && i != j; ++i)
    {
        auto & address (i->address);
        i->validate (i->key (), message);
        auto existing (latest.find (address));
        if (i->sequence > 0)
        {
            if (existing != latest.end ())
            {
                auto previous_entry (std::find_if (existing->second->entries.begin (), existing->second->entries.end (), [&address] (mu_coin::entry const & entry_a) {return address == entry_a.address;}));
                if (previous_entry != existing->second->entries.end ())
                {
                    if (previous_entry->sequence + 1 == i->sequence)
                    {
                        previous += previous_entry->coins;
                        next += i->coins;
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
        else
        {
            if (existing == latest.end ())
            {
                next += i->coins;
            }
            else
            {
                result = true;
            }
        }
    }
    if (!result)
    {
        if (next < previous)
        {
            if (next + block_a->fee () == previous)
            {
                for (auto i (block_a->entries.begin ()), j (block_a->entries.end ()); i != j; ++i)
                {
                    latest [i->address] = block_a;
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
    return result;
}

mu_coin::EC::PublicKey mu_coin::point_encoding::key () const
{
    mu_coin::EC::PublicKey::Element element;
    auto valid (curve ().DecodePoint (element, bytes.data (), bytes.size ()));
    assert (valid);
    mu_coin::EC::PublicKey result;
    result.Initialize (oid (), element);
    return result;
}

mu_coin::point_encoding::point_encoding (uint8_t type_a, uint256_union const & point_a)
{
    assert (type_a == 2 || type_a == 3);
    bytes [0] = type_a;
    std::copy (point_a.bytes.begin (), point_a.bytes.end (), bytes.begin () + 1);
}

uint8_t mu_coin::point_encoding::type () const
{
    return bytes [0];
}

mu_coin::uint256_union mu_coin::point_encoding::point () const
{
    uint256_union result;
    std::copy (bytes.begin () + 1, bytes.end (), result.bytes.begin ());
    return result;
}

mu_coin::EC::PublicKey mu_coin::entry::key () const
{
    mu_coin::point_encoding point (point_type, address.number);
    return point.key ();
}