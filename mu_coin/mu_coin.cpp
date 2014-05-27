#include <mu_coin/mu_coin.hpp>

#include <cryptopp/sha.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/base64.h>

#include <boost/filesystem.hpp>

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

static void sign_message (mu_coin::EC::PrivateKey const & private_key, mu_coin::uint256_union const & message, mu_coin::uint512_union & signature)
{
    mu_coin::EC::Signer signer (private_key);
    signer.SignMessage (mu_coin::pool (), message.bytes.data (), sizeof (message), signature.bytes.data ());
}

void mu_coin::entry::sign (EC::PrivateKey const & private_key, mu_coin::uint256_union const & message)
{
    sign_message (private_key, message, signature);
}

static bool validate_message (mu_coin::uint256_union const & message, mu_coin::uint512_union const & signature, mu_coin::EC::PublicKey const & key)
{
    mu_coin::EC::Verifier verifier (key);
    auto success (verifier.VerifyMessage (message.bytes.data (), sizeof (message), signature.bytes.data (), sizeof (signature)));
    return !success;
}

bool mu_coin::entry::validate (mu_coin::uint256_union const & message) const
{
    return validate_message (message, signature, key ());
}

bool mu_coin::send_input::validate (mu_coin::uint256_union const & message) const
{
    return validate_message (message, signature, key ());
}

mu_coin::EC::PublicKey mu_coin::send_input::key () const
{
    return source.address.point.key ();
}

mu_coin::point_encoding::point_encoding (mu_coin::EC::PublicKey const & pub)
{
    curve ().EncodePoint (bytes.data (), pub.GetPublicElement(), true);
}

std::unique_ptr <mu_coin::block> mu_coin::ledger::previous (mu_coin::address const & address_a)
{
    auto existing (store.latest (address_a));
    return existing;
}

mu_coin::uint256_union mu_coin::ledger::balance (mu_coin::address const & address_a)
{
    auto previous_l (previous (address_a));
    if (previous_l != nullptr)
    {
        mu_coin::uint256_t coins;
        uint16_t sequence;
        auto error (previous_l->balance (address_a, coins, sequence));
        assert (!error);
        return coins;
    }
    else
    {
        return mu_coin::uint256_t (0);
    }
}

bool mu_coin::ledger::has_balance (mu_coin::address const & address_a)
{
    return !balance (address_a).number ().is_zero ();
}

bool mu_coin::ledger::process (mu_coin::block const & block_a)
{
    mu_coin::ledger_processor processor (*this);
    block_a.visit (processor);
    return processor.result;
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
    assign (type_a, point_a);
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
    auto result (!curve ().DecodePoint (element, bytes.data (), bytes.size ()));
    if (!result)
    {
        mu_coin::EC::PublicKey pub;
        pub.Initialize (mu_coin::oid (), element);
        result = !pub.Validate (mu_coin::pool (), 3);
    }
    return result;
}

mu_coin::uint256_union::uint256_union (EC::PrivateKey const & prv, uint256_union const & key, uint128_union const & iv)
{
    mu_coin::uint256_union exponent (prv);
    CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
    enc.ProcessData (bytes.data (), exponent.bytes.data (), sizeof (exponent.bytes));
}

mu_coin::uint256_union::uint256_union (EC::PrivateKey const & prv)
{
    prv.GetPrivateExponent ().Encode (bytes.data (), sizeof (bytes));
}

mu_coin::EC::PrivateKey mu_coin::uint256_union::key (uint256_union const & key_a, uint128_union const & iv)
{
    CryptoPP::AES::Decryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
    mu_coin::uint256_union exponent;
    dec.ProcessData (exponent.bytes.data (), bytes.data (), sizeof (bytes));
    mu_coin::EC::PrivateKey result (exponent.key ());
    return result;
}

mu_coin::EC::PrivateKey mu_coin::uint256_union::key ()
{
    mu_coin::EC::PrivateKey result;
    result.Initialize (oid (), CryptoPP::Integer (bytes.data (), sizeof (bytes)));
    return result;
}

mu_coin::uint128_union mu_coin::point_encoding::iv () const
{
    mu_coin::uint128_union result;
    std::copy (bytes.begin (), bytes.begin () + sizeof (result.bytes), result.bytes.begin ());
    return result;
}

mu_coin::uint256_union::uint256_union (std::string const & password_a)
{
    CryptoPP::SHA256 hash;
    hash.Update (reinterpret_cast <uint8_t const *> (password_a.c_str ()), password_a.size ());
    hash.Final (bytes.data ());
}

void mu_coin::transaction_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.transaction_block (*this);
}

void mu_coin::send_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.send_block (*this);
}

void mu_coin::receive_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.receive_block (*this);
}

mu_coin::uint256_t mu_coin::send_block::fee () const
{
    return 1;
}

mu_coin::uint256_t mu_coin::send_block::hash () const
{
    mu_coin::uint256_union result;
    CryptoPP::SHA256 hash;
    for (auto & i: inputs)
    {
        hash.Update (i.source.address.point.bytes.data (), sizeof (i.source.address.point.bytes));
        hash.Update (reinterpret_cast <uint8_t const *> (&i.source.sequence), sizeof (i.source.sequence));
        hash.Update (i.coins.bytes.data (), sizeof (i.coins.bytes));
    }
    for (auto & i: outputs)
    {
        hash.Update (i.address.point.bytes.data (), sizeof (i.address.point.bytes));
        hash.Update (i.coins.bytes.data (), sizeof (i.coins.bytes));
    }
    hash.Final (result.bytes.data ());
    return result.number ();
}

void mu_coin::send_block::serialize (mu_coin::byte_write_stream & stream) const
{
    stream.write (static_cast <uint8_t> (mu_coin::block_type::send));
    uint16_t input_count (inputs.size ());
    stream.write (input_count);
    for (auto & i: inputs)
    {
        stream.write (i.signature.bytes);
        stream.write (i.source.address.point.bytes);
        stream.write (i.coins.bytes);
        stream.write (htons (i.source.sequence));
    }
    uint16_t output_count (outputs.size ());
    stream.write (output_count);
    for (auto & i: outputs)
    {
        stream.write (i.address.point.bytes);
        stream.write (i.coins.bytes);
    }
}

bool mu_coin::send_block::deserialize (mu_coin::byte_read_stream & stream)
{
    auto result (false);
    uint8_t type;
    result = stream.read (type);
    assert (!result);
    assert (static_cast <mu_coin::block_type> (type) == mu_coin::block_type::send);
    if (!result)
    {
        uint16_t input_count;
        result = stream.read (input_count);
        if (!result)
        {
            inputs.reserve (input_count);
            for (uint16_t i (0); !result && i < input_count; ++i)
            {
                inputs.push_back (mu_coin::send_input ());
                auto & back (inputs.back ());
                result = stream.read (back.signature.bytes);
                if (!result)
                {
                    result = stream.read (back.source.address.point.bytes);
                    if (!result)
                    {
                        result = back.source.address.point.validate ();
                        if (!result)
                        {
                            result = stream.read (back.coins.bytes);
                            if (!result)
                            {
                                result = stream.read (back.source.sequence);
                                back.source.sequence = ntohs (back.source.sequence);
                            }
                        }
                    }
                }
            }
            if (!result)
            {
                uint16_t output_count;
                result = stream.read (output_count);
                outputs.reserve (output_count);
                if (!result)
                {
                    for (uint16_t i (0); !result && i < output_count; ++i)
                    {
                        outputs.push_back (mu_coin::send_output ());
                        auto & back (outputs.back ());
                        result = stream.read (back.address.point.bytes);
                        if (!result)
                        {
                            result = back.address.point.validate ();
                            if (!result)
                            {
                                result = stream.read (back.coins);
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

bool mu_coin::send_block::operator == (mu_coin::send_block const & other_a) const
{
    auto result (inputs == other_a.inputs && outputs == other_a.outputs);
    return result;
}

bool mu_coin::send_input::send_input::operator == (mu_coin::send_input const & other_a) const
{
    auto result (source == other_a.source && coins == other_a.coins && signature == other_a.signature);
    return result;
}

mu_coin::send_input::send_input (EC::PublicKey const & pub_a, mu_coin::uint256_t const & coins_a, uint16_t sequence_a) :
source (pub_a, sequence_a),
coins (coins_a)
{
}

mu_coin::send_output::send_output (EC::PublicKey const & pub, mu_coin::uint256_t const & coins_a) :
address (pub),
coins (coins_a)
{
}

bool mu_coin::send_output::operator == (mu_coin::send_output const & other_a) const
{
    auto result (address == other_a.address && coins == other_a.coins);
    return result;
}

void mu_coin::send_input::sign (EC::PrivateKey const & prv, mu_coin::uint256_union const & message)
{
    sign_message (prv, message, signature);
}

void mu_coin::receive_block::sign (EC::PrivateKey const & prv, mu_coin::uint256_union const & message)
{
    sign_message (prv, message, signature);
}

bool mu_coin::receive_block::operator == (mu_coin::receive_block const & other_a) const
{
    auto result (signature == other_a.signature && output == other_a.output && source == other_a.source && coins == other_a.coins);
    return result;
}

bool mu_coin::receive_block::deserialize (mu_coin::byte_read_stream & stream_a)
{
    auto result (false);
    result = stream_a.read (signature.bytes);
    if (!result)
    {
        result = stream_a.read (source.address.point.bytes);
        if (!result)
        {
            result = source.address.point.validate ();
            if (!result)
            {
                result = stream_a.read (source.sequence);
                if (!result)
                {
                    result = stream_a.read (output.address.point.bytes);
                    if (!result)
                    {
                        result = stream_a.read (output.sequence);
                        if (!result)
                        {
                            result = output.address.point.validate ();
                            if (!result)
                            {
                                result = stream_a.read (coins.bytes);
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

void mu_coin::receive_block::serialize (mu_coin::byte_write_stream & stream_a) const
{
    stream_a.write (signature.bytes);
    stream_a.write (source.address.point.bytes);
    stream_a.write (source.sequence);
    stream_a.write (output.address.point.bytes);
    stream_a.write (output.sequence);
    stream_a.write (coins.bytes);
}

mu_coin::uint256_t mu_coin::receive_block::fee () const
{
    return 1;
}

mu_coin::uint256_t mu_coin::receive_block::hash () const
{
    CryptoPP::SHA256 hash;
    hash.Update (source.address.point.bytes.data (), sizeof (source.address.point.bytes));
    hash.Update (reinterpret_cast <uint8_t const *> (&source.sequence), sizeof (source.sequence));
    hash.Update (output.address.point.bytes.data (), sizeof (output.address.point.bytes));
    hash.Update (coins.bytes.data (), sizeof (coins.bytes));
    hash.Update (reinterpret_cast <uint8_t const *> (&output.sequence), sizeof (output.sequence));
    mu_coin::uint256_union result;
    hash.Final (result.bytes.data ());
    return result.number ();
}

void mu_coin::ledger_processor::send_block (mu_coin::send_block const & block_a)
{
    result = false;
    mu_coin::uint256_t message (block_a.hash ());
    mu_coin::uint256_t inputs;
    for (auto i (block_a.inputs.begin ()), j (block_a.inputs.end ()); !result && i != j; ++i)
    {
        auto & address (i->source.address);
        result = i->validate (message);
        if (!result)
        {
            if (i->source.sequence > 0)
            {
                auto existing (ledger.store.latest (address));
                if (existing != nullptr)
                {
                    mu_coin::uint256_t coins;
                    uint16_t sequence;
                    result = existing->balance (address, coins, sequence);
                    if (!result)
                    {
                        if (coins > i->coins.number ())
                        {
                            if (sequence + 1 == i->source.sequence)
                            {
                                inputs += coins - i->coins.number ();
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
    if (!result)
    {
        mu_coin::uint256_t outputs;
        for (auto i (block_a.outputs.begin ()), j (block_a.outputs.end ()); i != j && !result; ++i)
        {
            outputs += i->coins.number ();
        }
        if (outputs + block_a.fee () == inputs)
        {
            for (auto i (block_a.inputs.begin ()), j (block_a.inputs.end ()); i != j; ++i)
            {
                ledger.store.insert_block (i->source, block_a);
            }
            for (auto i (block_a.outputs.begin ()), j (block_a.outputs.end ()); i != j; ++i)
            {
                ledger.store.insert_send (i->address, block_a);
            }
        }
        else
        {
            result = true;
        }
    }
}

void mu_coin::ledger_processor::receive_block (mu_coin::receive_block const & block_a)
{
    result = block_a.validate (block_a.hash ());
    if (!result)
    {
        auto block (ledger.store.send (block_a.output.address, block_a.source));
        result = block == nullptr;
        if (!result)
        {
            auto entry (std::find_if (block->outputs.begin (), block->outputs.end (), [&block_a] (mu_coin::send_output const & output_a) {return output_a.address == block_a.output.address;}));
            assert (entry != block->outputs.end ());
            auto previous (ledger.previous (block_a.output.address));
            if (block_a.output.sequence > 0)
            {
                if (previous != nullptr)
                {
                    mu_coin::uint256_t coins;
                    uint16_t sequence;
                    result = previous->balance (block_a.output.address, coins, sequence);
                    if (!result)
                    {
                        if (block_a.coins == coins + entry->coins.number ())
                        {
                            ledger.store.clear (block_a.output.address, block_a.source);
                            ledger.store.insert_block (block_a.output, block_a);
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
            else
            {
                if (previous == nullptr)
                {
                    if (block_a.coins == entry->coins)
                    {
                        ledger.store.clear (block_a.output.address, block_a.source);
                        ledger.store.insert_block (block_a.output, block_a);
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
}

void mu_coin::ledger_processor::transaction_block (mu_coin::transaction_block const & block_a)
{
    result = false;
    mu_coin::uint256_t message (block_a.hash ());
    mu_coin::uint256_t previous;
    mu_coin::uint256_t next;
    for (auto i (block_a.entries.begin ()), j (block_a.entries.end ()); !result && i != j; ++i)
    {
        auto & address (i->id.address);
        auto result = i->validate (message);
        if (!result)
        {
            auto existing (ledger.store.latest (address));
            if (i->id.sequence > 0)
            {
                if (existing != nullptr)
                {
                    mu_coin::uint256_t coins;
                    uint16_t sequence;
                    result = existing->balance (address, coins, sequence);
                    if (!result)
                    {
                        result = sequence + 1 != i->id.sequence;
                        if (!result)
                        {
                            next += i->coins.number ();
                            previous += coins;
                        }
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
                    ledger.store.insert_block (i->id, block_a);
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

mu_coin::ledger_processor::ledger_processor (mu_coin::ledger & ledger_a) :
ledger (ledger_a)
{
}

bool mu_coin::send_block::balance (mu_coin::address const & address_a, mu_coin::uint256_t & coins_a, uint16_t & sequence_a)
{
    bool result (true);
    for (auto i (inputs.begin ()), j (inputs.end ()); i != j && result; ++i)
    {
        auto & entry (*i);
        if (entry.source.address == address_a)
        {
            coins_a = entry.coins.number ();
            sequence_a = entry.source.sequence;
            result = false;
        }
    }
    return result;
}

bool mu_coin::receive_block::balance (mu_coin::address const & address_a, mu_coin::uint256_t & coins_a, uint16_t & sequence_a)
{
    bool result;
    if (output.address == address_a)
    {
        coins_a = coins.number ();
        sequence_a = output.sequence;
        result = false;
    }
    else
    {
        result = true;
    }
    return result;
}

bool mu_coin::transaction_block::balance (mu_coin::address const & address_a, mu_coin::uint256_t & coins_a, uint16_t & sequence_a)
{
    auto previous_entry (std::find_if (entries.begin (), entries.end (), [&address_a] (mu_coin::entry const & entry_a) {return address_a == entry_a.id.address;}));
    bool result;
    if (previous_entry != entries.end ())
    {
        result = false;
        coins_a = previous_entry->coins.number ();
        sequence_a = previous_entry->id.sequence;
    }
    else
    {
        result = true;
    }
    return result;
}

mu_coin::send_block::send_block (send_block const & other_a) :
inputs (other_a.inputs),
outputs (other_a.outputs)
{
}

bool mu_coin::send_source::operator == (mu_coin::send_source const & other_a) const
{
    return address == other_a.address && source == other_a.source;
}

bool mu_coin::receive_block::validate (mu_coin::uint256_union const & message) const
{
    return validate_message (message, signature, output.address.point.key ());
}

bool mu_coin::transaction_block::operator == (mu_coin::block const & other_a) const
{
    auto other_l (dynamic_cast <mu_coin::transaction_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool mu_coin::send_block::operator == (mu_coin::block const & other_a) const
{
    auto other_l (dynamic_cast <mu_coin::send_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool mu_coin::receive_block::operator == (mu_coin::block const & other_a) const
{
    auto other_l (dynamic_cast <mu_coin::receive_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

std::unique_ptr <mu_coin::block> mu_coin::transaction_block::clone () const
{
    return std::unique_ptr <mu_coin::block> (new mu_coin::transaction_block (*this));
}

std::unique_ptr <mu_coin::block> mu_coin::send_block::clone () const
{
    return std::unique_ptr <mu_coin::block> (new mu_coin::send_block (*this));
}

std::unique_ptr <mu_coin::block> mu_coin::receive_block::clone () const
{
    return std::unique_ptr <mu_coin::block> (new mu_coin::receive_block (*this));
}

std::unique_ptr <mu_coin::block> mu_coin::deserialize_block (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::block_type type;
    stream_a.read (type);
    std::unique_ptr <mu_coin::block> result;
    switch (type)
    {
        case mu_coin::block_type::receive:
        {
            std::unique_ptr <mu_coin::receive_block> obj (new mu_coin::receive_block);
            auto error (obj->deserialize (stream_a));
            if (!error)
            {
                result = std::move (obj);
            }
            break;
        }
        case mu_coin::block_type::send:
        {
            std::unique_ptr <mu_coin::send_block> obj (new mu_coin::send_block);
            auto error (obj->deserialize (stream_a));
            if (!error)
            {
                result = std::move (obj);
            }
            break;
        }
        case mu_coin::block_type::transaction:
        {
            std::unique_ptr <mu_coin::transaction_block> obj (new mu_coin::transaction_block);
            auto error (obj->deserialize (stream_a));
            if (!error)
            {
                result = std::move (obj);
            }
            break;
        }
        default:
            assert (false);
            break;
    }
    return result;
}

void mu_coin::serialize_block (mu_coin::byte_write_stream & stream_a, mu_coin::block const & block_a)
{
    stream_a.write (block_a.type ());
    block_a.serialize (stream_a);
}

mu_coin::block_type mu_coin::transaction_block::type () const
{
    return mu_coin::block_type::transaction;
}

mu_coin::block_type mu_coin::send_block::type () const
{
    return mu_coin::block_type::send;
}

mu_coin::block_type mu_coin::receive_block::type () const
{
    return mu_coin::block_type::receive;
}

void mu_coin::cached_password_store::decrypt (mu_coin::uint256_union const & pin_hash, mu_coin::uint256_union & password_a)
{
    CryptoPP::AES::Decryption alg (pin_hash.bytes.data (), sizeof (pin_hash.bytes));
    CryptoPP::ECB_Mode_ExternalCipher::Decryption dec (alg);
    dec.ProcessData (password_a.bytes.data (), password.bytes.data (), sizeof (password.bytes));
}

void mu_coin::cached_password_store::encrypt (mu_coin::uint256_union const & pin_hash, mu_coin::uint256_union const & password_a)
{
    CryptoPP::AES::Encryption alg (pin_hash.bytes.data (), sizeof (pin_hash.bytes));
    CryptoPP::ECB_Mode_ExternalCipher::Encryption enc (alg);
    enc.ProcessData (password.bytes.data (), password_a.bytes.data (), sizeof (password_a.bytes));
}

mu_coin::cached_password_store::~cached_password_store ()
{
    clear ();
}

void mu_coin::cached_password_store::clear ()
{
    password.bytes.fill (0);
}

void mu_coin::uint256_union::encode (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (64) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint256_union::decode (std::string const & text)
{
    auto result (text.size () > 64);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::hex << std::noshowbase;
        mu_coin::uint256_t number_l;
        try
        {
            stream >> number_l;
            *this = number_l;
        }
        catch (std::runtime_error &)
        {
            result = true;
        }
    }
    return result;
}

void mu_coin::uint512_union::encode (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (128) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint512_union::decode (std::string const & text)
{
    auto result (text.size () > 128);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::hex << std::noshowbase;
        mu_coin::uint512_t number_l;
        try
        {
            stream >> number_l;
            *this = number_l;
        }
        catch (std::runtime_error &)
        {
            result = true;
        }
    }
    return result;
}

void mu_coin::point_encoding::encode (std::string & text)
{
    mu_coin::uint512_union address;
    std::copy (bytes.begin (), bytes.end (), address.bytes.end () - sizeof (bytes));
    address.encode (text);
    assert (text.size () == 128);
    text = text.substr (63, 65);
}

bool mu_coin::point_encoding::decode (std::string const & text)
{
    mu_coin::uint512_union address;
    bool result (address.decode (text));
    if (!result)
    {
        mu_coin::uint256_t number (address.number ());
        mu_coin::uint512_t type (address.number () >> 256);
        if (type == 2 || type == 3)
        {
            assign (type.convert_to <uint8_t> (), number);
        }
        else
        {
            result = true;
        }
    }
    return result;
}

void mu_coin::point_encoding::assign (uint8_t type_a, uint256_union const & point_a)
{
    assert (type_a == 2 || type_a == 3);
    bytes [0] = type_a;
    std::copy (point_a.bytes.begin (), point_a.bytes.end (), bytes.begin () + 1);
}

mu_coin::block_store_temp_t mu_coin::block_store_temp;

mu_coin::block_store::block_store (block_store_temp_t const &) :
handle (nullptr, 0)
{
    boost::filesystem::path temp (boost::filesystem::unique_path ());
    handle.open (nullptr, temp.native().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::latest (mu_coin::address const & address_a)
{
    std::unique_ptr <mu_coin::block> result;
    bool exists;
    uint16_t sequence;
    latest_sequence (address_a, sequence, exists);
    if (exists)
    {
        mu_coin::block_id block (address_a, sequence);
        dbt key (block);
        dbt value;
        int error (handle.get (nullptr, &key.data, &value.data, 0));
        result = value.block ();
    }
    return result;
}

void mu_coin::block_store::insert_block (mu_coin::block_id const & id_a, mu_coin::block const & block_a)
{
    dbt key (id_a);
    dbt data (block_a);
    int error (handle.put (nullptr, &key.data, &data.data, 0));
    dbt key2 (id_a.address);
    dbt data2 (id_a.sequence);
    int error2 (handle.put (nullptr, &key2.data, &data2.data, 0));
}

void mu_coin::block_store::latest_sequence (mu_coin::address const & address_a, uint16_t & sequence, bool & exists)
{
    dbt key (address_a);
    dbt value;
    int error (handle.get (nullptr, &key.data, &value.data, 0));
    if (value.data.get_size () == 2)
    {
        exists = true;
        mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t *> (value.data.get_data ()), reinterpret_cast <uint8_t *> (value.data.get_data ()) + value.data.get_size ());
        stream.read (sequence);
    }
    else
    {
        exists = false;
    }
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::block (mu_coin::block_id const & id_a)
{
    mu_coin::dbt key (id_a);
    mu_coin::dbt data;
    int error (handle.get (nullptr, &key.data, &data.data, 0));
    auto result (data.block ());
    return result;
}

std::unique_ptr <mu_coin::block> mu_coin::dbt::block()
{
    std::unique_ptr <mu_coin::block> result;
    if (data.get_size () > 0)
    {
        mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t *> (data.get_data ()), reinterpret_cast <uint8_t *> (data.get_data ()) + data.get_size ());
        result = mu_coin::deserialize_block (stream);
    }
    return result;
}

mu_coin::dbt::dbt (mu_coin::block const & block_a)
{
    mu_coin::byte_write_stream stream;
    mu_coin::serialize_block (stream, block_a);
    adopt (stream);
}

mu_coin::dbt::dbt (mu_coin::address const & address_a)
{
    mu_coin::byte_write_stream stream;
    address_a.serialize (stream);
    adopt (stream);
}

mu_coin::dbt::dbt (mu_coin::block_id const & id_a)
{
    mu_coin::byte_write_stream stream;
    id_a.serialize (stream);
    adopt (stream);
}

mu_coin::dbt::dbt (uint16_t sequence_a)
{
    mu_coin::byte_write_stream stream;
    stream.write (sequence_a);
    adopt (stream);
}

void mu_coin::dbt::adopt (mu_coin::byte_write_stream & stream_a)
{
    data.set_data (stream_a.data);
    data.set_size (stream_a.size);
    stream_a.data = nullptr;
}

std::unique_ptr <mu_coin::send_block> mu_coin::block_store::send (mu_coin::address const & address_a, mu_coin::block_id const & id_a)
{
    mu_coin::dbt key (address_a, id_a);
    mu_coin::dbt data;
    int error (handle.get (nullptr, &key.data, &data.data, 0));
    std::unique_ptr <mu_coin::block> block (data.block ());
    assert (block == nullptr || dynamic_cast <mu_coin::send_block *> (block.get ()) != nullptr);
    std::unique_ptr <mu_coin::send_block> result (static_cast <mu_coin::send_block *> (block.release ()));
    return result;
}

void mu_coin::block_store::insert_send (mu_coin::address const & address_a, mu_coin::send_block const & block_a)
{
    mu_coin::dbt key (address_a, block_a.inputs.front ().source);
    mu_coin::dbt data (block_a);
    int error (handle.put (0, &key.data, &data.data, 0));
}

void mu_coin::block_store::clear (mu_coin::address const & address_a, mu_coin::block_id const & id_a)
{
    mu_coin::dbt key (address_a, id_a);
    int error (handle.del (0, &key.data, 0));
}

mu_coin::dbt::dbt (mu_coin::address const & address_a, mu_coin::block_id const & id_a)
{
    mu_coin::byte_write_stream stream;
    stream.write (address_a.point.bytes);
    stream.write (id_a.address.point.bytes);
    stream.write (id_a.sequence);
    adopt (stream);
}