#include <mu_coin/mu_coin.hpp>

#include <cryptopp/sha.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <ed25519-donna/ed25519.h>
#include <cryptopp/osrng.h>

#include <unordered_set>
#include <memory>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <boost/interprocess/streams/vectorstream.hpp>

CryptoPP::AutoSeededRandomPool pool;

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

mu_coin::uint256_t mu_coin::uint256_union::number () const
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

void mu_coin::sign_message (mu_coin::private_key const & private_key, mu_coin::public_key const & public_key, mu_coin::uint256_union const & message, mu_coin::uint512_union & signature)
{
    ed25519_sign (message.bytes.data (), sizeof (message.bytes), private_key.bytes.data (), public_key.bytes.data (), signature.bytes.data ());
}

bool mu_coin::validate_message (mu_coin::public_key const & public_key, mu_coin::uint256_union const & message, mu_coin::uint512_union const & signature)
{
    auto result (0 != ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), public_key.bytes.data (), signature.bytes.data ()));
    return result;
}

class amount_visitor : public mu_coin::block_visitor
{
public:
    amount_visitor (mu_coin::block_store &);
    void compute (mu_coin::block_hash const &);
    void send_block (mu_coin::send_block const & block_a) override;
    void receive_block (mu_coin::receive_block const & block_a) override;
    mu_coin::block_store & store;
    mu_coin::uint256_t result;
};

class balance_visitor : public mu_coin::block_visitor
{
public:
    balance_visitor (mu_coin::block_store &);
    void compute (mu_coin::block_hash const &);
    void send_block (mu_coin::send_block const & block_a) override;
    void receive_block (mu_coin::receive_block const & block_a) override;
    mu_coin::block_store & store;
    mu_coin::uint256_t result;
};

balance_visitor::balance_visitor (mu_coin::block_store & store_a):
store (store_a),
result (0)
{
}

void balance_visitor::send_block (mu_coin::send_block const & block_a)
{
    result = block_a.hashables.balance.number ();
}

class account_visitor : public mu_coin::block_visitor
{
public:
    account_visitor (mu_coin::block_store & store_a) :
    store (store_a)
    {
    }
    void compute (mu_coin::block_hash const & hash_block)
    {
        auto block (store.block_get (hash_block));
        assert (block != nullptr);
        block->visit (*this);
    }
    void send_block (mu_coin::send_block const & block_a) override
    {
        account_visitor prev (store);
        prev.compute (block_a.hashables.previous);
        result = prev.result;
    }
    void receive_block (mu_coin::receive_block const & block_a) override
    {
        auto block (store.block_get (block_a.hashables.source));
        assert (block != nullptr);
        assert (dynamic_cast <mu_coin::send_block *> (block.get ()) != nullptr);
        auto send (static_cast <mu_coin::send_block *> (block.get ()));
        result = send->hashables.destination;
    }
    mu_coin::block_store & store;
    mu_coin::address result;
};

void balance_visitor::receive_block (mu_coin::receive_block const & block_a)
{
    account_visitor account (store);
    account.compute (block_a.hash ());
    mu_coin::uint256_t base (0);
    if (!(block_a.hashables.previous == account.result))
    {
        balance_visitor prev (store);
        prev.compute (block_a.hashables.previous);
        base = prev.result;
    }
    amount_visitor source (store);
    source.compute (block_a.hashables.source);
    result = base + source.result;
}

amount_visitor::amount_visitor (mu_coin::block_store & store_a) :
store (store_a)
{
}

void amount_visitor::send_block (mu_coin::send_block const & block_a)
{
    balance_visitor prev (store);
    prev.compute (block_a.hashables.previous);
    result = prev.result - block_a.hashables.balance.number ();
}

void amount_visitor::receive_block (mu_coin::receive_block const & block_a)
{
    balance_visitor source (store);
    source.compute (block_a.hashables.source);
    auto source_block (store.block_get (block_a.hashables.source));
    assert (source_block != nullptr);
    balance_visitor source_prev (store);
    source_prev.compute (source_block->previous ());
}

mu_coin::uint256_t mu_coin::ledger::balance (mu_coin::address const & address_a)
{
    mu_coin::uint256_t result (0);
    mu_coin::block_hash hash;
    auto none (store.latest_get (address_a, hash));
    if (!none)
    {
        balance_visitor visitor (store);
        visitor.compute (hash);
        result = visitor.result;
    }
    return result;
}

mu_coin::process_result mu_coin::ledger::process (mu_coin::block const & block_a)
{
    mu_coin::ledger_processor processor (*this);
    block_a.visit (processor);
    return processor.result;
}

mu_coin::keypair::keypair ()
{
    ed25519_randombytes_unsafe (prv.bytes.data (), sizeof (prv.bytes));
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

mu_coin::ledger::ledger (mu_coin::block_store & store_a) :
store (store_a)
{
}

bool mu_coin::uint256_union::operator == (mu_coin::uint256_union const & other_a) const
{
    return bytes == other_a.bytes;
}

bool mu_coin::uint512_union::operator == (mu_coin::uint512_union const & other_a) const
{
    return bytes == other_a.bytes;
}

size_t mu_coin::byte_read_stream::byte_read_stream::size ()
{
    return end - data;
}

mu_coin::byte_read_stream::byte_read_stream (uint8_t const * data_a, uint8_t const * end_a) :
data (data_a),
end (end_a)
{
}

mu_coin::byte_read_stream::byte_read_stream (uint8_t const * data_a, size_t const size_a) :
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

void mu_coin::uint256_union::serialize (mu_coin::byte_write_stream & stream_a) const
{
    stream_a.write (bytes);
}

bool mu_coin::uint256_union::deserialize (mu_coin::byte_read_stream & stream_a)
{
    auto & point_l (bytes);
    return stream_a.read (point_l);
}

mu_coin::uint256_union::uint256_union (mu_coin::private_key const & prv, uint256_union const & key, uint128_union const & iv)
{
    mu_coin::uint256_union exponent (prv);
    CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
    enc.ProcessData (bytes.data (), exponent.bytes.data (), sizeof (exponent.bytes));
}

mu_coin::private_key mu_coin::uint256_union::prv (mu_coin::secret_key const & key_a, uint128_union const & iv) const
{
    CryptoPP::AES::Decryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
    mu_coin::private_key result;
    dec.ProcessData (result.bytes.data (), bytes.data (), sizeof (bytes));
    return result;
}

mu_coin::uint256_union::uint256_union (std::string const & password_a)
{
    CryptoPP::SHA256 hash;
    hash.Update (reinterpret_cast <uint8_t const *> (password_a.c_str ()), password_a.size ());
    hash.Final (bytes.data ());
}

void mu_coin::send_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.send_block (*this);
}

void mu_coin::receive_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.receive_block (*this);
}

mu_coin::uint256_union mu_coin::send_block::hash () const
{
    return hashables.hash ();
}

mu_coin::uint256_union mu_coin::send_hashables::hash () const
{
    mu_coin::uint256_union result;
    CryptoPP::SHA256 hash;
    hash.Update (previous.bytes.data (), sizeof (previous.bytes));
    hash.Update (balance.bytes.data (), sizeof (balance.bytes));
    hash.Update (destination.bytes.data (), sizeof (destination.bytes));
    hash.Final (result.bytes.data ());
    return result;
}

void mu_coin::send_block::serialize (mu_coin::byte_write_stream & stream) const
{
    stream.write (signature.bytes);
    stream.write (hashables.previous.bytes);
    stream.write (hashables.balance.bytes);
    stream.write (hashables.destination.bytes);
}

bool mu_coin::send_block::deserialize (mu_coin::byte_read_stream & stream)
{
    auto result (false);
    result = stream.read (signature.bytes);
    if (!result)
    {
        result = stream.read (hashables.previous.bytes);
        if (!result)
        {
            result = stream.read (hashables.balance.bytes);
            if (!result)
            {
                result = stream.read (hashables.destination.bytes);
            }
        }
    }
    return result;
}

void mu_coin::receive_block::sign (mu_coin::private_key const & prv, mu_coin::public_key const & pub, mu_coin::uint256_union const & hash_a)
{
    sign_message (prv, pub, hash_a, signature);
}

bool mu_coin::receive_block::operator == (mu_coin::receive_block const & other_a) const
{
    auto result (signature == other_a.signature && hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source);
    return result;
}

bool mu_coin::receive_block::deserialize (mu_coin::byte_read_stream & stream_a)
{
    auto result (false);
    result = stream_a.read (signature.bytes);
    if (!result)
    {
        result = stream_a.read (hashables.previous.bytes);
        if (!result)
        {
            result = stream_a.read (hashables.source.bytes);
        }
    }
    return result;
}

void mu_coin::receive_block::serialize (mu_coin::byte_write_stream & stream_a) const
{
    stream_a.write (signature.bytes);
    stream_a.write (hashables.previous.bytes);
    stream_a.write (hashables.source.bytes);
}

mu_coin::uint256_union mu_coin::receive_block::hash () const
{
    return hashables.hash ();
}

mu_coin::uint256_union mu_coin::receive_hashables::hash () const
{
    CryptoPP::SHA256 hash;
    hash.Update (source.bytes.data (), sizeof (source.bytes));
    hash.Update (previous.bytes.data (), sizeof (previous.bytes));
    mu_coin::uint256_union result;
    hash.Final (result.bytes.data ());
    return result;
}

void mu_coin::ledger_processor::send_block (mu_coin::send_block const & block_a)
{
    mu_coin::uint256_union message (block_a.hash ());
    auto existing (ledger.store.block_get (message));
    result = existing != nullptr ? mu_coin::process_result::old : mu_coin::process_result::progress; // Have we seen this block before? (Harmless)
    if (result == mu_coin::process_result::progress)
    {
        auto previous (ledger.store.block_get (block_a.hashables.previous));
        result = previous != nullptr ? mu_coin::process_result::progress : mu_coin::process_result::gap; // Have we seen the previous block before? (Harmless)
        if (result == mu_coin::process_result::progress)
        {
            account_visitor account (ledger.store);
            account.compute (block_a.hashables.previous);
            result = validate_message (account.result, message, block_a.signature) ? mu_coin::process_result::bad_signature : mu_coin::process_result::progress; // Is this block signed correctly (Malformed)
            if (result == mu_coin::process_result::progress)
            {
                mu_coin::uint256_t coins (ledger.balance (account.result));
                result = coins > block_a.hashables.balance.number () ? mu_coin::process_result::progress : mu_coin::process_result::overspend; // Is this trying to spend more than they have (Malicious)
                if (result == mu_coin::process_result::progress)
                {
                    ledger.store.block_put (message, block_a);
                    ledger.store.latest_put (account.result, message);
                    ledger.store.pending_put (message);
                }
            }
        }
    }
}

void mu_coin::ledger_processor::receive_block (mu_coin::receive_block const & block_a)
{
    auto hash (block_a.hash ());
    auto existing (ledger.store.block_get (hash));
    result = existing != nullptr ? mu_coin::process_result::old : mu_coin::process_result::progress; // Have we seen this block already?  (Harmless)
    if (result == mu_coin::process_result::progress)
    {
        auto source_block (ledger.store.block_get (block_a.hashables.source));
        result = source_block == nullptr ? mu_coin::process_result::gap : mu_coin::process_result::progress; // Have we seen the source block? (Harmless)
        if (result == mu_coin::process_result::progress)
        {
            auto source_send (dynamic_cast <mu_coin::send_block *> (source_block.get ()));
            result = source_send == nullptr ? mu_coin::process_result::not_receive_from_send : mu_coin::process_result::progress; // Are we receiving from a send (Malformed)
            if (result == mu_coin::process_result::progress)
            {
                result = mu_coin::validate_message (source_send->hashables.destination, hash, block_a.signature) ? mu_coin::process_result::bad_signature : mu_coin::process_result::progress; // Is the signature valid (Malformed)
                if (result == mu_coin::process_result::progress)
                {
                    if (source_send->hashables.destination == block_a.hashables.previous)
                    {
                        // New address
                        mu_coin::block_hash latest;
                        result = ledger.store.latest_get (source_send->hashables.destination, latest) ? mu_coin::process_result::progress : mu_coin::process_result::fork; // Address is claiming this is their first block (Malicious)
                    }
                    else
                    {
                        // Old address
                        mu_coin::block_hash latest;
                        result = ledger.store.latest_get (source_send->hashables.destination, latest) ? mu_coin::process_result::gap : mu_coin::process_result::progress;
                        if (result == mu_coin::process_result::progress)
                        {
                            result = latest == block_a.hashables.previous ? mu_coin::process_result::progress : mu_coin::process_result::gap; // Block doesn't immediately follow latest block (Harmless)
                            if (result == mu_coin::process_result::progress)
                            {
                                
                            }
                            else
                            {
                                result = ledger.store.block_get (latest) ? mu_coin::process_result::fork : mu_coin::process_result::gap; // If we have the block but it's not the latest we have a signed fork (Malicious)
                            }
                        }
                    }
                    if (result == mu_coin::process_result::progress)
                    {
                        ledger.store.pending_del (source_send->hash ());
                        ledger.store.block_put (hash, block_a);
                        ledger.store.latest_put (source_send->hashables.destination, hash);
                    }
                }
            }
        }
    }
}

mu_coin::ledger_processor::ledger_processor (mu_coin::ledger & ledger_a) :
ledger (ledger_a),
result (mu_coin::process_result::progress)
{
}

mu_coin::send_block::send_block (send_block const & other_a) :
hashables (other_a.hashables),
signature (other_a.signature)
{
}

bool mu_coin::receive_block::validate (mu_coin::public_key const & key, mu_coin::uint256_t const & hash) const
{
    return validate_message (key, hash, signature);
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
    auto error (stream_a.read (type));
    std::unique_ptr <mu_coin::block> result;
    if (!error)
    {
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
            }
                break;
            case mu_coin::block_type::send:
            {
                std::unique_ptr <mu_coin::send_block> obj (new mu_coin::send_block);
                auto error (obj->deserialize (stream_a));
                if (!error)
                {
                    result = std::move (obj);
                }
            }
                break;
            default:
                break;
        }
    }
    return result;
}

void mu_coin::serialize_block (mu_coin::byte_write_stream & stream_a, mu_coin::block const & block_a)
{
    stream_a.write (block_a.type ());
    block_a.serialize (stream_a);
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

void mu_coin::uint256_union::encode_hex (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (64) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint256_union::decode_hex (std::string const & text)
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

void mu_coin::uint256_union::encode_dec (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::dec << std::noshowbase;
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint256_union::decode_dec (std::string const & text)
{
    auto result (text.size () > 78);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::dec << std::noshowbase;
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

void mu_coin::uint512_union::encode_hex (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (128) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint512_union::decode_hex (std::string const & text)
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

mu_coin::block_store_temp_t mu_coin::block_store_temp;

mu_coin::block_store::block_store (block_store_temp_t const &) :
block_store (boost::filesystem::unique_path ())
{
}

mu_coin::block_store::block_store (boost::filesystem::path const & path_a) :
addresses (nullptr, 0),
blocks (nullptr, 0),
pending (nullptr, 0)
{
    boost::filesystem::create_directories (path_a);
    addresses.open (nullptr, (path_a / "addresses.bin").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    blocks.open (nullptr, (path_a / "blocks.bin").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    pending.open (nullptr, (path_a / "pending.bin").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
}

void mu_coin::block_store::block_put (mu_coin::block_hash const & hash_a, mu_coin::block const & block_a)
{
    dbt key (hash_a);
    dbt data (block_a);
    int error (blocks.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::block_get (mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (hash_a);
    mu_coin::dbt data;
    int error (blocks.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    auto result (data.block ());
    return result;
}

void mu_coin::block_store::genesis_put (mu_coin::public_key const & key_a, uint256_union const & coins_a)
{
    mu_coin::send_block send1;
    send1.hashables.destination.clear ();
    send1.hashables.balance = coins_a;
    send1.hashables.previous.clear ();
    send1.signature.clear ();
    block_put (send1.hash (), send1);
    mu_coin::send_block send2;
    send2.hashables.destination = key_a;
    send2.hashables.balance.clear ();
    send2.hashables.previous = send1.hash ();
    send2.signature.clear ();
    block_put (send2.hash (), send2);
    mu_coin::receive_block receive;
    receive.hashables.previous = key_a;
    receive.hashables.source = send2.hash ();
    receive.signature.clear ();
    block_put (receive.hash (), receive);
    latest_put (key_a, receive.hash ());
}

bool mu_coin::block_store::latest_get (mu_coin::address const & address_a, mu_coin::block_hash & hash_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data;
    int error (addresses.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    bool result;
    if (data.data.get_size () == 0)
    {
        result = true;
    }
    else
    {
        mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t const *> (data.data.get_data ()), data.data.get_size ());
        stream.read (hash_a.bytes);
        result = false;
    }
    return result;
}

void mu_coin::block_store::latest_put (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data (hash_a);
    int error (addresses.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

void mu_coin::block_store::pending_put (mu_coin::identifier const & identifier_a)
{
    mu_coin::dbt key (identifier_a);
    mu_coin::dbt data;
    int error (pending.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

void mu_coin::block_store::pending_del (mu_coin::identifier const & identifier_a)
{
    mu_coin::dbt key (identifier_a);
    mu_coin::dbt data;
    int error (pending.del (nullptr, &key.data, 0));
    assert (error == 0);
}

bool mu_coin::block_store::pending_get (mu_coin::identifier const & identifier_a)
{
    mu_coin::dbt key (identifier_a);
    mu_coin::dbt data;
    int error (pending.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    bool result;
    if (error == DB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        result = false;
    }
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

mu_coin::dbt::dbt (mu_coin::uint256_union const & address_a)
{
    mu_coin::byte_write_stream stream;
    address_a.serialize (stream);
    adopt (stream);
}

void mu_coin::dbt::adopt (mu_coin::byte_write_stream & stream_a)
{
    data.set_data (stream_a.data);
    data.set_size (stream_a.size);
    stream_a.data = nullptr;
}

mu_coin::dbt::dbt (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::byte_write_stream stream;
    stream.write (address_a.bytes);
    stream.write (hash_a.bytes);
    adopt (stream);
}

mu_coin::network::network (boost::asio::io_service & service_a, uint16_t port, mu_coin::client & client_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4::any (), port)),
service (service_a),
client (client_a),
keepalive_req_count (0),
keepalive_ack_count (0),
publish_req_count (0),
publish_ack_count (0),
publish_nak_count (0),
confirm_req_count (0),
confirm_ack_count (0),
confirm_nak_count (0),
confirm_unk_count (0),
unknown_count (0),
on (true)
{
}

void mu_coin::network::receive ()
{
    socket.async_receive_from (boost::asio::buffer (buffer), remote, [this] (boost::system::error_code const & error, size_t size_a) {receive_action (error, size_a); });
}

void mu_coin::network::stop ()
{
    on = false;
    socket.close ();
}

void mu_coin::network::send_keepalive (boost::asio::ip::udp::endpoint const & endpoint_a)
{
    mu_coin::keepalive_req message;
    mu_coin::byte_write_stream stream;
    message.serialize (stream);
    auto data (stream.data);
    std::cerr << "Keepalive " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (endpoint_a.port ()) << std::endl;
    socket.async_send_to (boost::asio::buffer (stream.data, stream.size), endpoint_a, [data] (boost::system::error_code const &, size_t) {free (data);});
    stream.abandon ();
    client.peers.add_peer (endpoint_a);
}

void mu_coin::network::publish_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <mu_coin::block> block)
{
    mu_coin::publish_req message (std::move (block));
    mu_coin::byte_write_stream stream;
    message.serialize (stream);
    auto data (stream.data);
    std::cerr << "Publish " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (endpoint_a.port ()) << std::endl;
    socket.async_send_to (boost::asio::buffer (stream.data, stream.size), endpoint_a, [data] (boost::system::error_code const & ec, size_t size) {free (data);});
    stream.abandon ();
    client.peers.add_peer (endpoint_a);
}

void mu_coin::network::confirm_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <mu_coin::block> block)
{
    mu_coin::confirm_req message (std::move (block));
    mu_coin::byte_write_stream stream;
    message.serialize (stream);
    auto data (stream.data);
    std::cerr << "Confirm " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (endpoint_a.port ()) << std::endl;
    socket.async_send_to (boost::asio::buffer (stream.data, stream.size), endpoint_a, [data] (boost::system::error_code const & ec, size_t size) {free (data);});
    stream.abandon ();
    client.peers.add_peer (endpoint_a);
}

void mu_coin::network::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (size_a >= sizeof (mu_coin::message_type))
        {
            auto sender (remote);
            client.peers.add_peer (sender);
            mu_coin::byte_read_stream type_stream (buffer.data (), size_a);
            mu_coin::message_type type;
            type_stream.read (type);
            switch (type)
            {
                case mu_coin::message_type::keepalive_req:
                {
                    ++keepalive_req_count;
                    receive ();
                    mu_coin::keepalive_ack message;
                    mu_coin::byte_write_stream stream;
                    message.serialize (stream);
                    auto data (stream.data);
                    socket.async_send_to (boost::asio::buffer (data, stream.size), sender, [data] (boost::system::error_code const & error, size_t size_a) {free (data);});
                    stream.abandon ();
                    break;
                }
                case mu_coin::message_type::keepalive_ack:
                {
                    ++keepalive_ack_count;
                    receive ();
                    break;
                }
                case mu_coin::message_type::publish_req:
                {
                    ++publish_req_count;
                    auto incoming (new mu_coin::publish_req);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    if (!error)
                    {
                        std::cerr << "Publish req" << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                        auto result (client.processor.process_publish (std::unique_ptr <mu_coin::publish_req> (incoming), sender));
                        switch (result)
                        {
                            case mu_coin::process_result::progress:
                            case mu_coin::process_result::owned:
                            case mu_coin::process_result::old:
                            case mu_coin::process_result::gap:
                            {
                                // Either successfully updated the ledger or we see some gap in the information likely due to network loss
                                mu_coin::publish_ack outgoing;
                                mu_coin::byte_write_stream stream;
                                outgoing.serialize (stream);
                                auto data (stream.data);
                                socket.async_send_to (boost::asio::buffer (stream.data, stream.size), sender, [data] (boost::system::error_code const & error, size_t size_a) {free (data);});
                                stream.abandon ();
                                break;
                            }
                            case mu_coin::process_result::bad_signature:
                            case mu_coin::process_result::overspend:
                            case mu_coin::process_result::overreceive:
                            case mu_coin::process_result::not_receive_from_send:
                            {
                                // None of these affect the integrity of the ledger since they're all ignored
                                mu_coin::publish_err outgoing;
                                mu_coin::byte_write_stream stream;
                                outgoing.serialize (stream);
                                auto data (stream.data);
                                socket.async_send_to (boost::asio::buffer (stream.data, stream.size), sender, [data] (boost::system::error_code const & error, size_t size_a) {free (data);});
                                stream.abandon ();
                                break;
                            }
                            case mu_coin::process_result::fork:
                            {
                                // Forked spend that needs arbitration
                                assert (false);
                                break;
                            }
                            default:
                            {
                                assert (false);
                                break;
                            }
                        }
                    }
                    break;
                }
                case mu_coin::message_type::publish_ack:
                {
                    ++publish_ack_count;
                    auto incoming (new mu_coin::publish_ack);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    break;
                }
                case mu_coin::message_type::publish_err:
                {
                    ++publish_err_count;
                    auto incoming (new mu_coin::publish_err);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    break;
                }
                case mu_coin::message_type::publish_nak:
                {
                    ++publish_nak_count;
                    auto incoming (new mu_coin::publish_nak);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    break;
                }
                case mu_coin::message_type::confirm_req:
                {
                    ++confirm_req_count;
                    std::cerr << "Confirm req " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                    auto incoming (new mu_coin::confirm_req);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    if (!error)
                    {
                        auto result (client.ledger.process (*incoming->block));
                        switch (result)
                        {
                            case mu_coin::process_result::old:
                            case mu_coin::process_result::progress:
                            {
                                client.processor.process_confirmation (incoming->block->hash (), sender);
                                break;
                            }
                            default:
                            {
                                assert (false);
                            }
                        }
                    }
                    break;
                }
                case mu_coin::message_type::confirm_ack:
                {
                    ++confirm_ack_count;
                    std::cerr << "Confirm ack " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                    auto incoming (new mu_coin::confirm_ack);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    if (!error)
                    {
                        std::unique_lock <std::mutex> lock (mutex);
                        auto session (confirm_listeners.find (incoming->block));
                        if (session != confirm_listeners.end ())
                        {
                            lock.unlock ();
                            session->second (std::unique_ptr <mu_coin::message> {incoming}, sender);
                        }
                    }
                    break;
                }
                case mu_coin::message_type::confirm_nak:
                {
                    ++confirm_nak_count;
                    std::cerr << "Confirm nak " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                    auto incoming (new mu_coin::confirm_nak);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    if (!error)
                    {
                        std::unique_lock <std::mutex> lock (mutex);
                        auto session (confirm_listeners.find (incoming->block));
                        if (session != confirm_listeners.end ())
                        {
                            lock.release ();
                            session->second (std::unique_ptr <mu_coin::message> {incoming}, sender);
                        }
                    }
                    break;
                }
                case mu_coin::message_type::confirm_unk:
                {
                    ++confirm_unk_count;
                    auto incoming (new mu_coin::confirm_unk);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    break;
                }
                default:
                {
                    ++unknown_count;
                    receive ();
                    break;
                }
            }
        }
    }
}

mu_coin::publish_req::publish_req (std::unique_ptr <mu_coin::block> block_a) :
block (std::move (block_a))
{
}

bool mu_coin::publish_req::deserialize (mu_coin::byte_read_stream & stream)
{
    auto result (false);
    mu_coin::message_type type;
    result = stream.read (type);
    assert (!result);
    block = mu_coin::deserialize_block (stream);
    result = block == nullptr;
    return result;
}

void mu_coin::publish_req::serialize (mu_coin::byte_write_stream & stream)
{
    stream.write (mu_coin::message_type::publish_req);
    mu_coin::serialize_block (stream, *block);
}

mu_coin::wallet_temp_t mu_coin::wallet_temp;


mu_coin::dbt::dbt (mu_coin::private_key const & prv, mu_coin::secret_key const & key, mu_coin::uint128_union const & iv)
{
    mu_coin::uint256_union encrypted (prv, key, iv);
    mu_coin::byte_write_stream stream;
    stream.write (encrypted.bytes);
    adopt (stream);
}

mu_coin::wallet::wallet (mu_coin::uint256_union const & password_a, boost::filesystem::path const & path_a) :
password (password_a),
handle (nullptr, 0)
{
    handle.open (nullptr, path_a.native().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
}

mu_coin::wallet::wallet (mu_coin::uint256_union const & password_a, mu_coin::wallet_temp_t const &) :
wallet (password_a, boost::filesystem::unique_path ())
{
}

void mu_coin::wallet::insert (mu_coin::public_key const & pub, mu_coin::private_key const & prv, mu_coin::uint256_union const & key_a)
{
    dbt key (pub);
    dbt value (prv, key_a, pub.owords [0]);
    auto error (handle.put (0, &key.data, &value.data, 0));
    assert (error == 0);
}

void mu_coin::wallet::insert (mu_coin::private_key const & prv, mu_coin::uint256_union const & key)
{
    mu_coin::public_key pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    insert (pub, prv, key);
}

bool mu_coin::wallet::fetch (mu_coin::public_key const & pub, mu_coin::secret_key const & key_a, mu_coin::private_key & prv)
{
    dbt key (pub);
    dbt value;
    auto result (false);
    auto error (handle.get (0, &key.data, &value.data, 0));
    if (error == 0)
    {
        value.key (key_a, pub.owords [0], prv);
        if (!result)
        {
            mu_coin::public_key compare;
            ed25519_publickey (prv.bytes.data (), compare.bytes.data ());
            if (!(pub == compare))
            {
                result = true;
            }
        }
    }
    else
    {
        result = true;
    }
    return result;
}

void mu_coin::dbt::key (mu_coin::uint256_union const & key_a, mu_coin::uint128_union const & iv, mu_coin::private_key & prv)
{
    mu_coin::uint256_union encrypted;
    mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t *> (data.get_data ()), data.get_size ());
    auto result (stream.read (encrypted.bytes));
    assert (!result);
    prv = encrypted.prv (key_a, iv);
}

mu_coin::key_iterator::key_iterator (Dbc * cursor_a) :
cursor (cursor_a)
{
}

void mu_coin::key_iterator::clear ()
{
    current.first.clear ();
    current.second.clear ();
    cursor->close ();
    cursor = nullptr;
}

mu_coin::key_iterator & mu_coin::key_iterator::operator ++ ()
{
    auto result (cursor->get (&key.data, &data.data, DB_NEXT));
    if (result == DB_NOTFOUND)
    {
        clear ();
    }
    else
    {
        current.first = key.uint256 ();
        current.second = data.uint256 ();
    }
    return *this;
}

mu_coin::key_entry & mu_coin::key_iterator::operator -> ()
{
    return current;
}

mu_coin::key_iterator mu_coin::wallet::begin ()
{
    Dbc * cursor;
    handle.cursor (0, &cursor, 0);
    mu_coin::key_iterator result (cursor);
    ++result;
    return result;
}

mu_coin::key_iterator mu_coin::wallet::find (mu_coin::uint256_union const & key)
{
    Dbc * cursor;
    handle.cursor (0, &cursor, 0);
    mu_coin::key_iterator result (cursor);
    result.key = key;
    auto exists (cursor->get (&result.key.data, &result.data.data, DB_SET));
    if (exists == DB_NOTFOUND)
    {
        result.clear ();
    }
    else
    {
        result.current.first = result.key.uint256 ();
        result.current.second = result.data.uint256 ();
    }
    return result;
}

mu_coin::key_iterator mu_coin::wallet::end ()
{
    return mu_coin::key_iterator (nullptr);
}

bool mu_coin::key_iterator::operator == (mu_coin::key_iterator const & other_a) const
{
    return cursor == other_a.cursor;
}

bool mu_coin::key_iterator::operator != (mu_coin::key_iterator const & other_a) const
{
    return !(*this == other_a);
}

bool mu_coin::wallet::generate_send (mu_coin::ledger & ledger_a, mu_coin::public_key const & destination, mu_coin::uint256_t const & coins, mu_coin::uint256_union const & key, std::vector <std::unique_ptr <mu_coin::send_block>> & blocks)
{
    bool result (false);
    mu_coin::uint256_t remaining (coins);
    for (auto i (begin ()), j (end ()); i != j && !result && !remaining.is_zero (); ++i)
    {
        auto account (i->first);
        auto balance (ledger_a.balance (account));
        if (!balance.is_zero ())
        {
            mu_coin::block_hash latest;
            assert (!ledger_a.store.latest_get (account, latest));
            auto amount (std::min (remaining, balance));
            remaining -= amount;
            std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
            block->hashables.destination = destination;
            block->hashables.previous = latest;
            block->hashables.balance = balance - amount;
            mu_coin::private_key prv;
            result = fetch (account, key, prv);
            assert (!result);
            sign_message (prv, account, block->hash (), block->signature);
            prv.clear ();
            blocks.push_back (std::move (block));
        }
    }
    return result;
}

void mu_coin::byte_write_stream::abandon ()
{
    data = nullptr;
}

mu_coin::uint256_union mu_coin::uint256_union::operator ^ (mu_coin::uint256_union const & other_a) const
{
    mu_coin::uint256_union result;
    auto k (other_a.qwords.begin ());
    auto l (result.qwords.begin ());
    for (auto i (qwords.begin ()), j (qwords.end ()); i != j; ++i, ++k, ++l)
    {
        *l = *i ^ *k;
    }
    return result;
}

mu_coin::uint256_union::uint256_union (uint64_t value)
{
    *this = mu_coin::uint256_t (value);
}

mu_coin::uint256_union mu_coin::dbt::uint256 () const
{
    assert (data.get_size () == 32);
    mu_coin::uint256_union result;
    mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t const *> (data.get_data ()), data.get_size ());
    stream.read (result);
    return result;
}

mu_coin::dbt::dbt (bool y_component)
{
    uint8_t y_byte (y_component);
    mu_coin::byte_write_stream stream;
    stream.write (y_byte);
    adopt (stream);
}

void mu_coin::processor_service::run ()
{
    std::unique_lock <std::mutex> lock (mutex);
    while (!done)
    {
        if (!operations.empty ())
        {
            auto & operation (operations.top ());
            if (operation.wakeup < std::chrono::system_clock::now ())
            {
                operations.pop ();
                lock.unlock ();
                operation.function ();
                lock.lock ();
            }
            else
            {
                condition.wait_until (lock, operation.wakeup);
            }
        }
        else
        {
            condition.wait (lock);
        }
    }
}

void mu_coin::processor_service::add (std::chrono::system_clock::time_point const & wakeup_a, std::function <void ()> const & operation)
{
    std::lock_guard <std::mutex> lock (mutex);
    operations.push (mu_coin::operation ({wakeup_a, operation}));
    condition.notify_all ();
}

mu_coin::processor_service::processor_service () :
done (false)
{
}

void mu_coin::processor_service::stop ()
{
    std::lock_guard <std::mutex> lock (mutex);
    done = true;
    condition.notify_all ();
}

mu_coin::processor::processor (mu_coin::processor_service & service_a, mu_coin::client & client_a) :
service (service_a),
client (client_a)
{
}

bool mu_coin::operation::operator < (mu_coin::operation const & other_a) const
{
    return wakeup < other_a.wakeup;
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, boost::filesystem::path const & wallet_path_a, boost::filesystem::path const & block_store_path_a, mu_coin::processor_service & processor_a) :
store (block_store_path_a),
ledger (store),
wallet (0, wallet_path_a),
network (*service_a, port_a, *this),
rpc (service_a, pool_a, command_port_a, *this),
processor (processor_a, *this)
{
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, mu_coin::processor_service & processor_a) :
client (service_a, pool_a, port_a, command_port_a, boost::filesystem::unique_path (), boost::filesystem::unique_path (), processor_a)
{
}

class publish_visitor : public mu_coin::block_visitor
{
public:
    publish_visitor (mu_coin::client & client_a, std::unique_ptr <mu_coin::publish_req> incoming_a, mu_coin::endpoint const & sender_a) :
    client (client_a),
    incoming (std::move (incoming_a)),
    sender (sender_a),
    result (mu_coin::process_result::progress)
    {
    }
    void send_block (mu_coin::send_block const & block_a)
    {
        result = client.ledger.process (block_a);
        if (result == mu_coin::process_result::progress)
        {
            if (client.wallet.find (block_a.hashables.destination) != client.wallet.end ())
            {
                result = mu_coin::process_result::owned;
                client.processor.process_receivable (std::move (incoming), mu_coin::endpoint {});
            }
        }
    }
    void receive_block (mu_coin::receive_block const & block_a)
    {
        result = client.ledger.process (block_a);
    }
    mu_coin::client & client;
    std::unique_ptr <mu_coin::publish_req> incoming;
    mu_coin::endpoint sender;
    mu_coin::process_result result;
};

class receivable_message_processor : public mu_coin::message_visitor
{
public:
    receivable_message_processor (mu_coin::receivable_processor & processor_a, mu_coin::endpoint const & sender_a) :
    processor (processor_a),
    sender (sender_a)
    {
    }
    void keepalive_req (mu_coin::keepalive_req const &)
    {
        assert (false);
    }
    void keepalive_ack (mu_coin::keepalive_ack const &)
    {
        assert (false);
    }
    void publish_req (mu_coin::publish_req const &)
    {
        assert (false);
    }
    void publish_ack (mu_coin::publish_ack const &)
    {
        assert (false);
    }
    void publish_err (mu_coin::publish_err const &)
    {
        assert (false);
    }
    void publish_nak (mu_coin::publish_nak const &)
    {
        assert (false);
    }
    void confirm_req (mu_coin::confirm_req const &)
    {
        assert (false);
    }
    void confirm_ack (mu_coin::confirm_ack const &);
    void confirm_unk (mu_coin::confirm_unk const &);
    void confirm_nak (mu_coin::confirm_nak const &);
    void process_authorizations (mu_coin::block_hash const &, std::vector <mu_coin::authorization> const &);
    mu_coin::receivable_processor & processor;
    mu_coin::endpoint sender;
};

mu_coin::receivable_processor::receivable_processor (std::unique_ptr <mu_coin::publish_req> incoming_a, mu_coin::endpoint const & sender_a, mu_coin::client & client_a) :
threshold (client_a.ledger.supply () / 2),
incoming (std::move (incoming_a)),
sender (sender_a),
client (client_a),
complete (false)
{
}

void mu_coin::receivable_processor::run ()
{
    mu_coin::uint256_t acknowledged_l;
    for (auto i (client.wallet.begin ()), j (client.wallet.end ()); i != j; ++i)
    {
        acknowledged_l += client.ledger.balance (i->first);
    }
    process_acknowledged (acknowledged_l);
    if (!complete)
    {
        auto this_l (shared_from_this ());
        client.network.add_confirm_listener (incoming->block->hash (), [this_l] (std::unique_ptr <mu_coin::message> message_a, mu_coin::endpoint const & endpoint_a) {this_l->confirm_ack (std::move (message_a), endpoint_a);});
        auto list (client.peers.list ());
        for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
        {
            if (*i != sender)
            {
                client.network.confirm_block (*i, incoming->block->clone ());
            }
        }
    }
    else
    {
        // Didn't need anyone else to authorize?  Wow, you're rich.
    }
}

void mu_coin::receivable_processor::confirm_ack (std::unique_ptr <mu_coin::message> message, mu_coin::endpoint const & sender)
{
    receivable_message_processor processor_l (*this, sender);
    message->visit (processor_l);
}

void mu_coin::receivable_processor::advance_timeout ()
{
    auto this_l (shared_from_this ());
    timeout = std::chrono::system_clock::now () + std::chrono::seconds (5);
    client.processor.service.add (timeout, [this_l] () {this_l->timeout_action ();});
}

void mu_coin::receivable_processor::timeout_action ()
{
    std::lock_guard <std::mutex> lock (mutex);
    if (timeout < std::chrono::system_clock::now ())
    {
        
    }
    else
    {
        // Timeout signals may be invalid if we've received action since they were queued
    }
}

void mu_coin::receivable_processor::process_acknowledged (mu_coin::uint256_t const & acknowledged_a)
{
    std::unique_lock <std::mutex> lock (mutex);
    if (!complete)
    {
        acknowledged += acknowledged_a;
        if (acknowledged > threshold && nacked.is_zero ())
        {
            complete = true;
            lock.release ();
            assert (dynamic_cast <mu_coin::send_block *> (incoming->block.get ()) != nullptr);
            auto & send (static_cast <mu_coin::send_block &> (*incoming->block.get ()));
            auto hash (send.hash ());
            mu_coin::private_key prv;
            if (!client.wallet.fetch (send.hashables.destination, client.wallet.password, prv))
            {
                if (!client.ledger.store.pending_get (send.hash ()))
                {
                    mu_coin::block_hash previous;
                    auto new_address (client.ledger.store.latest_get (send.hashables.destination, previous));
                    if (new_address)
                    {
                        previous = send.hashables.destination;
                    }
                    balance_visitor visitor (client.ledger.store);
                    visitor.compute (send.hashables.previous);
                    auto receive (new mu_coin::receive_block);
                    receive->hashables.previous = previous;
                    receive->hashables.source = hash;
                    mu_coin::sign_message (prv, send.hashables.destination, receive->hash (), receive->signature);
                    prv.bytes.fill (0);
                    client.processor.publish (std::unique_ptr <mu_coin::block> (receive), mu_coin::endpoint {});
                }
                else
                {
                    // Ledger doesn't have this marked as available to receive anymore
                }
            }
            else
            {
                // Wallet doesn't contain key for this destination or couldn't decrypt
            }
        }
        else
        {
            advance_timeout ();
        }
    }
}

void receivable_message_processor::process_authorizations (mu_coin::block_hash const & block, std::vector <mu_coin::authorization> const & authorizations)
{
    mu_coin::uint256_t acknowledged;
    for (auto i (authorizations.begin ()), j (authorizations.end ()); i != j; ++i)
    {
        if (!mu_coin::validate_message (i->address, block, i->signature))
        {
            auto balance (processor.client.ledger.balance (i->address));
            acknowledged += balance;
        }
        else
        {
            // Signature didn't match.
        }
    }
    std::string ack_string (acknowledged.convert_to <std::string> ());
    processor.process_acknowledged (acknowledged);
}

void receivable_message_processor::confirm_ack (mu_coin::confirm_ack const & message)
{
    process_authorizations (message.block, message.authorizations);
}

void receivable_message_processor::confirm_unk (mu_coin::confirm_unk const & message)
{
}

void receivable_message_processor::confirm_nak (mu_coin::confirm_nak const & message)
{
    auto block (message.winner->hash ());
    for (auto i (message.authorizations.begin ()), j (message.authorizations.end ()); i != j; ++i)
    {
        if (!mu_coin::validate_message (i->address, block, i->signature))
        {
            auto balance (processor.client.ledger.balance (i->address));
            processor.nacked += balance;
        }
        else
        {
            // Signature didn't match.
        }
    }
}

void mu_coin::processor::process_receivable (std::unique_ptr <mu_coin::publish_req> incoming, mu_coin::endpoint const & sender_a)
{
    auto processor (std::make_shared <receivable_processor> (std::move (incoming), sender_a, client));
    processor->run ();
}

void mu_coin::processor::publish (std::unique_ptr <mu_coin::block> block_a, mu_coin::endpoint const & sender_a)
{
    auto result (process_publish (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::move (block_a)}}, sender_a));
    switch (result)
    {
        case mu_coin::process_result::progress:
        case mu_coin::process_result::owned:
        {
            break;
        }
        case mu_coin::process_result::old:
        case mu_coin::process_result::gap:
        case mu_coin::process_result::bad_signature:
        case mu_coin::process_result::overspend:
        case mu_coin::process_result::overreceive:
        case mu_coin::process_result::not_receive_from_send:
        case mu_coin::process_result::fork:
        default:
        {
            assert (!result);
            break;
        }
    }
}

mu_coin::process_result mu_coin::processor::process_publish (std::unique_ptr <mu_coin::publish_req> incoming, mu_coin::endpoint const & sender_a)
{
    auto block (incoming->block->clone ());
    publish_visitor visitor (client, std::move (incoming), sender_a);
    visitor.incoming->block->visit (visitor);
    if (visitor.result == mu_coin::process_result::progress)
    {
        auto list (client.peers.list ());
        for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
        {
            if (*i != sender_a)
            {
                client.network.publish_block (*i, block->clone ());
            }
        }
    }
    return visitor.result;
}

void mu_coin::peer_container::add_peer (boost::asio::ip::udp::endpoint const & endpoint_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    peers.insert (endpoint_a);
}

std::vector <boost::asio::ip::udp::endpoint> mu_coin::peer_container::list ()
{
    std::vector <boost::asio::ip::udp::endpoint> result;
    std::lock_guard <std::mutex> lock (mutex);
    result.reserve (peers.size ());
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        result.push_back (*i);
    }
    return result;
}

void mu_coin::network::add_confirm_listener (mu_coin::block_hash const & block_a, session const & function_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    confirm_listeners [block_a] = function_a;
}

void mu_coin::network::remove_confirm_listener (mu_coin::block_hash const & block_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    confirm_listeners.erase (block_a);
}

void mu_coin::keepalive_req::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.keepalive_req (*this);
}

void mu_coin::keepalive_ack::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.keepalive_ack (*this);
}

void mu_coin::publish_req::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.publish_req (*this);
}

void mu_coin::publish_ack::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.publish_ack (*this);
}

void mu_coin::publish_nak::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.publish_nak (*this);
}

void mu_coin::publish_ack::serialize (mu_coin::byte_write_stream & stream)
{
    stream.write (mu_coin::message_type::publish_ack);
    stream.write (block);
}

bool mu_coin::publish_ack::deserialize (mu_coin::byte_read_stream & stream)
{
    mu_coin::message_type type;
    auto result (stream.read (type));
    assert (type == mu_coin::message_type::publish_ack);
    result = stream.read (block);
    return result;
}

void mu_coin::keepalive_ack::serialize (mu_coin::byte_write_stream & stream)
{
    stream.write (mu_coin::message_type::keepalive_ack);
}

bool mu_coin::keepalive_ack::deserialize (mu_coin::byte_read_stream & stream)
{
    mu_coin::message_type type;
    auto result (stream.read (type));
    assert (type == mu_coin::message_type::keepalive_ack);
    return result;
}

void mu_coin::keepalive_req::serialize (mu_coin::byte_write_stream & stream)
{
    stream.write (mu_coin::message_type::keepalive_req);
}

bool mu_coin::keepalive_req::deserialize (mu_coin::byte_read_stream & stream)
{
    mu_coin::message_type type;
    auto result (stream.read (type));
    assert (type == mu_coin::message_type::keepalive_req);
    return result;
}

mu_coin::uint256_t mu_coin::ledger::supply ()
{
    return std::numeric_limits <mu_coin::uint256_t>::max ();
}

size_t mu_coin::processor_service::size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return operations.size ();
}

size_t mu_coin::network::publish_listener_size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return confirm_listeners.size ();
}

mu_coin::publish_ack::publish_ack (mu_coin::block_hash const & block_a) :
block (block_a)
{
}

bool mu_coin::publish_ack::operator == (mu_coin::publish_ack const & other_a) const
{
    return block == other_a.block;
}

bool mu_coin::authorization::operator == (mu_coin::authorization const & other_a) const
{
    return address == other_a.address && signature == other_a.signature;
}

mu_coin::account_iterator::account_iterator (Dbc * cursor_a) :
cursor (cursor_a)
{
}

mu_coin::account_iterator & mu_coin::account_iterator::operator ++ ()
{
    auto result (cursor->get (&key.data, &data.data, DB_NEXT));
    if (result == DB_NOTFOUND)
    {
        cursor->close ();
        cursor = nullptr;
        current.first.clear ();
        current.second.clear ();
    }
    else
    {
        current.first = key.uint256 ();
        current.second = data.uint256 ();
    }
    return *this;
}

mu_coin::account_entry & mu_coin::account_iterator::operator -> ()
{
    return current;
}

bool mu_coin::account_iterator::operator == (mu_coin::account_iterator const & other_a) const
{
    return cursor == other_a.cursor;
}

bool mu_coin::account_iterator::operator != (mu_coin::account_iterator const & other_a) const
{
    return !(*this == other_a);
}

mu_coin::block_iterator::block_iterator (Dbc * cursor_a) :
cursor (cursor_a)
{
}

mu_coin::block_iterator & mu_coin::block_iterator::operator ++ ()
{
    auto result (cursor->get (&key.data, &data.data, DB_NEXT));
    if (result == DB_NOTFOUND)
    {
        cursor->close ();
        cursor = nullptr;
        current.first.clear ();
        current.second.release ();
    }
    else
    {
        current.first = key.uint256 ();
        current.second = data.block ();
    }
    return *this;
}

mu_coin::block_entry & mu_coin::block_iterator::operator -> ()
{
    return current;
}

bool mu_coin::block_iterator::operator == (mu_coin::block_iterator const & other_a) const
{
    return cursor == other_a.cursor;
}

bool mu_coin::block_iterator::operator != (mu_coin::block_iterator const & other_a) const
{
    return !(*this == other_a);
}

mu_coin::block_iterator mu_coin::block_store::blocks_begin ()
{
    Dbc * cursor;
    blocks.cursor (0, &cursor, 0);
    mu_coin::block_iterator result (cursor);
    ++result;
    return result;
}

mu_coin::block_iterator mu_coin::block_store::blocks_end ()
{
    mu_coin::block_iterator result (nullptr);
    return result;
}

mu_coin::account_iterator mu_coin::block_store::latest_begin ()
{
    Dbc * cursor;
    addresses.cursor (0, &cursor, 0);
    mu_coin::account_iterator result (cursor);
    ++result;
    return result;
}

mu_coin::account_iterator mu_coin::block_store::latest_end ()
{
    mu_coin::account_iterator result (nullptr);
    return result;
}

mu_coin::block_entry * mu_coin::block_entry::operator -> ()
{
    return this;
}

mu_coin::account_entry * mu_coin::account_entry::operator -> ()
{
    return this;
}

bool mu_coin::send_block::operator == (mu_coin::send_block const & other_a) const
{
    auto result (signature == other_a.signature && hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance);
    return result;
}

mu_coin::block_hash mu_coin::send_block::previous () const
{
    return hashables.previous;
}

mu_coin::block_hash mu_coin::receive_block::previous () const
{
    return hashables.previous;
}

void amount_visitor::compute (mu_coin::block_hash const & block_hash)
{
    auto block (store.block_get (block_hash));
    assert (block != nullptr);
    block->visit (*this);
}

void balance_visitor::compute (mu_coin::block_hash const & block_hash)
{
    auto block (store.block_get (block_hash));
    assert (block != nullptr);
    block->visit (*this);
}

bool mu_coin::client::send (mu_coin::public_key const & address, mu_coin::uint256_t const & coins, mu_coin::uint256_union const & password)
{
    std::vector <std::unique_ptr <mu_coin::send_block>> blocks;
    auto result (wallet.generate_send (ledger, address, coins, password, blocks));
    if (!result)
    {
        for (auto i (blocks.begin ()), j (blocks.end ()); i != j; ++i)
        {
            processor.publish (std::move (*i), mu_coin::endpoint {});
        }
    }
    return result;
}

mu_coin::system::system (size_t threads_a, uint16_t port_a, uint16_t command_port_a, size_t count_a) :
service (new boost::asio::io_service),
pool (new boost::network::utils::thread_pool (threads_a))
{
    clients.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        clients.push_back (std::unique_ptr <mu_coin::client> (new mu_coin::client (service, pool, port_a + i, command_port_a + i, processor)));
        clients.back ()->network.receive ();
    }
    for (auto i (clients.begin ()), j (clients.end ()); i != j; ++i)
    {
        for (auto k (clients.begin ()), l (clients.end ()); k != l; ++k)
        {
            if (*i != *k)
            {
                (*i)->peers.add_peer (mu_coin::endpoint {boost::asio::ip::address_v4::loopback (), (*k)->network.socket.local_endpoint ().port ()});
            }
        }
    }
    for (auto i (clients.begin ()), j (clients.end ()); i != j; ++i)
    {
        (*i)->rpc.listen ();
    }
}

mu_coin::endpoint mu_coin::system::endpoint (size_t index_a)
{
    return mu_coin::endpoint (boost::asio::ip::address_v4::loopback (), clients [index_a]->network.socket.local_endpoint ().port ());
}

void mu_coin::processor::process_confirmation (mu_coin::block_hash const & hash, mu_coin::endpoint const & sender)
{
    mu_coin::confirm_ack outgoing {hash};
    for (auto i (client.wallet.begin ()), j (client.wallet.end ()); i != j; ++i)
    {
        auto prv (i->second.prv (client.wallet.password, i->first.owords [0]));
        outgoing.authorizations.push_back (mu_coin::authorization {});
        outgoing.authorizations.back ().address = i->first;
        mu_coin::sign_message (prv, i->first, hash, outgoing.authorizations.back ().signature);
        assert (!mu_coin::validate_message(i->first, hash, outgoing.authorizations.back ().signature));
    }
    mu_coin::byte_write_stream stream;
    outgoing.serialize (stream);
    auto data (stream.data);
    client.network.socket.async_send_to (boost::asio::buffer (stream.data, stream.size), sender, [data] (boost::system::error_code const & error, size_t size_a) {free (data);});
    stream.abandon ();
}

mu_coin::key_entry * mu_coin::key_entry::operator -> ()
{
    return this;
}

void mu_coin::system::genesis (mu_coin::public_key const & address, mu_coin::uint256_t const & amount)
{
    for (auto i (clients.begin ()), j (clients.end ()); i != j; ++i)
    {
        (*i)->store.genesis_put (address, amount);
    }
}

bool mu_coin::confirm_ack::deserialize (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::message_type type;
    auto result (stream_a.read (type));
    assert (type == mu_coin::message_type::confirm_ack);
    if (!result)
    {
        result = stream_a.read (block);
        if (!result)
        {
            uint8_t authorization_count;
            result = stream_a.read (authorization_count);
            if (!result)
            {
                authorizations.reserve (authorization_count);
                for (auto i (0); !result && i < authorization_count; ++i)
                {
                    authorizations.push_back (mu_coin::authorization {});
                    result = stream_a.read (authorizations.back ());
                }
            }
        }
    }
    return result;
}

void mu_coin::confirm_ack::serialize (mu_coin::byte_write_stream & stream_a)
{
    stream_a.write (mu_coin::message_type::confirm_ack);
    stream_a.write (block);
    assert (authorizations.size () <= std::numeric_limits <uint8_t>::max ());
    uint8_t authorization_count (authorizations.size ());
    stream_a.write (authorization_count);
    for (auto & i: authorizations)
    {
        stream_a.write (i);
    }
}

mu_coin::confirm_ack::confirm_ack (mu_coin::uint256_union const & block_a) :
block (block_a)
{
}

void mu_coin::publish_nak::serialize (mu_coin::byte_write_stream & stream_a)
{
    assert (conflict != nullptr);
    stream_a.write (mu_coin::message_type::publish_nak);
    stream_a.write (block);
    mu_coin::serialize_block (stream_a, *conflict);
}

bool mu_coin::confirm_ack::operator == (mu_coin::confirm_ack const & other_a) const
{
    auto result (block == other_a.block && authorizations == other_a.authorizations);
    return result;
}

void mu_coin::confirm_ack::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.confirm_ack (*this);
}

bool mu_coin::confirm_nak::deserialize (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::message_type type;
    stream_a.read (type);
    assert (type == mu_coin::message_type::confirm_nak);
    auto result (stream_a.read (block));
    if (!result)
    {
        winner = mu_coin::deserialize_block (stream_a);
        result = winner == nullptr;
        if (!result)
        {
            loser = mu_coin::deserialize_block (stream_a);
            result = loser == nullptr;
        }
    }
    return result;
}

bool mu_coin::confirm_req::deserialize (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::message_type type;
    stream_a.read (type);
    assert (type == mu_coin::message_type::confirm_req);
    block = mu_coin::deserialize_block (stream_a);
    auto result (block == nullptr);
    return result;
}

bool mu_coin::confirm_unk::deserialize (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::message_type type;
    stream_a.read (type);
    assert (type == mu_coin::message_type::confirm_unk);
    auto result (stream_a.read (block));
    return result;
}

bool mu_coin::publish_nak::deserialize (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::message_type type;
    stream_a.read (type);
    assert (type == mu_coin::message_type::publish_nak);
    auto result (stream_a.read (block));
    if (!result)
    {
        conflict = mu_coin::deserialize_block (stream_a);
        result = conflict == nullptr;
    }
    return result;
}

void mu_coin::confirm_nak::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.confirm_nak (*this);
}

void mu_coin::confirm_req::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.confirm_req (*this);
}

void mu_coin::confirm_unk::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.confirm_unk (*this);
}

void mu_coin::confirm_req::serialize (mu_coin::byte_write_stream & stream_a)
{
    assert (block != nullptr);
    stream_a.write (mu_coin::message_type::confirm_req);
    mu_coin::serialize_block (stream_a, *block);
}

mu_coin::confirm_req::confirm_req (std::unique_ptr <mu_coin::block> block_a) :
block (std::move (block_a))
{
}

void mu_coin::publish_err::serialize (mu_coin::byte_write_stream & stream_a)
{
    stream_a.write (mu_coin::message_type::publish_err);
    stream_a.write (block);
}

void mu_coin::publish_err::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.publish_err (*this);
}

bool mu_coin::publish_err::deserialize (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::message_type type;
    stream_a.read (type);
    assert (type == mu_coin::message_type::publish_err);
    auto result (stream_a.read (block));
    return result;
}

mu_coin::rpc::rpc (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, mu_coin::client & client_a) :
server (decltype (server)::options (*this).address ("0.0.0.0").port (std::to_string (port_a)).io_service (service_a).thread_pool (pool_a)),
client (client_a)
{
}

void mu_coin::rpc::listen ()
{
    server.listen ();
}

void mu_coin::rpc::operator () (boost::network::http::server <mu_coin::rpc>::request const & request, boost::network::http::server <mu_coin::rpc>::response & response)
{
    if (request.method == "POST")
    {
        try
        {
            boost::property_tree::ptree request_l;
            std::stringstream istream (request.body);
            boost::property_tree::read_json (istream, request_l);
            std::string action (request_l.get <std::string> ("action"));
            if (action == "balance")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                mu_coin::uint256_union account;
                auto error (account.decode_hex (account_text));
                if (!error)
                {
                    auto balance (client.ledger.balance (account));
                    boost::property_tree::ptree response_l;
                    response_l.put ("balance", balance.convert_to <std::string> ());
                    std::stringstream ostream;
                    boost::property_tree::write_json (ostream, response_l);
                    response.status = boost::network::http::server <mu_coin::rpc>::response::ok;
                    response.headers.push_back (boost::network::http::response_header_narrow {"Content-Type", "application/json"});
                    response.content = ostream.str ();
                }
                else
                {
                    response = boost::network::http::server<mu_coin::rpc>::response::stock_reply (boost::network::http::server<mu_coin::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "create")
            {
                mu_coin::keypair new_key;
                client.wallet.insert (new_key.pub, new_key.prv, client.wallet.password);
                boost::property_tree::ptree response_l;
                std::string account;
                new_key.pub.encode_hex (account);
                response_l.put ("account", account);
                std::stringstream ostream;
                boost::property_tree::write_json (ostream, response_l);
                response.status = boost::network::http::server <mu_coin::rpc>::response::ok;
                response.headers.push_back (boost::network::http::response_header_narrow {"Content-Type", "application/json"});
                response.content = ostream.str ();
            }
            else
            {
                response = boost::network::http::server<mu_coin::rpc>::response::stock_reply (boost::network::http::server<mu_coin::rpc>::response::bad_request);
                response.content = "Unknown command";
            }
        }
        catch (std::runtime_error const &)
        {
            response = boost::network::http::server<mu_coin::rpc>::response::stock_reply (boost::network::http::server<mu_coin::rpc>::response::bad_request);
            response.content = "Unable to parse JSON";
        }
    }
    else
    {
        response = boost::network::http::server<mu_coin::rpc>::response::stock_reply (boost::network::http::server<mu_coin::rpc>::response::method_not_allowed);
        response.content = "Can only POST requests";
    }
}