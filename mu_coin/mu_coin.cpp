#include <mu_coin/mu_coin.hpp>
#include <cryptopp/sha.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

bool mu_coin::address::operator == (mu_coin::address const & other_a) const
{
    return point.bytes == other_a.point.bytes;
}

mu_coin::address::address (point_encoding const & point_a) :
point (point_a)
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
id (pub, sequence_a)
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
    for (auto i (entries.begin ()), j (entries.end ()); i != j; ++i)
    {
        hash_number (hash, i->coins.number ());
        hash.Update (i->id.address.point.bytes.data (), sizeof (i->id.address.point.bytes));
        hash.Update (reinterpret_cast <uint8_t const *> (&i->id.sequence), sizeof (decltype (i->id.sequence)));
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

bool mu_coin::entry::validate (mu_coin::uint256_union const & message) const
{
    EC::Verifier verifier (key ());
    auto result (verifier.VerifyMessage (message.bytes.data (), sizeof (message), signature.bytes.data (), sizeof (signature)));
    return result;
}

mu_coin::point_encoding::point_encoding (mu_coin::EC::PublicKey const & pub)
{
    curve ().EncodePoint (bytes.data (), pub.GetPublicElement(), true);
}

std::unique_ptr <mu_coin::transaction_block> mu_coin::ledger::previous (mu_coin::address const & address_a)
{
    assert (has_balance (address_a));
    auto existing (store.latest (address_a));
    return existing;
}

bool mu_coin::ledger::has_balance (mu_coin::address const & address_a)
{
    return store.latest (address_a) != nullptr;
}

bool mu_coin::ledger::process (mu_coin::transaction_block const & block_a)
{
    auto result (false);
    mu_coin::uint256_t message (block_a.hash ());
    boost::multiprecision::uint256_t previous;
    boost::multiprecision::uint256_t next;
    for (auto i (block_a.entries.begin ()), j (block_a.entries.end ()); !result && i != j; ++i)
    {
        auto & address (i->id.address);
        auto valid (i->validate (message));
        if (valid)
        {
            auto existing (store.latest (address));
            if (i->id.sequence > 0)
            {
                if (existing != nullptr)
                {
                    auto previous_entry (std::find_if (existing->entries.begin (), existing->entries.end (), [&address] (mu_coin::entry const & entry_a) {return address == entry_a.id.address;}));
                    if (previous_entry != existing->entries.end ())
                    {
                        if (previous_entry->id.sequence + 1 == i->id.sequence)
                        {
                            previous += previous_entry->coins.number ();
                            next += i->coins.number ();
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
                if (existing == nullptr)
                {
                    next += i->coins.number ();
                }
                else
                {
                    result = true;
                }
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
            if (next + block_a.fee () == previous)
            {
                for (auto i (block_a.entries.begin ()), j (block_a.entries.end ()); i != j; ++i)
                {
                    store.insert (i->id, block_a);
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
    return id.address.point.key ();
}

mu_coin::keypair::keypair ()
{
    prv.Initialize (pool (), oid ());
    prv.MakePublicKey (pub);
}

std::unique_ptr <mu_coin::transaction_block> mu_coin::block_store_memory::latest (mu_coin::address const & address_a)
{
    auto existing (blocks.find (address_a));
    if (existing != blocks.end ())
    {
        return std::unique_ptr <mu_coin::transaction_block> (new mu_coin::transaction_block (existing->second->back ()));
    }
    else
    {
        return nullptr;
    }
}

void mu_coin::block_store_memory::insert (mu_coin::block_id const & block_id_a, mu_coin::transaction_block const & block)
{
    auto existing (blocks.find (block_id_a.address));
    if (existing != blocks.end ())
    {
        assert (existing->second->size () == block_id_a.sequence);
        existing->second->push_back (block);
    }
    else
    {
        assert (block_id_a.sequence == 0);
        auto blocks_l (new std::vector <mu_coin::transaction_block>);
        blocks [block_id_a.address] = blocks_l;
        blocks_l->push_back (block);
    }
}

mu_coin::ledger::ledger (mu_coin::block_store & store_a) :
store (store_a)
{
}

bool mu_coin::entry::operator == (mu_coin::entry const & other_a) const
{
    return signature == other_a.signature && id.address == other_a.id.address && coins == other_a.coins && id.sequence == other_a.id.sequence;
}

bool mu_coin::uint256_union::operator == (mu_coin::uint256_union const & other_a) const
{
    return bytes == other_a.bytes;
}

bool mu_coin::uint512_union::operator == (mu_coin::uint512_union const & other_a) const
{
    return bytes == other_a.bytes;
}

bool mu_coin::transaction_block::operator == (mu_coin::transaction_block const & other_a) const
{
    return entries == other_a.entries;
}

void mu_coin::transaction_block::serialize (mu_coin::byte_write_stream & data_a) const
{
    uint32_t size (entries.size ());
    data_a.write (size);
    for (auto & i: entries)
    {
        data_a.write (i.signature.bytes);
        data_a.write (i.coins.bytes);
        i.id.serialize (data_a);
    }
}

bool mu_coin::transaction_block::deserialize (byte_read_stream & data)
{
    auto error (false);
    uint32_t size;
    error = data.read (size);
    if (!error)
    {
        for (uint32_t i (0), j (size); i < j && !error; ++i)
        {
            static size_t const entry_size (sizeof (mu_coin::uint512_union) + sizeof (mu_coin::uint256_union) + sizeof (uint16_t));
            if (data.size () >= entry_size)
            {
                entries.push_back (mu_coin::entry ());
                auto & signature (entries.back ().signature.bytes);
                data.read (signature);
                auto & coins (entries.back ().coins.bytes);
                data.read (coins);
                error = entries.back ().id.deserialize (data);
            }
            else
            {
                error = true;
            }
        }
        if (data.size () > 0)
        {
            error = true;
        }
    }
    else
    {
        error = true;
    }    
    return error;
}

size_t mu_coin::byte_read_stream::byte_read_stream::size ()
{
    return end - data;
}

mu_coin::byte_read_stream::byte_read_stream (uint8_t * data_a, uint8_t * end_a) :
data (data_a),
end (end_a)
{
}

mu_coin::byte_read_stream::byte_read_stream (uint8_t * data_a, size_t size_a) :
data (data_a),
end (data_a + size_a)
{
}

bool mu_coin::byte_read_stream::read (uint8_t * value, size_t size_a)
{
    auto result (false);
    if (data + size_a <= end)
    {
        std::copy (data, data + size_a, value);
        data += size_a;
    }
    else
    {
       result = true;
    }
    return result;
}

mu_coin::byte_write_stream::byte_write_stream () :
data (nullptr),
size (0)
{
}

mu_coin::byte_write_stream::~byte_write_stream ()
{
    free (data);
}

void mu_coin::byte_write_stream::extend (size_t additional)
{
    data = size ? reinterpret_cast <uint8_t *> (realloc (data, size + additional)) : reinterpret_cast <uint8_t *> (malloc (additional));
    size += additional;
}

void mu_coin::byte_write_stream::write (uint8_t const * data_a, size_t size_a)
{
    extend (size_a);
    std::copy (data_a, data_a + size_a, data + size - size_a);
}

mu_coin::block_id::block_id (EC::PublicKey const & pub, uint16_t sequence_a) :
address (pub),
sequence (sequence_a)
{
}

mu_coin::address::address (EC::PublicKey const & pub_a) :
point (pub_a)
{
}

void mu_coin::block_id::serialize (mu_coin::byte_write_stream & stream_a) const
{
    stream_a.write (address.point.bytes);
    stream_a.write (sequence);
}

bool mu_coin::block_id::deserialize (mu_coin::byte_read_stream & stream_a)
{
    auto result (false);
    static size_t const block_id_size (sizeof (decltype (address.point.bytes)) + sizeof (decltype (sequence)));
    if (stream_a.size () >= block_id_size)
    {
        result = address.deserialize (stream_a);
        auto & sequence_l (sequence);
        stream_a.read (sequence_l);
    }
    else
    {
        result = true;
    }
    return result;
}

void mu_coin::address::serialize (mu_coin::byte_write_stream & stream_a) const
{
    stream_a.write (point.bytes);
}

bool mu_coin::address::deserialize (mu_coin::byte_read_stream & stream_a)
{
    auto & point_l (point.bytes);
    return stream_a.read (point_l);
}

std::unique_ptr <mu_coin::transaction_block> mu_coin::block_store_memory::block (mu_coin::block_id const & id_a)
{
    std::unique_ptr <mu_coin::transaction_block> result;
    auto existing (blocks.find (id_a.address));
    if (existing != blocks.end ())
    {
        if (existing->second->size () > id_a.sequence)
        {
            std::unique_ptr <mu_coin::transaction_block> data (new mu_coin::transaction_block ((*existing->second) [id_a.sequence]));
            result = std::move (data);
        }
    }
    return result;
}

mu_coin::block_id::block_id (mu_coin::address const & address_a, uint16_t sequence_a) :
address (address_a),
sequence (sequence_a)
{
}

bool mu_coin::block_id::operator == (mu_coin::block_id const & other_a) const
{
    return address == other_a.address && sequence == other_a.sequence;
}

bool mu_coin::point_encoding::validate ()
{
    mu_coin::EC::PublicKey::Element element;
    auto valid (curve ().DecodePoint (element, bytes.data (), bytes.size ()));
    return !valid;
}

mu_coin::uint512_union::uint512_union (EC::PrivateKey const & prv, uint256_union const & key)
{
    mu_coin::uint256_union exponent (prv);
    CryptoPP::SHA256 hash;
    hash.Update (exponent.bytes.data (), sizeof (exponent.bytes));
    hash.Final (uint256s [0].bytes.data ());
    CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Encryption enc (alg, uint256s [0].bytes.data ());
    enc.ProcessData (uint256s [1].bytes.data (), exponent.bytes.data (), sizeof (exponent.bytes));
}

mu_coin::uint256_union::uint256_union (EC::PrivateKey const & prv)
{
    prv.GetPrivateExponent ().Encode (bytes.data (), sizeof (bytes));
}

mu_coin::EC::PrivateKey mu_coin::uint512_union::key (uint256_union const & key_a)
{
    CryptoPP::AES::Decryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Decryption dec (alg, uint256s [0].bytes.data ());
    mu_coin::uint256_union exponent;
    dec.ProcessData (exponent.bytes.data (), uint256s [1].bytes.data (), sizeof (uint256s [1].bytes));
    mu_coin::EC::PrivateKey result (exponent.key ());
    return result;
}

mu_coin::EC::PrivateKey mu_coin::uint256_union::key ()
{
    mu_coin::EC::PrivateKey result;
    result.Initialize (oid (), CryptoPP::Integer (bytes.data (), sizeof (bytes)));
    return result;
}