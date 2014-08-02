#include <mu_coin/mu_coin.hpp>

#include <cryptopp/sha3.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <ed25519-donna/ed25519.h>
#include <cryptopp/osrng.h>

#include <unordered_set>
#include <memory>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace
{
    bool const network_debug = true;
}

CryptoPP::AutoSeededRandomPool random_pool;

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

void hash_number (CryptoPP::SHA3 & hash_a, boost::multiprecision::uint256_t const & number_a)
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

namespace {
class ledger_processor : public mu_coin::block_visitor
{
public:
    ledger_processor (mu_coin::ledger &);
    void send_block (mu_coin::send_block const &) override;
    void receive_block (mu_coin::receive_block const &) override;
    void open_block (mu_coin::open_block const &) override;
    void change_block (mu_coin::change_block const &) override;
    mu_coin::ledger & ledger;
    mu_coin::process_result result;
};

class amount_visitor : public mu_coin::block_visitor
{
public:
    amount_visitor (mu_coin::block_store &);
    void compute (mu_coin::block_hash const &);
    void send_block (mu_coin::send_block const &) override;
    void receive_block (mu_coin::receive_block const &) override;
    void open_block (mu_coin::open_block const &) override;
    void change_block (mu_coin::change_block const &) override;
    void from_send (mu_coin::block_hash const &);
    mu_coin::block_store & store;
    mu_coin::uint256_t result;
};

class balance_visitor : public mu_coin::block_visitor
{
public:
    balance_visitor (mu_coin::block_store &);
    void compute (mu_coin::block_hash const &);
    void send_block (mu_coin::send_block const &) override;
    void receive_block (mu_coin::receive_block const &) override;
    void open_block (mu_coin::open_block const &) override;
    void change_block (mu_coin::change_block const &) override;
    mu_coin::block_store & store;
    mu_coin::uint256_t result;
};

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
        from_previous (block_a.hashables.source);
    }
    void open_block (mu_coin::open_block const & block_a) override
    {
        from_previous (block_a.hashables.source);
    }
    void change_block (mu_coin::change_block const & block_a) override
    {
        account_visitor prev (store);
        prev.compute (block_a.hashables.previous);
        result = prev.result;
    }
    void from_previous (mu_coin::block_hash const & hash_a)
    {
        auto block (store.block_get (hash_a));
        assert (block != nullptr);
        assert (dynamic_cast <mu_coin::send_block *> (block.get ()) != nullptr);
        auto send (static_cast <mu_coin::send_block *> (block.get ()));
        result = send->hashables.destination;
    }
    mu_coin::block_store & store;
    mu_coin::address result;
};

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
    from_send (block_a.hashables.source);
}

void amount_visitor::open_block (mu_coin::open_block const & block_a)
{
    from_send (block_a.hashables.source);
}

void amount_visitor::change_block (mu_coin::change_block const & block_a)
{
    
}

void amount_visitor::from_send (mu_coin::block_hash const & hash_a)
{
    balance_visitor source (store);
    source.compute (hash_a);
    auto source_block (store.block_get (hash_a));
    assert (source_block != nullptr);
    balance_visitor source_prev (store);
    source_prev.compute (source_block->previous ());
}

balance_visitor::balance_visitor (mu_coin::block_store & store_a):
store (store_a),
result (0)
{
}

void balance_visitor::send_block (mu_coin::send_block const & block_a)
{
    result = block_a.hashables.balance.number ();
}

void balance_visitor::receive_block (mu_coin::receive_block const & block_a)
{
    balance_visitor prev (store);
    prev.compute (block_a.hashables.previous);
    amount_visitor source (store);
    source.compute (block_a.hashables.source);
    result = prev.result + source.result;
}

void balance_visitor::open_block (mu_coin::open_block const & block_a)
{
    amount_visitor source (store);
    source.compute (block_a.hashables.source);
    result = source.result;
}

void balance_visitor::change_block (mu_coin::change_block const & block_a)
{
    balance_visitor prev (store);
    prev.compute (block_a.hashables.previous);
    result = prev.result;
}
}

mu_coin::uint256_t mu_coin::ledger::balance (mu_coin::block_hash const & hash_a)
{
	balance_visitor visitor (store);
	visitor.compute (hash_a);
	return visitor.result;
}

mu_coin::uint256_t mu_coin::ledger::account_balance (mu_coin::address const & address_a)
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
    ledger_processor processor (*this);
    block_a.visit (processor);
    return processor.result;
}

mu_coin::keypair::keypair ()
{
    ed25519_randombytes_unsafe (prv.bytes.data (), sizeof (prv.bytes));
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

mu_coin::keypair::keypair (std::string const & prv_a)
{
    auto error (prv.decode_hex (prv_a));
    assert (!error);
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

void mu_coin::uint256_union::serialize (mu_coin::stream & stream_a) const
{
    write (stream_a, bytes);
}

bool mu_coin::uint256_union::deserialize (mu_coin::stream & stream_a)
{
    return read (stream_a, bytes);
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
    CryptoPP::SHA3 hash (32);
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

void mu_coin::send_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

void mu_coin::send_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
    hash_a.Update (balance.bytes.data (), sizeof (balance.bytes));
    hash_a.Update (destination.bytes.data (), sizeof (destination.bytes));
}

void mu_coin::send_block::serialize (mu_coin::stream & stream_a) const
{
    write (stream_a, signature.bytes);
    write (stream_a, hashables.previous.bytes);
    write (stream_a, hashables.balance.bytes);
    write (stream_a, hashables.destination.bytes);
}

bool mu_coin::send_block::deserialize (mu_coin::stream & stream_a)
{
    auto result (false);
    result = read (stream_a, signature.bytes);
    if (!result)
    {
        result = read (stream_a, hashables.previous.bytes);
        if (!result)
        {
            result = read (stream_a, hashables.balance.bytes);
            if (!result)
            {
                result = read (stream_a, hashables.destination.bytes);
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

bool mu_coin::receive_block::deserialize (mu_coin::stream & stream_a)
{
    auto result (false);
    result = read (stream_a, signature.bytes);
    if (!result)
    {
        result = read (stream_a, hashables.previous.bytes);
        if (!result)
        {
            result = read (stream_a, hashables.source.bytes);
        }
    }
    return result;
}

void mu_coin::receive_block::serialize (mu_coin::stream & stream_a) const
{
    write (stream_a, signature.bytes);
    write (stream_a, hashables.previous.bytes);
    write (stream_a, hashables.source.bytes);
}

void mu_coin::receive_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

void mu_coin::receive_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (source.bytes.data (), sizeof (source.bytes));
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
}

namespace
{
    class representative_visitor : public mu_coin::block_visitor
    {
    public:
        representative_visitor (mu_coin::block_store & store_a) :
        store (store_a)
        {
        }
        void compute (mu_coin::block_hash const & hash_a)
        {
            auto block (store.block_get (hash_a));
            assert (block != nullptr);
            block->visit (*this);
        }
        void send_block (mu_coin::send_block const & block_a) override
        {
            representative_visitor visitor (store);
            visitor.compute (block_a.previous ());
            result = visitor.result;
        }
        void receive_block (mu_coin::receive_block const & block_a) override
        {
            representative_visitor visitor (store);
            visitor.compute (block_a.previous ());
            result = visitor.result;
        }
        void open_block (mu_coin::open_block const & block_a) override
        {
            result = block_a.hashables.representative;
        }
        void change_block (mu_coin::change_block const & block_a) override
        {
            result = block_a.hashables.representative;
        }
        mu_coin::block_store & store;
        mu_coin::address result;
    };
}

void ledger_processor::change_block (mu_coin::change_block const & block_a)
{
    mu_coin::uint256_union message (block_a.hash ());
    auto existing (ledger.store.block_get (message));
    result = existing != nullptr ? mu_coin::process_result::old : mu_coin::process_result::progress; // Have we seen this block before? (Harmless)
    if (result == mu_coin::process_result::progress)
    {
        auto previous (ledger.store.block_get (block_a.hashables.previous));
        result = previous != nullptr ? mu_coin::process_result::progress : mu_coin::process_result::gap;  // Have we seen the previous block before? (Harmless)
        if (result == mu_coin::process_result::progress)
        {
			auto account (ledger.account (block_a.hashables.previous));
            mu_coin::block_hash latest;
            auto latest_error (ledger.store.latest_get (account, latest));
            assert (!latest_error);
            result = validate_message (account, message, block_a.signature) ? mu_coin::process_result::bad_signature : mu_coin::process_result::progress; // Is this block signed correctly (Malformed)
            if (result == mu_coin::process_result::progress)
            {
                result = latest == block_a.hashables.previous ? mu_coin::process_result::progress : mu_coin::process_result::fork; // Is the previous block the latest (Malicious)
                if (result == mu_coin::process_result::progress)
                {
					ledger.move_representation (ledger.representative (block_a.hashables.previous), block_a.hashables.representative, ledger.balance (block_a.hashables.previous));
                    ledger.store.block_put (message, block_a);
                    ledger.store.latest_put (account, message);
                }
            }
        }
    }
}

void ledger_processor::send_block (mu_coin::send_block const & block_a)
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
			auto account (ledger.account (block_a.hashables.previous));
            result = validate_message (account, message, block_a.signature) ? mu_coin::process_result::bad_signature : mu_coin::process_result::progress; // Is this block signed correctly (Malformed)
            if (result == mu_coin::process_result::progress)
            {
                mu_coin::uint256_t coins (ledger.balance (block_a.hashables.previous));
                result = coins > block_a.hashables.balance.number () ? mu_coin::process_result::progress : mu_coin::process_result::overspend; // Is this trying to spend more than they have (Malicious)
                if (result == mu_coin::process_result::progress)
                {
                    mu_coin::block_hash latest;
                    auto latest_error (ledger.store.latest_get (account, latest));
                    assert (!latest_error);
                    result = latest == block_a.hashables.previous ? mu_coin::process_result::progress : mu_coin::process_result::fork;
                    if (result == mu_coin::process_result::progress)
                    {
                        ledger.store.block_put (message, block_a);
                        ledger.store.latest_put (account, message);
                        ledger.store.pending_put (message);
                    }
                }
            }
        }
    }
}

void ledger_processor::receive_block (mu_coin::receive_block const & block_a)
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
                    mu_coin::block_hash latest;
                    result = ledger.store.latest_get (source_send->hashables.destination, latest) ? mu_coin::process_result::gap : mu_coin::process_result::progress;  //Have we seen the previous block? (Harmless)
                    if (result == mu_coin::process_result::progress)
                    {
                        result = latest == block_a.hashables.previous ? mu_coin::process_result::progress : mu_coin::process_result::gap; // Block doesn't immediately follow latest block (Harmless)
                        if (result == mu_coin::process_result::progress)
                        {
                            ledger.store.pending_del (source_send->hash ());
                            ledger.store.block_put (hash, block_a);
                            ledger.store.latest_put (source_send->hashables.destination, hash);
                            ledger.move_representation (ledger.account (block_a.hashables.source), ledger.account (hash), ledger.amount (block_a.hashables.source));
                        }
                        else
                        {
                            result = ledger.store.block_get (latest) ? mu_coin::process_result::fork : mu_coin::process_result::gap; // If we have the block but it's not the latest we have a signed fork (Malicious)
                        }
                    }
                }
            }
        }
    }
}

void ledger_processor::open_block (mu_coin::open_block const & block_a)
{
    auto hash (block_a.hash ());
    auto existing (ledger.store.block_get (hash));
    result = existing != nullptr ? mu_coin::process_result::old : mu_coin::process_result::progress; // Have we seen this block already? (Harmless)
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
                    mu_coin::block_hash latest;
                    result = ledger.store.latest_get (source_send->hashables.destination, latest) ? mu_coin::process_result::progress : mu_coin::process_result::fork; // Has this account already been opened? (Malicious)
                    if (result == mu_coin::process_result::progress)
                    {
                        ledger.store.pending_del (source_send->hash ());
                        ledger.store.block_put (hash, block_a);
                        ledger.store.latest_put (source_send->hashables.destination, hash);
						ledger.move_representation (ledger.account (block_a.hashables.source), ledger.account (hash), ledger.amount (block_a.hashables.source));
                    }
                }
            }
        }
    }
}

ledger_processor::ledger_processor (mu_coin::ledger & ledger_a) :
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

std::unique_ptr <mu_coin::block> mu_coin::deserialize_block (mu_coin::stream & stream_a)
{
    mu_coin::block_type type;
    auto error (read (stream_a, type));
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
            case mu_coin::block_type::open:
            {
                std::unique_ptr <mu_coin::open_block> obj (new mu_coin::open_block);
                auto error (obj->deserialize (stream_a));
                if (!error)
                {
                    result = std::move (obj);
                }
                break;
            }
            case mu_coin::block_type::change:
            {
                std::unique_ptr <mu_coin::change_block> obj (new mu_coin::change_block);
                auto error (obj->deserialize (stream_a));
                if (!error)
                {
                    result = std::move (obj);
                }
                break;
            }
        }
    }
    return result;
}

void mu_coin::serialize_block (mu_coin::stream & stream_a, mu_coin::block const & block_a)
{
    write (stream_a, block_a.type ());
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

void mu_coin::uint256_union::encode_hex (std::string & text) const
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

void mu_coin::uint256_union::encode_dec (std::string & text) const
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
pending (nullptr, 0),
representation (nullptr, 0),
forks (nullptr, 0),
bootstrap (nullptr, 0),
successors (nullptr, 0)
{
    boost::filesystem::create_directories (path_a);
    addresses.open (nullptr, (path_a / "addresses.bdb").native ().c_str (), nullptr, DB_BTREE, DB_CREATE | DB_EXCL, 0);
    blocks.open (nullptr, (path_a / "blocks.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    pending.open (nullptr, (path_a / "pending.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    representation.open (nullptr, (path_a / "representation.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    forks.open (nullptr, (path_a / "forks.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    bootstrap.open (nullptr, (path_a / "bootstrap.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    successors.open (nullptr, (path_a / "successors.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
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

mu_coin::genesis::genesis (mu_coin::address const & key_a, mu_coin::uint256_t const & coins_a)
{
    send1.hashables.destination.clear ();
    send1.hashables.balance = coins_a;
    send1.hashables.previous.clear ();
    send1.signature.clear ();
    send2.hashables.destination = key_a;
    send2.hashables.balance.clear ();
    send2.hashables.previous = send1.hash ();
    send2.signature.clear ();
    open.hashables.source = send2.hash ();
    open.hashables.representative = key_a;
    open.signature.clear ();
}

void mu_coin::genesis::initialize (mu_coin::block_store & store_a) const
{
    //assert (store_a.latest_begin () == store_a.latest_end ());
    store_a.block_put (send1.hash (), send1);
    store_a.block_put (send2.hash (), send2);
    store_a.block_put (open.hash (), open);
    store_a.latest_put (send2.hashables.destination, open.hash ());
    store_a.representation_put (send2.hashables.destination, send1.hashables.balance.number ());
}

bool mu_coin::block_store::latest_get (mu_coin::address const & address_a, mu_coin::block_hash & hash_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data;
    int error (addresses.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    bool result;
    if (error == DB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (data.data.get_data ()), data.data.get_size ());
        read (stream, hash_a.bytes);
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
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t *> (data.get_data ()), reinterpret_cast <uint8_t *> (data.get_data ()) + data.get_size ());
        result = mu_coin::deserialize_block (stream);
    }
    return result;
}

mu_coin::dbt::dbt (mu_coin::block const & block_a)
{
    {
        mu_coin::vectorstream stream (bytes);
        mu_coin::serialize_block (stream, block_a);
    }
    adopt ();
}

mu_coin::dbt::dbt (mu_coin::uint256_union const & address_a)
{
    {
        mu_coin::vectorstream stream (bytes);
        address_a.serialize (stream);
    }
    adopt ();
}

mu_coin::dbt::dbt (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    {
        mu_coin::vectorstream stream (bytes);
        write (stream, address_a.bytes);
        write (stream, hash_a.bytes);
    }
    adopt ();
}

void mu_coin::dbt::adopt ()
{
    data.set_data (bytes.data ());
    data.set_size (bytes.size ());
}

mu_coin::network::network (boost::asio::io_service & service_a, uint16_t port, mu_coin::client & client_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4::any (), port)),
service (service_a),
client (client_a),
keepalive_req_count (0),
keepalive_ack_count (0),
publish_req_count (0),
confirm_req_count (0),
confirm_ack_count (0),
confirm_nak_count (0),
confirm_unk_count (0),
bad_sender_count (0),
unknown_count (0),
error_count (0),
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
    client.peers.random_fill (message.peers);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        mu_coin::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_debug)
    {
        std::cerr << "Keepalive req " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (endpoint_a.port ()) << std::endl;
    }
    socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), endpoint_a, [bytes] (boost::system::error_code const &, size_t) {});
}

void mu_coin::network::publish_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <mu_coin::block> block)
{
    mu_coin::publish_req message (std::move (block));
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        mu_coin::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_debug)
    {
        std::cerr << "Publish " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (endpoint_a.port ()) << std::endl;
    }
    socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), endpoint_a, [bytes] (boost::system::error_code const & ec, size_t size) {});
}

void mu_coin::network::confirm_block (boost::asio::ip::udp::endpoint const & endpoint_a, mu_coin::uint256_union const & session_a, std::unique_ptr <mu_coin::block> block)
{
    mu_coin::confirm_req message;
	message.session = session_a;
	message.block = std::move (block);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        mu_coin::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_debug)
    {
        std::cerr << "Confirm " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (endpoint_a.port ()) << std::endl;
    }
    socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), endpoint_a, [bytes] (boost::system::error_code const & ec, size_t size) {});
}

void mu_coin::network::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (!mu_coin::reserved_address (remote) && remote != endpoint ())
        {
            if (size_a >= sizeof (mu_coin::message_type))
            {
                auto sender (remote);
                client.peers.incoming_from_peer (sender);
                mu_coin::bufferstream type_stream (buffer.data (), size_a);
                mu_coin::message_type type;
                read (type_stream, type);
                switch (type)
                {
                    case mu_coin::message_type::keepalive_req:
                    {
                        ++keepalive_req_count;
                        mu_coin::keepalive_req incoming;
                        mu_coin::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            if (network_debug)
                            {
                                std::cerr << "Keepalive req " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                            }
                            mu_coin::keepalive_ack ack_message;
                            client.peers.random_fill (ack_message.peers);
                            std::shared_ptr <std::vector <uint8_t>> ack_bytes (new std::vector <uint8_t>);
                            {
                                mu_coin::vectorstream stream (*ack_bytes);
                                ack_message.serialize (stream);
                            }
                            mu_coin::keepalive_req req_message;
                            req_message.peers = ack_message.peers;
                            std::shared_ptr <std::vector <uint8_t>> req_bytes (new std::vector <uint8_t>);
                            {
                                mu_coin::vectorstream stream (*req_bytes);
                                req_message.serialize (stream);
                            }
                            merge_peers (req_bytes, incoming.peers);
                            if (network_debug)
                            {
                                std::cerr << "Keepalive ack " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (sender.port ()) << std::endl;
                            }
                            socket.async_send_to (boost::asio::buffer (ack_bytes->data (), ack_bytes->size ()), sender, [ack_bytes] (boost::system::error_code const & error, size_t size_a)
                            {
                                if (network_debug)
                                {
                                    if (error)
                                    {
                                        std::cerr << "Send error: " << error.message () << std::endl;
                                    }
                                }
                            });
                        }
                        break;
                    }
                    case mu_coin::message_type::keepalive_ack:
                    {
                        mu_coin::keepalive_ack incoming;
                        mu_coin::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            ++keepalive_ack_count;
                            if (network_debug)
                            {
                                std::cerr << "Keepalive ack " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                            }
                            mu_coin::keepalive_req req_message;
                            client.peers.random_fill (req_message.peers);
                            std::shared_ptr <std::vector <uint8_t>> req_bytes (new std::vector <uint8_t>);
                            {
                                mu_coin::vectorstream stream (*req_bytes);
                                req_message.serialize (stream);
                            }
                            merge_peers (req_bytes, incoming.peers);
                        }
                        break;
                    }
                    case mu_coin::message_type::publish_req:
                    {
                        mu_coin::publish_req incoming;
                        mu_coin::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            ++publish_req_count;
                            if (network_debug)
                            {
                                std::cerr << "Publish req " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                            }
                            client.processor.process_and_republish (std::move (incoming.block), sender);
                        }
                        else
                        {
                            ++error_count;
                        }
                        break;
                    }
                    case mu_coin::message_type::confirm_req:
                    {
                        ++confirm_req_count;
                        if (network_debug)
                        {
                            std::cerr << "Confirm req " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                        }
                        auto incoming (new mu_coin::confirm_req);
                        mu_coin::bufferstream stream (buffer.data (), size_a);
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
                                    client.processor.process_confirmation (incoming->session, incoming->block->hash (), sender);
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
                        if (network_debug)
                        {
                            std::cerr << "Confirm ack " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                        }
                        auto incoming (new mu_coin::confirm_ack);
                        mu_coin::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming->deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            client.processor.confirm_ack (std::unique_ptr <mu_coin::confirm_ack> {incoming}, sender);
                        }
                        break;
                    }
                    case mu_coin::message_type::confirm_nak:
                    {
                        ++confirm_nak_count;
                        if (network_debug)
                        {
                            std::cerr << "Confirm nak " << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                        }
                        auto incoming (new mu_coin::confirm_nak);
                        mu_coin::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming->deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            client.processor.confirm_nak (std::unique_ptr <mu_coin::confirm_nak> {incoming}, sender);
                        }
                        break;
                    }
                    case mu_coin::message_type::confirm_unk:
                    {
                        ++confirm_unk_count;
                        auto incoming (new mu_coin::confirm_unk);
                        mu_coin::bufferstream stream (buffer.data (), size_a);
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
        else
        {
            ++bad_sender_count;
            if (network_debug)
            {
                std::cerr << "Reserved sender" << std::endl;
            }
        }
    }
    else
    {
        if (network_debug)
        {
            std::cerr << "Receive error" << std::endl;
        }
    }
}

void mu_coin::network::merge_peers (std::shared_ptr <std::vector <uint8_t>> const & bytes_a, std::array <mu_coin::endpoint, 24> const & peers_a)
{
    for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i) // Amplify attack, send to the same IP many times
    {
        if (!client.peers.contacting_peer (*i) && *i != endpoint ())
        {
            if (network_debug)
            {
                std::cerr << "Keepalive req " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (i->port ()) << std::endl;
            }
            socket.async_send_to (boost::asio::buffer (bytes_a->data (), bytes_a->size ()), *i, [bytes_a] (boost::system::error_code const & error, size_t size_a)
            {
                if (network_debug)
                {
                    if (error)
                    {
                        std::cerr << "Send error: " << error.message () << std::endl;
                    }
                }
            });
        }
        else
        {
            if (network_debug)
            {
                if (mu_coin::reserved_address (*i))
                {
                    if (i->address ().to_v4 ().to_ulong () != 0 || i->port () != 0)
                    {
                        std::cerr << "Keepalive_req contained reserved address" << std::endl;
                    }
                }
            }
        }
    }
}

mu_coin::publish_req::publish_req (std::unique_ptr <mu_coin::block> block_a) :
block (std::move (block_a))
{
}

bool mu_coin::publish_req::deserialize (mu_coin::stream & stream_a)
{
    auto result (false);
    mu_coin::message_type type;
    result = read (stream_a, type);
    assert (!result);
    block = mu_coin::deserialize_block (stream_a);
    result = block == nullptr;
    return result;
}

void mu_coin::publish_req::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::publish_req);
    mu_coin::serialize_block (stream_a, *block);
}

mu_coin::wallet_temp_t mu_coin::wallet_temp;


mu_coin::dbt::dbt (mu_coin::private_key const & prv, mu_coin::secret_key const & key, mu_coin::uint128_union const & iv)
{
    mu_coin::uint256_union encrypted (prv, key, iv);
    {
        mu_coin::vectorstream stream (bytes);
        write (stream, encrypted.bytes);
    }
    adopt ();
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
    mu_coin::bufferstream stream (reinterpret_cast <uint8_t *> (data.get_data ()), data.get_size ());
    auto result (read (stream, encrypted.bytes));
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
        auto balance (ledger_a.account_balance (account));
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
    if (!remaining.is_zero ())
    {
        result = true;
        blocks.clear ();
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
    mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (data.get_data ()), data.get_size ());
    read (stream, result);
    return result;
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

mu_coin::processor::processor (mu_coin::client & client_a) :
client (client_a)
{
}

bool mu_coin::operation::operator < (mu_coin::operation const & other_a) const
{
    return wakeup < other_a.wakeup;
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, boost::filesystem::path const & wallet_path_a, boost::filesystem::path const & block_store_path_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a, mu_coin::genesis const & genesis_a) :
genesis (genesis_a),
representative (representative_a),
store (block_store_path_a),
ledger (store),
wallet (0, wallet_path_a),
network (*service_a, port_a, *this),
bootstrap (*service_a, port_a, *this),
rpc (service_a, pool_a, command_port_a, *this),
processor (*this),
peers (network.endpoint ()),
service (processor_a)
{
    genesis_a.initialize (store);
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a, mu_coin::genesis const & genesis_a) :
client (service_a, pool_a, port_a, command_port_a, boost::filesystem::unique_path (), boost::filesystem::unique_path (), processor_a, representative_a, genesis_a)
{
}

void mu_coin::processor::republish (std::unique_ptr <mu_coin::block> incoming_a, mu_coin::endpoint const & sender_a)
{
    auto list (client.peers.list ());
    for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
    {
        if (i->endpoint != sender_a)
        {
            client.network.publish_block (i->endpoint, incoming_a->clone ());
        }
    }
}

namespace {
class publish_visitor : public mu_coin::block_visitor
{
public:
    publish_visitor (mu_coin::client & client_a, std::unique_ptr <mu_coin::block> incoming_a, mu_coin::endpoint const & sender_a) :
    client (client_a),
    incoming (std::move (incoming_a)),
    sender (sender_a)
    {
    }
    void send_block (mu_coin::send_block const & block_a)
    {
        handle_process_result (client.ledger.process (block_a), block_a.hashables.destination);
    }
    void receive_block (mu_coin::receive_block const & block_a)
    {
        handle_process_result (client.ledger.process (block_a), 0);
    }
    void open_block (mu_coin::open_block const & block_a)
    {
        handle_process_result (client.ledger.process (block_a), 0);
    }
    void change_block (mu_coin::change_block const & block_a)
    {
        handle_process_result (client.ledger.process (block_a), 0);
    }
    void handle_process_result (mu_coin::process_result result_a, mu_coin::address destination)
    {
        switch (result_a)
        {
            case mu_coin::process_result::progress:
                if (client.wallet.find (destination) != client.wallet.end ())
                {
                    client.processor.process_receivable (std::move (incoming), mu_coin::endpoint {});
                }
                else
                {
                    client.processor.republish (std::move (incoming), sender);
                }
                break;
            case mu_coin::process_result::old:
            case mu_coin::process_result::bad_signature:
            case mu_coin::process_result::overspend:
            case mu_coin::process_result::overreceive:
            case mu_coin::process_result::gap:
            case mu_coin::process_result::not_receive_from_send:
                break;
            case mu_coin::process_result::fork:
                assert (false);
                break;
        }
    }
    mu_coin::client & client;
    std::unique_ptr <mu_coin::block> incoming;
    mu_coin::endpoint sender;
    mu_coin::message_type result;
};

class receivable_message_processor : public mu_coin::message_visitor
{
public:
    receivable_message_processor (mu_coin::receivable_processor & processor_a, mu_coin::endpoint const & sender_a) :
    processor (processor_a),
    sender (sender_a)
    {
    }
    void keepalive_req (mu_coin::keepalive_req const &) override
    {
        assert (false);
    }
    void keepalive_ack (mu_coin::keepalive_ack const &) override
    {
        assert (false);
    }
    void publish_req (mu_coin::publish_req const &) override
    {
        assert (false);
    }
    void confirm_req (mu_coin::confirm_req const &) override
    {
        assert (false);
    }
    void confirm_ack (mu_coin::confirm_ack const &) override;
    void confirm_unk (mu_coin::confirm_unk const &) override;
    void confirm_nak (mu_coin::confirm_nak const &) override;
    void bulk_req (mu_coin::bulk_req const &) override
    {
        assert (false);
    }
    mu_coin::receivable_processor & processor;
    mu_coin::endpoint sender;
};
}

mu_coin::receivable_processor::receivable_processor (std::unique_ptr <mu_coin::block> incoming_a, mu_coin::endpoint const & sender_a, mu_coin::client & client_a) :
threshold (client_a.ledger.supply () / 2),
incoming (std::move (incoming_a)),
sender (sender_a),
client (client_a),
complete (false)
{
    ed25519_randombytes_unsafe (session.bytes.data (), sizeof (session.bytes));
}

void mu_coin::receivable_processor::run ()
{
    mu_coin::uint256_t weight (0);
    if (client.wallet.find (client.representative) != client.wallet.end ())
    {
        weight = client.ledger.weight (client.representative);
    }
    process_acknowledged (weight);
    if (!complete)
    {
        auto this_l (shared_from_this ());
        client.processor.add_confirm_listener (session, [this_l] (std::unique_ptr <mu_coin::message> message_a, mu_coin::endpoint const & endpoint_a) {this_l->confirm_ack (std::move (message_a), endpoint_a);});
        auto list (client.peers.list ());
        for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
        {
            if (i->endpoint != sender)
            {
                client.network.confirm_block (i->endpoint, session, incoming->clone ());
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
    client.service.add (timeout, [this_l] () {this_l->timeout_action ();});
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
            assert (dynamic_cast <mu_coin::send_block *> (incoming.get ()) != nullptr);
            auto & send (static_cast <mu_coin::send_block &> (*incoming.get ()));
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
                        balance_visitor visitor (client.ledger.store);
                        visitor.compute (send.hashables.previous);
                        auto open (new mu_coin::open_block);
                        open->hashables.source = hash;
                        open->hashables.representative = client.representative;
                        mu_coin::sign_message (prv, send.hashables.destination, open->hash (), open->signature);
                        prv.bytes.fill (0);
                        client.processor.process_and_republish (std::unique_ptr <mu_coin::block> (open), mu_coin::endpoint {});
                    }
                    else
                    {
                        balance_visitor visitor (client.ledger.store);
                        visitor.compute (send.hashables.previous);
                        auto receive (new mu_coin::receive_block);
                        receive->hashables.previous = previous;
                        receive->hashables.source = hash;
                        mu_coin::sign_message (prv, send.hashables.destination, receive->hash (), receive->signature);
                        prv.bytes.fill (0);
                        client.processor.process_and_republish (std::unique_ptr <mu_coin::block> (receive), mu_coin::endpoint {});
                    }
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

void receivable_message_processor::confirm_ack (mu_coin::confirm_ack const & message)
{
    assert (message.session == processor.session);
    if (!mu_coin::validate_message (message.address, message.hash (), message.signature))
    {
        auto weight (processor.client.ledger.weight (message.address));
        std::string ack_string (weight.convert_to <std::string> ());
        processor.process_acknowledged (weight);
    }
    else
    {
        // Signature didn't match
    }
}

void receivable_message_processor::confirm_unk (mu_coin::confirm_unk const & message)
{
}

void receivable_message_processor::confirm_nak (mu_coin::confirm_nak const & message)
{
    assert (message.session == processor.session);
    if (!mu_coin::validate_message (message.address, message.hash (), message.signature))
    {
        auto weight (processor.client.ledger.weight (message.address));
        processor.nacked += weight;
    }
    else
    {
        // Signature didn't match.
    }
}

void mu_coin::processor::process_receivable (std::unique_ptr <mu_coin::block> incoming, mu_coin::endpoint const & sender_a)
{
    auto processor (std::make_shared <receivable_processor> (std::move (incoming), sender_a, client));
    processor->run ();
}

void mu_coin::processor::process_and_republish (std::unique_ptr <mu_coin::block> incoming, mu_coin::endpoint const & sender_a)
{
    publish_visitor visitor (client, std::move (incoming), sender_a);
    visitor.incoming->visit (visitor);
}

void mu_coin::peer_container::incoming_from_peer (mu_coin::endpoint const & endpoint_a)
{
	assert (!reserved_address (endpoint_a));
	if (endpoint_a != self)
	{
		std::lock_guard <std::mutex> lock (mutex);
		auto existing (peers.find (endpoint_a));
		if (existing == peers.end ())
		{
			peers.insert ({endpoint_a, std::chrono::system_clock::now (), std::chrono::system_clock::now ()});
		}
		else
		{
			peers.modify (existing, [] (mu_coin::peer_information & info) {info.last_contact = std::chrono::system_clock::now (); info.last_attempt = std::chrono::system_clock::now ();});
		}
	}
	else
	{
		if (network_debug)
		{
			std::cerr << "Ignoring self endpoint" << std::endl;
		}
	}
}

std::vector <mu_coin::peer_information> mu_coin::peer_container::list ()
{
    std::vector <mu_coin::peer_information> result;
    std::lock_guard <std::mutex> lock (mutex);
    result.reserve (peers.size ());
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        result.push_back (*i);
    }
    return result;
}

void mu_coin::processor::add_confirm_listener (mu_coin::uint256_union const & session_a, session const & function_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    confirm_listeners [session_a] = function_a;
}

void mu_coin::processor::remove_confirm_listener (mu_coin::block_hash const & block_a)
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

void mu_coin::keepalive_ack::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::keepalive_ack);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address (i->address ().to_v4 ().to_ulong ());
        write (stream_a, address);
        write (stream_a, i->port ());
    }
}

bool mu_coin::keepalive_ack::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
    assert (type == mu_coin::message_type::keepalive_ack);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = mu_coin::endpoint (boost::asio::ip::address_v4 (address), port);
    }
    return result;
}

void mu_coin::keepalive_req::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::keepalive_req);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address (i->address ().to_v4 ().to_ulong ());
        write (stream_a, address);
        write (stream_a, i->port ());
    }
}

bool mu_coin::keepalive_req::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
    assert (type == mu_coin::message_type::keepalive_req);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = mu_coin::endpoint (boost::asio::ip::address_v4 (address), port);
    }
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

size_t mu_coin::processor::publish_listener_size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return confirm_listeners.size ();
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
    return (cursor == nullptr && other_a.cursor == nullptr) || (cursor != nullptr && other_a.cursor != nullptr && current.first == other_a.current.first);
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
            processor.process_and_republish (std::move (*i), mu_coin::endpoint {});
        }
    }
    return result;
}

mu_coin::system::system (size_t threads_a, uint16_t port_a, uint16_t command_port_a, size_t count_a, mu_coin::uint256_t const & amount) :
test_genesis_address ("E49C03BB7404C10B388AE56322217306B57F3DCBB3A5F060A2F420AD7AA3F034"),
genesis (test_genesis_address.pub, amount),
service (new boost::asio::io_service),
pool (new boost::network::utils::thread_pool (threads_a))
{
    clients.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        clients.push_back (std::unique_ptr <mu_coin::client> (new mu_coin::client (service, pool, port_a + i, command_port_a + i, processor, test_genesis_address.pub, genesis)));
        genesis.initialize (clients.back ()->store);
    }
    for (auto i (clients.begin ()), j (clients.end ()); i != j; ++i)
    {
        (*i)->start ();
        (*i)->bootstrap.accept ();
    }
    for (auto i (clients.begin ()), j (clients.begin () + 1), n (clients.end ()); j != n; ++i, ++j)
    {
        auto starting1 ((*i)->peers.size ());
        auto starting2 ((*j)->peers.size ());
        (*j)->network.send_keepalive (mu_coin::endpoint (boost::asio::ip::address_v4::loopback (), (*i)->network.endpoint().port ()));
        do {
            service->run_one ();
        } while ((*i)->peers.size () == starting1 || (*j)->peers.size () == starting2);
    }
}

void mu_coin::processor::process_unknown (mu_coin::uint256_union const & session_a, mu_coin::vectorstream & stream_a)
{
	mu_coin::confirm_unk outgoing;
	outgoing.rep_hint = client.representative;
	outgoing.session = session_a;
	outgoing.serialize (stream_a);
}

void mu_coin::processor::process_confirmation (mu_coin::uint256_union const & session_a, mu_coin::block_hash const & hash_a, mu_coin::endpoint const & sender)
{
    mu_coin::private_key prv;
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
	{
		mu_coin::vectorstream stream (*bytes);
		if (client.wallet.fetch (client.representative, client.wallet.password, prv))
		{
			process_unknown (session_a, stream);
		}
		else
		{
			auto weight (client.ledger.weight (client.representative));
			if (weight.is_zero ())
			{
				process_unknown (session_a, stream);
			}
			else
			{
				mu_coin::confirm_ack outgoing;
				outgoing.address = client.representative;
				outgoing.session = session_a;
				mu_coin::sign_message (prv, client.representative, outgoing.hash (), outgoing.signature);
				assert (!mu_coin::validate_message (client.representative, outgoing.hash (), outgoing.signature));
				{
					mu_coin::vectorstream stream (*bytes);
					outgoing.serialize (stream);
				}
			}
		}
	}
    client.network.socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), sender, [bytes] (boost::system::error_code const & error, size_t size_a) {});
}

mu_coin::key_entry * mu_coin::key_entry::operator -> ()
{
    return this;
}

bool mu_coin::confirm_ack::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
    assert (type == mu_coin::message_type::confirm_ack);
    if (!result)
    {
        result = read (stream_a, session);
        if (!result)
        {
			result = read (stream_a, address);
			if (!result)
			{
				result = read (stream_a, signature);
			}
        }
    }
    return result;
}

void mu_coin::confirm_ack::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::confirm_ack);
    write (stream_a, session);
    write (stream_a, address);
    write (stream_a, signature);
}

bool mu_coin::confirm_ack::operator == (mu_coin::confirm_ack const & other_a) const
{
    auto result (session == other_a.session && address == other_a.address && signature == other_a.signature);
    return result;
}

void mu_coin::confirm_ack::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.confirm_ack (*this);
}

bool mu_coin::confirm_nak::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    read (stream_a, type);
    assert (type == mu_coin::message_type::confirm_nak);
    auto result (read (stream_a, session));
    if (!result)
    {
		result = read (stream_a, address);
		if (!result)
		{
			result = read (stream_a, signature);
			if (!result)
			{
				block = mu_coin::deserialize_block (stream_a);
				result = block == nullptr;
			}
		}
    }
    return result;
}

bool mu_coin::confirm_req::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    read (stream_a, type);
    assert (type == mu_coin::message_type::confirm_req);
	auto result (read (stream_a, session));
	if (!result)
	{
		block = mu_coin::deserialize_block (stream_a);
		result = block == nullptr;
	}
    return result;
}

bool mu_coin::confirm_unk::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    read (stream_a, type);
    assert (type == mu_coin::message_type::confirm_unk);
    auto result (read (stream_a, rep_hint));
	if (!result)
	{
		result = read (stream_a, session);
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

void mu_coin::confirm_req::serialize (mu_coin::stream & stream_a)
{
    assert (block != nullptr);
    write (stream_a, mu_coin::message_type::confirm_req);
	write (stream_a, session);
    mu_coin::serialize_block (stream_a, *block);
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

namespace
{
void set_response (boost::network::http::server <mu_coin::rpc>::response & response, boost::property_tree::ptree & tree)
{
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    response.status = boost::network::http::server <mu_coin::rpc>::response::ok;
    response.headers.push_back (boost::network::http::response_header_narrow {"Content-Type", "application/json"});
    response.content = ostream.str ();
}
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
            if (action == "account_balance")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                mu_coin::uint256_union account;
                auto error (account.decode_hex (account_text));
                if (!error)
                {
                    auto balance (client.ledger.account_balance (account));
                    boost::property_tree::ptree response_l;
                    response_l.put ("balance", balance.convert_to <std::string> ());
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<mu_coin::rpc>::response::stock_reply (boost::network::http::server<mu_coin::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "wallet_create")
            {
                mu_coin::keypair new_key;
                client.wallet.insert (new_key.pub, new_key.prv, client.wallet.password);
                boost::property_tree::ptree response_l;
                std::string account;
                new_key.pub.encode_hex (account);
                response_l.put ("account", account);
                set_response (response, response_l);
            }
            else if (action == "wallet_contains")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                mu_coin::uint256_union account;
                auto error (account.decode_hex (account_text));
                if (!error)
                {
                    auto exists (client.wallet.find (account) != client.wallet.end ());
                    boost::property_tree::ptree response_l;
                    response_l.put ("exists", exists ? "1" : "0");
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<mu_coin::rpc>::response::stock_reply (boost::network::http::server<mu_coin::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "wallet_list")
            {
                boost::property_tree::ptree response_l;
                boost::property_tree::ptree accounts;
                for (auto i (client.wallet.begin ()), j (client.wallet.end ()); i != j; ++i)
                {
                    std::string account;
                    i->first.encode_hex (account);
                    boost::property_tree::ptree entry;
                    entry.put ("", account);
                    accounts.push_back (std::make_pair ("", entry));
                }
                response_l.add_child ("accounts", accounts);
                set_response (response, response_l);
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


void mu_coin::open_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

mu_coin::block_hash mu_coin::open_block::previous () const
{
    mu_coin::block_hash result (0);
    return result;
}

void mu_coin::open_block::serialize (mu_coin::stream & stream_a) const
{
    write (stream_a, hashables.representative);
    write (stream_a, hashables.source);
    write (stream_a, signature);
}

bool mu_coin::open_block::deserialize (mu_coin::stream & stream_a)
{
    auto result (read (stream_a, hashables.representative));
    if (!result)
    {
        result = read (stream_a, hashables.source);
        if (!result)
        {
            result = read (stream_a, signature);
        }
    }
    return result;
}

void mu_coin::open_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.open_block (*this);
}

std::unique_ptr <mu_coin::block> mu_coin::open_block::clone () const
{
    return std::unique_ptr <mu_coin::block> (new mu_coin::open_block (*this));
}

mu_coin::block_type mu_coin::open_block::type () const
{
    return mu_coin::block_type::open;
}

bool mu_coin::open_block::operator == (mu_coin::block const & other_a) const
{
    auto other_l (dynamic_cast <mu_coin::open_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool mu_coin::open_block::operator == (mu_coin::open_block const & other_a) const
{
    return hashables.representative == other_a.hashables.representative && hashables.source == other_a.hashables.source && signature == other_a.signature;
}

void mu_coin::open_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash_a.Update (source.bytes.data (), sizeof (source.bytes));
}

mu_coin::uint256_t mu_coin::block_store::representation_get (mu_coin::address const & address_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data;
    int error (representation.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    mu_coin::uint256_t result;
    if (error == 0)
    {
        assert (data.data.get_size () == 32);
        result = data.uint256 ().number ();
    }
    else
    {
        result = 0;
    }
    return result;
}

void mu_coin::block_store::representation_put (mu_coin::address const & address_a, mu_coin::uint256_t const & representation_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data (mu_coin::uint256_union {representation_a});
    int error (representation.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

mu_coin::address mu_coin::ledger::representative (mu_coin::block_hash const & hash_a)
{
	representative_visitor visitor (store);
	visitor.compute (hash_a);
	return visitor.result;
}

mu_coin::uint256_t mu_coin::ledger::weight (mu_coin::address const & address_a)
{
    return store.representation_get (address_a);
}

void mu_coin::confirm_unk::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, rep_hint);
    write (stream_a, session);
}

void mu_coin::change_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

mu_coin::block_hash mu_coin::change_block::previous () const
{
    return hashables.previous;
}

void mu_coin::change_block::serialize (mu_coin::stream & stream_a) const
{
    write (stream_a, hashables.representative);
    write (stream_a, hashables.previous);
    write (stream_a, signature);
}

bool mu_coin::change_block::deserialize (mu_coin::stream & stream_a)
{
    auto result (read (stream_a, hashables.representative));
    if (!result)
    {
        result = read (stream_a, hashables.previous);
        if (!result)
        {
            result = read (stream_a, signature);
        }
    }
    return result;
}

void mu_coin::change_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.change_block (*this);
}

std::unique_ptr <mu_coin::block> mu_coin::change_block::clone () const
{
    return std::unique_ptr <mu_coin::block> (new mu_coin::change_block (*this));
}

mu_coin::block_type mu_coin::change_block::type () const
{
    return mu_coin::block_type::change;
}

bool mu_coin::change_block::operator == (mu_coin::block const & other_a) const
{
    auto other_l (dynamic_cast <mu_coin::change_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool mu_coin::change_block::operator == (mu_coin::change_block const & other_a) const
{
    return signature == other_a.signature && hashables.representative == other_a.hashables.representative && hashables.previous == other_a.hashables.previous;
}

void mu_coin::change_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::fork_get (mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (hash_a);
    mu_coin::dbt data;
    int error (forks.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    auto result (data.block ());
    return result;
}

void mu_coin::block_store::fork_put (mu_coin::block_hash const & hash_a, mu_coin::block const & block_a)
{
    dbt key (hash_a);
    dbt data (block_a);
    int error (forks.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

bool mu_coin::uint256_union::operator != (mu_coin::uint256_union const & other_a) const
{
    return ! (*this == other_a);
}

namespace
{
class rollback_visitor : public mu_coin::block_visitor
{
public:
    rollback_visitor (mu_coin::ledger & ledger_a) :
    ledger (ledger_a)
    {
    }
    void send_block (mu_coin::send_block const & block_a) override
    {
		auto hash (block_a.hash ());
		auto account (ledger.account (hash));
		while (ledger.store.pending_get (hash))
		{
			ledger.rollback (ledger.latest (block_a.hashables.destination));
		}
		ledger.store.pending_del (hash);
		ledger.store.latest_put (account, block_a.hashables.previous);
		ledger.store.block_del (hash);
    }
    void receive_block (mu_coin::receive_block const & block_a) override
    {
		auto hash (block_a.hash ());
		auto account (ledger.account (hash));
		ledger.move_representation (account, ledger.account (block_a.hashables.source), ledger.amount (block_a.hashables.source));
		ledger.store.latest_put (account, block_a.hashables.previous);
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source);
    }
    void open_block (mu_coin::open_block const & block_a) override
    {
		auto hash (block_a.hash ());
		auto account (ledger.account (hash));
		ledger.move_representation (account, ledger.account (block_a.hashables.source), ledger.amount (block_a.hashables.source));
		ledger.store.latest_del (account);
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source);
    }
    void change_block (mu_coin::change_block const & block_a) override
    {
		ledger.move_representation (block_a.hashables.representative, ledger.representative (block_a.hashables.previous), ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (block_a.hash ());
		ledger.store.latest_put (ledger.account (block_a.hashables.previous), block_a.hashables.previous);
    }
    mu_coin::ledger & ledger;
};
}

void mu_coin::block_store::block_del (mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (hash_a);
    mu_coin::dbt data;
    int error (blocks.del (nullptr, &key.data, 0));
    assert (error == 0);
}

void mu_coin::ledger::rollback (mu_coin::block_hash const & frontier_a)
{
	auto account_l (account (frontier_a));
    rollback_visitor rollback (*this);
    mu_coin::block_hash latest;
	do
	{
		auto latest_error (store.latest_get (account_l, latest));
		assert (!latest_error);
        auto block (store.block_get (latest));
        block->visit (rollback);
		
	} while (latest != frontier_a);
}

mu_coin::address mu_coin::ledger::account (mu_coin::block_hash const & hash_a)
{
	account_visitor account (store);
	account.compute (hash_a);
	return account.result;
}

mu_coin::uint256_t mu_coin::ledger::amount (mu_coin::block_hash const & hash_a)
{
	amount_visitor amount (store);
	amount.compute (hash_a);
	return amount.result;
}

void mu_coin::ledger::move_representation (mu_coin::address const & source_a, mu_coin::address const & destination_a, mu_coin::uint256_t const & amount_a)
{
	auto source_previous (store.representation_get (source_a));
	assert (source_previous >= amount_a);
    store.representation_put (source_a, source_previous - amount_a);
    store.representation_put (destination_a, store.representation_get (destination_a) + amount_a);
}

mu_coin::block_hash mu_coin::ledger::latest (mu_coin::address const & address_a)
{
	mu_coin::block_hash latest;
	auto latest_error (store.latest_get (address_a, latest));
	assert (!latest_error);
	return latest;
}

void mu_coin::block_store::latest_del (mu_coin::address const & address_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data;
    int error (addresses.del (nullptr, &key.data, 0));
    assert (error == 0);
}

void mu_coin::processor::confirm_ack (std::unique_ptr <mu_coin::confirm_ack> message_a, mu_coin::endpoint const & sender_a)
{
	std::unique_lock <std::mutex> lock (mutex);
	auto session (confirm_listeners.find (message_a->session));
	if (session != confirm_listeners.end ())
	{
		lock.unlock ();
		session->second (std::unique_ptr <mu_coin::message> {message_a.release ()}, sender_a);
	}
}

void mu_coin::processor::confirm_nak (std::unique_ptr <mu_coin::confirm_nak> message_a, mu_coin::endpoint const & sender_a)
{
	std::unique_lock <std::mutex> lock (mutex);
	auto session (confirm_listeners.find (message_a->session));
	if (session != confirm_listeners.end ())
	{
		lock.release ();
		session->second (std::unique_ptr <mu_coin::message> {message_a.release ()}, sender_a);
	}
}

mu_coin::uint256_union mu_coin::confirm_ack::hash () const
{
	mu_coin::uint256_union result;
    CryptoPP::SHA3 hash (32);
    hash.Update (session.bytes.data (), sizeof (session.bytes));
    hash.Update (address.bytes.data (), sizeof (address.bytes));
    hash.Final (result.bytes.data ());
	return result;
}

mu_coin::uint256_union mu_coin::confirm_nak::hash () const
{
	mu_coin::uint256_union result;
    CryptoPP::SHA3 hash (32);
    hash.Update (session.bytes.data (), sizeof (session.bytes));
    hash.Update (address.bytes.data (), sizeof (address.bytes));
    block->hash (hash);
    hash.Final (result.bytes.data ());
	return result;
}

mu_coin::uint256_union mu_coin::block::hash () const
{
    CryptoPP::SHA3 hash_l (32);
    hash (hash_l);
    mu_coin::uint256_union result;
    hash_l.Final (result.bytes.data ());
    return result;
}

namespace
{
    char const * base58_lookup ("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
    char const * base58_reverse ("~012345678~~~~~~~9:;<=>?@~ABCDE~FGHIJKLMNOP~~~~~~QRSTUVWXYZ[~\\]^_`abcdefghi");
    char base58_encode (uint8_t value)
    {
        assert (value < 58);
        auto result (base58_lookup [value]);
        return result;
    }
    uint8_t base58_decode (char value)
    {
        auto result (base58_reverse [value - 0x30] - 0x30);
        return result;
    }
}

void mu_coin::uint256_union::encode_base58check (std::string & destination_a) const
{
    assert (destination_a.empty ());
    destination_a.reserve (50);
    uint32_t check;
    CryptoPP::SHA3 hash (4);
    hash.Update (bytes.data (), sizeof (bytes));
    hash.Final (reinterpret_cast <uint8_t *> (&check));
    mu_coin::uint512_t number_l (number ());
    number_l |= mu_coin::uint512_t (check) << 256;
    number_l |= mu_coin::uint512_t (13) << (256 + 32);
    while (!number_l.is_zero ())
    {
        auto r ((number_l % 58).convert_to <uint8_t> ());
        number_l /= 58;
        destination_a.push_back (base58_encode (r));
    }
    std::reverse (destination_a.begin (), destination_a.end ());
}

bool mu_coin::uint256_union::decode_base58check (std::string const & source_a)
{
    auto result (source_a.size () != 50);
    if (!result)
    {
        mu_coin::uint512_t number_l;
        for (auto i (source_a.begin ()), j (source_a.end ()); !result && i != j; ++i)
        {
            uint8_t byte (base58_decode (*i));
            result = byte == '~';
            if (!result)
            {
                number_l *= 58;
                number_l += byte;
            }
        }
        if (!result)
        {
            *this = number_l.convert_to <mu_coin::uint256_t> ();
            uint32_t check ((number_l >> 256).convert_to <uint32_t> ());
            result = (number_l >> (256 + 32)) != 13;
            if (!result)
            {
                uint32_t validation;
                CryptoPP::SHA3 hash (4);
                hash.Update (bytes.data (), sizeof (bytes));
                hash.Final (reinterpret_cast <uint8_t *> (&validation));
                result = check != validation;
            }
        }
    }
    return result;
}

namespace
{
bool parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
    auto result (false);
    auto port_position (string.rfind (':'));
    if (port_position != std::string::npos && port_position > 0)
    {
        std::string port_string (string.substr (port_position + 1));
        try
        {
            size_t converted;
            auto port (std::stoul (port_string, &converted));
            if (port <= std::numeric_limits <uint16_t>::max () && converted == port_string.size ())
            {
                boost::system::error_code ec;
                auto address (boost::asio::ip::address_v4::from_string (string.substr (0, port_position), ec));
                if (ec == 0)
                {
                    address_a = address;
                    port_a = port;
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
        catch (...)
        {
            result = true;
        }
    }
    else
    {
        result = true;
    }
    return result;
}
}

bool mu_coin::parse_endpoint (std::string const & string, mu_coin::endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = mu_coin::endpoint (address, port);
    }
    return result;
}

bool mu_coin::parse_tcp_endpoint (std::string const & string, mu_coin::tcp_endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = mu_coin::tcp_endpoint (address, port);
    }
    return result;
}

void mu_coin::bulk_req::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.bulk_req (*this);
}

bool mu_coin::bulk_req::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
    if (!result)
    {
        assert (type == mu_coin::message_type::bulk_req);
        result = read (stream_a, start);
        if (!result)
        {
            result = read (stream_a, end);
        }
    }
    return result;
}

void mu_coin::bulk_req::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::bulk_req);
    write (stream_a, start);
    write (stream_a, end);
}

void mu_coin::client::start ()
{
    rpc.listen ();
    network.receive ();
    processor.ongoing_keepalive ();
}

void mu_coin::processor::bootstrap (boost::asio::ip::tcp::endpoint const & endpoint_a, std::function <void ()> const & complete_action_a)
{
    auto processor (std::make_shared <mu_coin::bootstrap_processor> (client, complete_action_a));
    processor->run (endpoint_a);
}

mu_coin::bootstrap::bootstrap (boost::asio::io_service & service_a, uint16_t port_a, mu_coin::client & client_a) :
acceptor (service_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::any (), port_a)),
service (service_a),
client (client_a)
{
}

void mu_coin::bootstrap::accept ()
{
    acceptor.open (local.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (local);
    acceptor.listen ();
    accept_connection ();
}

void mu_coin::bootstrap::stop ()
{
    on = false;
}

void mu_coin::bootstrap::accept_connection ()
{
    auto socket (std::make_shared <boost::asio::ip::tcp::socket> (service));
    acceptor.async_accept (*socket, [this, socket] (boost::system::error_code const & error) {accept_action (error, socket); accept_connection ();});
}

void mu_coin::bootstrap::accept_action (boost::system::error_code const & ec, std::shared_ptr <boost::asio::ip::tcp::socket> socket_a)
{
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (socket_a, client));
    connection->receive ();
}

mu_coin::bootstrap_connection::bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket> socket_a, mu_coin::client & client_a) :
socket (socket_a),
client (client_a)
{
}

void mu_coin::bootstrap_connection::receive ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->receive_type_action (ec, size_a);});
}

void mu_coin::bootstrap_connection::receive_type_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
        mu_coin::bufferstream type_stream (receive_buffer.data (), size_a);
        mu_coin::message_type type;
        read (type_stream, type);
        switch (type)
        {
            case mu_coin::message_type::bulk_req:
            {
                auto this_l (shared_from_this ());
                boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 1, 32 + 32), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->receive_req_action (ec, size_a);});
                break;
            }
            case mu_coin::message_type::bulk_fin:
            {
                break;
            }
            default:
            {
                break;
            }
        }
    }
    else
    {
        receive ();
    }
}

void mu_coin::bootstrap_connection::receive_req_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        mu_coin::bulk_req request;
        mu_coin::bufferstream stream (receive_buffer.data (), 32 + 32 + 1);
        auto error (request.deserialize (stream));
        if (!error)
        {
            receive ();
            std::pair <mu_coin::block_hash, mu_coin::block_hash> pair;
            auto error (process_bulk_req (request, pair));
            if (!error)
            {
                if (network_debug)
                {
                    std::cerr << "Sending: " << request.start.to_string () << " down to: " << request.end.to_string () << std::endl;
                }
                auto startup (requests.empty ());
                requests.push (pair);
                if (startup)
                {
                    send_next ();
                }
            }
            else
            {
                if (network_debug)
                {
                    std::cerr << "Malformed request, address: " << request.start.to_string () << " does not own block: " << request.end.to_string ();
                }
            }
        }
    }
}

bool mu_coin::bootstrap_connection::process_bulk_req (mu_coin::bulk_req const & request, std::pair <mu_coin::block_hash, mu_coin::block_hash> & result_a)
{
    auto result (false);
    auto end_exists (request.end.is_zero () || client.store.block_exists (request.end));
    if (end_exists)
    {
        mu_coin::block_hash hash;
        auto no_address (client.store.latest_get (request.start, hash));
        if (no_address)
        {
            result_a.first = request.end;
            result_a.second = request.end;
        }
        else
        {
            if (!request.end.is_zero ())
            {
                account_visitor visitor (client.store);
                visitor.compute (request.end);
                if (visitor.result == request.start)
                {
                    result_a.first = hash;
                    result_a.second = request.end;
                }
                else
                {
                    result = true;
                }
            }
            else
            {
                result_a.first = hash;
                result_a.second = request.end;
            }
        }
    }
    else
    {
        result_a.first = request.end;
        result_a.second = request.end;
    }
    return result;
}

void mu_coin::bootstrap_connection::send_next ()
{
    std::unique_ptr <mu_coin::block> block (get_next ());
    if (block != nullptr)
    {
        {
            send_buffer.clear ();
            mu_coin::vectorstream stream (send_buffer);
            mu_coin::serialize_block (stream, *block);
        }
        auto this_l (shared_from_this ());
        if (network_debug)
        {
            std::cerr << "Sending block: " << block->hash ().to_string () << std::endl;
        }
        async_write (*socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->sent_action (ec, size_a);});
    }
    else
    {
        send_finished ();
    }
}

std::unique_ptr <mu_coin::block> mu_coin::bootstrap_connection::get_next ()
{
    std::unique_ptr <mu_coin::block> result;
    assert (!requests.empty ());
    auto & front (requests.front ());
    if (front.first != front.second)
    {
        result = client.store.block_get (front.first);
        assert (result != nullptr);
        auto previous (result->previous ());
        if (!previous.is_zero ())
        {
            front.first = previous;
        }
        else
        {
            front.second = front.first;
        }
    }
    return result;
}

void mu_coin::bootstrap_connection::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
}

void mu_coin::bootstrap_connection::send_finished ()
{
    send_buffer.clear ();
    send_buffer.push_back (static_cast <uint8_t> (mu_coin::block_type::not_a_block));
    auto this_l (shared_from_this ());
    if (network_debug)
    {
        std::cerr << "Sending finished" << std::endl;
    }
    requests.pop ();
    async_write (*socket, boost::asio::buffer (send_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->no_block_sent (ec, size_a);});
}

void mu_coin::bootstrap_connection::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
        if (!requests.empty ())
        {
            send_next ();
        }
    }
}

mu_coin::account_iterator mu_coin::block_store::latest_begin (mu_coin::address const & address_a)
{
    Dbc * cursor;
    addresses.cursor (0, &cursor, 0);
    mu_coin::account_iterator result (cursor, address_a);
    return result;
}

mu_coin::account_iterator::account_iterator (Dbc * cursor_a, mu_coin::address const & address_a) :
cursor (cursor_a)
{
    key = address_a;
    auto result (cursor->get (&key.data, &data.data, DB_SET_RANGE));
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
}

void mu_coin::bootstrap_processor::run (boost::asio::ip::tcp::endpoint const & endpoint_a)
{
    auto this_l (shared_from_this ());
    socket.async_connect (endpoint_a, [this_l] (boost::system::error_code const & ec) {this_l->connect_action (ec);});
}

mu_coin::bootstrap_processor::bootstrap_processor (mu_coin::client & client_a, std::function <void ()> const & complete_action_a) :
iterator (client_a.store),
client (client_a),
socket (client_a.network.service),
complete_action (complete_action_a)
{
}

void mu_coin::bootstrap_processor::connect_action (boost::system::error_code const & ec)
{
    if (!ec)
    {
        receive_block ();
        fill_queue ();
    }
}

void mu_coin::bootstrap_processor::send_request (std::pair <mu_coin::address, mu_coin::block_hash> const & value)
{
    assert (!value.first.is_zero ());
    mu_coin::bulk_req request;
    request.start = value.first;
    request.end = value.second;
    expecting = request.start;
    requests.push (value);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        mu_coin::vectorstream stream (*bytes);
        request.serialize (stream);
    }
    auto this_l (shared_from_this ());
    if (network_debug)
    {
        std::cerr << "Requesting: " << request.start.to_string () << " down to: " << request.end.to_string () << std::endl;
    }
    boost::asio::async_write (socket, boost::asio::buffer (bytes->data (), bytes->size ()), [bytes, this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->send_action (ec, size_a);});
}

void mu_coin::bootstrap_processor::send_action (boost::system::error_code const & ec, size_t size_a)
{
    fill_queue ();
}

void mu_coin::bootstrap_processor::fill_queue ()
{
    if (requests.size () < max_queue_size)
    {
        ++iterator;
        if (!iterator.current.first.is_zero ())
        {
            send_request (iterator.current);
        }
    }
}

void mu_coin::bootstrap_processor::receive_block ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (socket, boost::asio::buffer (buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_type (ec, size_a);});
}

void mu_coin::bootstrap_processor::received_type (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        if (!requests.empty ())
        {
            auto this_l (shared_from_this ());
            mu_coin::block_type type (static_cast <mu_coin::block_type> (buffer [0]));
            switch (type)
            {
                case mu_coin::block_type::send:
                {
                    boost::asio::async_read (socket, boost::asio::buffer (buffer.data () + 1, 64 + 32 + 32 + 32), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                    break;
                }
                case mu_coin::block_type::receive:
                {
                    boost::asio::async_read (socket, boost::asio::buffer (buffer.data () + 1, 64 + 32 + 32), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                    break;
                }
                case mu_coin::block_type::open:
                {
                    boost::asio::async_read (socket, boost::asio::buffer (buffer.data () + 1, 32 + 32 + 64), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                    break;
                }
                case mu_coin::block_type::change:
                {
                    boost::asio::async_read (socket, boost::asio::buffer (buffer.data () + 1, 32 + 32 + 64), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                    break;
                }
                case mu_coin::block_type::not_a_block:
                {
                    auto error (process_end ());
					if (!error)
					{
                        if (!requests.empty ())
                        {
                            receive_block ();
                        }
                        else
                        {
                            if (network_debug)
                            {
                                std::cerr << "Exiting bootstrap processor" << std::endl;
                            }
                        }
					}
					else
					{
						if (network_debug)
						{
							std::cerr << "Error processing end block" << std::endl;
						}
					}
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }
}

namespace
{
class observed_visitor : public mu_coin::block_visitor
{
public:
    observed_visitor () :
    address (0)
    {
    }
    void send_block (mu_coin::send_block const & block_a)
    {
        address = block_a.hashables.destination;
    }
    void receive_block (mu_coin::receive_block const &)
    {
    }
    void open_block (mu_coin::open_block const &)
    {
    }
    void change_block (mu_coin::change_block const &)
    {
    }
    mu_coin::address address;
};
}

bool mu_coin::bootstrap_processor::process_end ()
{
    bool result;
    if (expecting == requests.front ().second)
    {
        mu_coin::process_result processing;
        std::unique_ptr <mu_coin::block> block;
        do
        {
            block = client.store.bootstrap_get (expecting);
            if (block != nullptr)
            {
                processing = client.ledger.process (*block);
                if (processing == mu_coin::process_result::progress)
                {
                    iterator.observed_block (*block);
                }
                expecting = block->hash ();
            }
        } while (block != nullptr && processing == mu_coin::process_result::progress);
        result = processing != mu_coin::process_result::progress;
    }
    else if (expecting == requests.front ().first)
    {
        result = false;
    }
    else
    {
        result = true;
    }
    requests.pop ();
    fill_queue ();
    return result;
}

void mu_coin::bootstrap_iterator::observed_block (mu_coin::block const & block_a)
{
    observed_visitor visitor;
    block_a.visit (visitor);
    if (!visitor.address.is_zero ())
    {
        mu_coin::block_hash hash;
        if (store.latest_get (visitor.address, hash))
        {
            observed.insert (visitor.address);
        }
    }
}

mu_coin::block_hash mu_coin::genesis::hash () const
{
    return open.hash ();
}

void mu_coin::bootstrap_processor::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		mu_coin::bufferstream stream (buffer.data (), 1 + size_a);
		auto block (mu_coin::deserialize_block (stream));
		if (block != nullptr)
		{
			auto error (process_block (*block));
			if (!error)
			{
				receive_block ();
			}
		}
	}
}

bool mu_coin::bootstrap_processor::process_block (mu_coin::block const & block)
{
    assert (!requests.empty ());
    bool result;
    auto hash (block.hash ());
    if (network_debug)
    {
        std::cerr << "Received block: " << hash.to_string () << std::endl;
    }
    if (expecting != requests.front ().second && (expecting == requests.front ().first || hash == expecting))
    {
        auto previous (block.previous ());
        client.store.bootstrap_put (previous, block);
        expecting = previous;
        if (network_debug)
        {
            std::cerr << "Expecting: " << expecting.to_string () << std::endl;
        }
        result = false;
    }
    else
    {
		if (network_debug)
		{
			std::cerr << "Block hash: " << hash.to_string () << " did not match expecting: " << expecting.to_string () << std::endl;
		}
        result = true;
    }
    return result;
}

bool mu_coin::block_store::block_exists (mu_coin::block_hash const & hash_a)
{
    dbt key (hash_a);
    dbt data;
    bool result;
    int error (blocks.get (nullptr, &key.data, &data.data, 0));
    if (error == DB_NOTFOUND)
    {
        result = false;
    }
    else
    {
        result = true;
    }
    return result;
}

void mu_coin::bootstrap_processor::stop_blocks ()
{
    
}

void mu_coin::block_store::bootstrap_put (mu_coin::block_hash const & hash_a, mu_coin::block const & block_a)
{
    dbt key (hash_a);
    dbt data (block_a);
    int error (bootstrap.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::bootstrap_get (mu_coin::block_hash const & hash_a)
{
    dbt key (hash_a);
    dbt data;
    int error (bootstrap.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    auto result (data.block ());
    return result;
}

void mu_coin::block_store::bootstrap_del (mu_coin::block_hash const & hash_a)
{
    dbt key (hash_a);
    int error (bootstrap.del (nullptr, &key.data, 0));
    assert (error == 0);
}

mu_coin::endpoint mu_coin::network::endpoint ()
{
    return mu_coin::endpoint (boost::asio::ip::address_v4::loopback (), socket.local_endpoint ().port ());
}

boost::asio::ip::tcp::endpoint mu_coin::bootstrap::endpoint ()
{
    return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), local.port ());
}

bool mu_coin::uint256_union::is_zero () const
{
    return qwords [0] == 0 && qwords [1] == 0 && qwords [2] == 0 && qwords [3] == 0;
}

std::string mu_coin::uint256_union::to_string () const
{
    std::string result;
    encode_hex (result);
    return result;
}

bool mu_coin::uint256_union::operator < (mu_coin::uint256_union const & other_a) const
{
    return number () < other_a.number ();
}

mu_coin::bootstrap_iterator::bootstrap_iterator (mu_coin::block_store & store_a) :
store (store_a),
current (std::make_pair (0, 0)),
store_address (0)
{
}

mu_coin::bootstrap_iterator & mu_coin::bootstrap_iterator::operator ++ ()
{
    auto next (store.latest_begin (store_address.number () + 1));
    if (!observed.empty () && next != store.latest_end ())
    {
        if (next.key.uint256 () < *observed.begin ())
        {
            current.first = next.key.uint256 ();
            current.second = next.data.uint256 ();
            store_address = current.first;
        }
        else
        {
            current.first = *observed.begin ();
            current.second = 0;
            observed.erase (current.first);
        }
    }
    else if (!observed.empty ())
    {
        current.first = *observed.begin ();
        current.second = 0;
        observed.erase (current.first);
    }
    else if (next != store.latest_end ())
    {
        current.first = next.key.uint256 ();
        current.second = next.data.uint256 ();
        store_address = current.first;
    }
    else
    {
        current.first = 0;
    }
    return *this;
}

void mu_coin::system::generate_transaction (uint32_t amount)
{
    assert (!clients.empty ());
    auto max (clients.size () - 1);
    for (auto i (clients.begin ()), j (clients.end ()); i != j; ++i)
    {
        mu_coin::keypair key;
        (*i)->wallet.insert (key.prv, (*i)->wallet.password);
    }
    for (uint32_t i (0); i < amount; ++i)
    {
        uint32_t source (random_pool.GenerateWord32 (0, max));
        uint32_t destination;
        do
        {
            destination = random_pool.GenerateWord32 (0, max);
        } while (source == destination);
    }
}

mu_coin::bootstrap_processor::~bootstrap_processor ()
{
    complete_action ();
    if (network_debug)
    {
        std::cerr << "Exiting bootstrap processor" << std::endl;
    }
}

mu_coin::bootstrap_connection::~bootstrap_connection ()
{
    if (network_debug)
    {
        std::cerr << "Exiting bootstrap connection" << std::endl;
    }
}

void mu_coin::peer_container::random_fill (std::array <mu_coin::endpoint, 24> & target_a)
{
    auto peers (list ());
    while (peers.size () > target_a.size ())
    {
        auto index (random_pool.GenerateWord32 (0, peers.size ()));
        peers [index] = peers [peers.size () - 1];
        peers.pop_back ();
    }
    auto k (target_a.begin ());
    for (auto i (peers.begin ()), j (peers.begin () + std::min (peers.size (), target_a.size ())); i != j; ++i, ++k)
    {
        *k = i->endpoint;
    }
    std::fill (target_a.begin () + std::min (peers.size (), target_a.size ()), target_a.end (), mu_coin::endpoint ());
}

void mu_coin::processor::ongoing_keepalive ()
{
    auto period (std::chrono::seconds (10));
    auto cutoff (period * 5);
    auto peers (client.peers.purge_list (std::chrono::system_clock::now () - cutoff));
    for (auto i (peers.begin ()), j (peers.end ()); i != j && std::chrono::system_clock::now () - i->last_attempt > period; ++i)
    {
        client.network.send_keepalive (i->endpoint);
    }
    client.service.add (std::chrono::system_clock::now () + period, [this] () { ongoing_keepalive ();});
}

std::vector <mu_coin::peer_information> mu_coin::peer_container::purge_list (std::chrono::system_clock::time_point const & cutoff)
{
    std::unique_lock <std::mutex> lock (mutex);
    auto pivot (peers.get <1> ().lower_bound (cutoff));
    std::vector <mu_coin::peer_information> result (pivot, peers.get <1> ().end ());
    peers.get <1> ().erase (peers.get <1> ().begin (), pivot);
    return result;
}

size_t mu_coin::peer_container::size ()
{
    std::unique_lock <std::mutex> lock (mutex);
    return peers.size ();
}

bool mu_coin::peer_container::empty ()
{
    return size () == 0;
}

bool mu_coin::peer_container::contacting_peer (mu_coin::endpoint const & endpoint_a)
{
	auto result (mu_coin::reserved_address (endpoint_a));
	if (!result)
	{
		if (endpoint_a != self)
		{
			std::unique_lock <std::mutex> lock (mutex);
			auto existing (peers.find (endpoint_a));
			if (existing != peers.end ())
			{
				result = true;
			}
			else
			{
				peers.insert ({endpoint_a, std::chrono::system_clock::time_point (), std::chrono::system_clock::now ()});
			}
		}
		else
		{
			if (network_debug)
			{
				std::cerr << "Ignoring endpoint of self" << std::endl;
			}
		}
	}
	return result;
}

bool mu_coin::reserved_address (mu_coin::endpoint const & endpoint_a)
{
	auto bytes (endpoint_a.address ().to_v4().to_ulong ());
	auto result (false);
	if (bytes <= 0x00ffffffu)
	{
		result = true;
	}
	else if (bytes >= 0xc0000200ul && bytes <= 0xc00002fful)
	{
		result = true;
	}
	else if (bytes >= 0xc6336400ul && bytes <= 0xc63364fful)
	{
		result = true;
	}
	else if (bytes >= 0xcb007100ul && bytes <= 0xcb0071fful)
	{
		result = true;
	}
	else if (bytes >= 0xe9fc0000ul && bytes <= 0xe9fc00fful)
	{
		result = true;
	}
	else if (bytes >= 0xf0000000ul)
	{
		result = true;
	}
	return result;
}

mu_coin::peer_container::peer_container (mu_coin::endpoint const & self_a) :
self (self_a)
{
}