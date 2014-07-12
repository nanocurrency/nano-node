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

static bool const network_debug = false;

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
pending (nullptr, 0),
representation (nullptr, 0),
forks (nullptr, 0)
{
    boost::filesystem::create_directories (path_a);
    addresses.open (nullptr, (path_a / "addresses.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    blocks.open (nullptr, (path_a / "blocks.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    pending.open (nullptr, (path_a / "pending.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    representation.open (nullptr, (path_a / "representation.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
    forks.open (nullptr, (path_a / "forks.bdb").native ().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
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

void mu_coin::block_store::genesis_put (mu_coin::public_key const & key_a, uint256_t const & coins_a)
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
    mu_coin::open_block open;
    open.hashables.source = send2.hash ();
    open.hashables.representative = key_a;
    open.signature.clear ();
    block_put (open.hash (), open);
    latest_put (key_a, open.hash ());
    representation_put (key_a, coins_a);
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
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        mu_coin::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_debug)
    {
        std::cerr << "Keepalive " << std::to_string (socket.local_endpoint().port ()) << "->" << std::to_string (endpoint_a.port ()) << std::endl;
    }
    socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), endpoint_a, [bytes] (boost::system::error_code const &, size_t) {});
    client.peers.add_peer (endpoint_a);
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
    client.peers.add_peer (endpoint_a);
}

void mu_coin::network::confirm_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <mu_coin::block> block)
{
    mu_coin::confirm_req message (std::move (block));
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
            mu_coin::bufferstream type_stream (buffer.data (), size_a);
            mu_coin::message_type type;
            read (type_stream, type);
            switch (type)
            {
                case mu_coin::message_type::keepalive_req:
                {
                    ++keepalive_req_count;
                    receive ();
                    mu_coin::keepalive_ack message;
                    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
                    {
                        mu_coin::vectorstream stream (*bytes);
                        message.serialize (stream);
                    }
                    socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), sender, [bytes] (boost::system::error_code const & error, size_t size_a) {});
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
                    mu_coin::bufferstream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    if (!error)
                    {
                        if (network_debug)
                        {
                            std::cerr << "Publish req" << std::to_string (socket.local_endpoint().port ()) << "<-" << std::to_string (sender.port ()) << std::endl;
                        }
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
                                std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
                                {
                                    mu_coin::vectorstream stream (*bytes);
                                    outgoing.serialize (stream);
                                }
                                socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), sender, [bytes] (boost::system::error_code const & error, size_t size_a) {});
                                break;
                            }
                            case mu_coin::process_result::bad_signature:
                            case mu_coin::process_result::overspend:
                            case mu_coin::process_result::overreceive:
                            case mu_coin::process_result::not_receive_from_send:
                            {
                                // None of these affect the integrity of the ledger since they're all ignored
                                mu_coin::publish_err outgoing;
                                std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
                                {
                                    mu_coin::vectorstream stream (*bytes);
                                    outgoing.serialize (stream);
                                }
                                socket.async_send_to (boost::asio::buffer (bytes->data (), bytes->size ()), sender, [bytes] (boost::system::error_code const & error, size_t size_a) {});
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
                    mu_coin::bufferstream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    break;
                }
                case mu_coin::message_type::publish_err:
                {
                    ++publish_err_count;
                    auto incoming (new mu_coin::publish_err);
                    mu_coin::bufferstream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    break;
                }
                case mu_coin::message_type::publish_nak:
                {
                    ++publish_nak_count;
                    auto incoming (new mu_coin::publish_nak);
                    mu_coin::bufferstream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
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

mu_coin::processor::processor (mu_coin::processor_service & service_a, mu_coin::client & client_a) :
service (service_a),
client (client_a)
{
}

bool mu_coin::operation::operator < (mu_coin::operation const & other_a) const
{
    return wakeup < other_a.wakeup;
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, boost::filesystem::path const & wallet_path_a, boost::filesystem::path const & block_store_path_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a) :
representative (representative_a),
store (block_store_path_a),
ledger (store),
wallet (0, wallet_path_a),
network (*service_a, port_a, *this),
rpc (service_a, pool_a, command_port_a, *this),
processor (processor_a, *this)
{
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a) :
client (service_a, pool_a, port_a, command_port_a, boost::filesystem::unique_path (), boost::filesystem::unique_path (), processor_a, representative_a)
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
    void open_block (mu_coin::open_block const & block_a)
    {
        result = client.ledger.process (block_a);
    }
    void change_block (mu_coin::change_block const & block_a)
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
    mu_coin::uint256_t weight (0);
    if (client.wallet.find (client.representative) != client.wallet.end ())
    {
        weight = client.ledger.weight (client.representative);
    }
    process_acknowledged (weight);
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
                        balance_visitor visitor (client.ledger.store);
                        visitor.compute (send.hashables.previous);
                        auto open (new mu_coin::open_block);
                        open->hashables.source = hash;
                        open->hashables.representative = client.representative;
                        mu_coin::sign_message (prv, send.hashables.destination, open->hash (), open->signature);
                        prv.bytes.fill (0);
                        client.processor.publish (std::unique_ptr <mu_coin::block> (open), mu_coin::endpoint {});
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
                        client.processor.publish (std::unique_ptr <mu_coin::block> (receive), mu_coin::endpoint {});
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
    if (!mu_coin::validate_message (message.address, message.block, message.signature))
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
    auto block (message.winner->hash ());
    if (!mu_coin::validate_message (message.address, block, message.signature))
    {
        auto weight (processor.client.ledger.weight (message.address));
        processor.nacked += weight;
    }
    else
    {
        // Signature didn't match.
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

void mu_coin::publish_ack::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::publish_ack);
    write (stream_a, block);
}

bool mu_coin::publish_ack::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
    assert (type == mu_coin::message_type::publish_ack);
    result = read (stream_a, block);
    return result;
}

void mu_coin::keepalive_ack::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::keepalive_ack);
}

bool mu_coin::keepalive_ack::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
    assert (type == mu_coin::message_type::keepalive_ack);
    return result;
}

void mu_coin::keepalive_req::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::keepalive_req);
}

bool mu_coin::keepalive_req::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
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

mu_coin::system::system (size_t threads_a, uint16_t port_a, uint16_t command_port_a, size_t count_a, mu_coin::public_key const & address, mu_coin::uint256_t const & amount) :
service (new boost::asio::io_service),
pool (new boost::network::utils::thread_pool (threads_a))
{
    clients.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        clients.push_back (std::unique_ptr <mu_coin::client> (new mu_coin::client (service, pool, port_a + i, command_port_a + i, processor, address)));
        clients.back ()->store.genesis_put (address, amount);
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
    mu_coin::private_key prv;
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    if (client.wallet.fetch (client.representative, client.wallet.password, prv))
    {
        mu_coin::confirm_unk outgoing;
        outgoing.rep_hint = client.representative;
        outgoing.block = hash;
        {
            mu_coin::vectorstream stream (*bytes);
            outgoing.serialize (stream);
        }
    }
    else
    {
        mu_coin::confirm_ack outgoing {hash};
        outgoing.address = client.representative;
        outgoing.block = hash;
        mu_coin::sign_message (prv, client.representative, hash, outgoing.signature);
        assert (!mu_coin::validate_message (client.representative, hash, outgoing.signature));
        {
            mu_coin::vectorstream stream (*bytes);
            outgoing.serialize (stream);
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
        result = read (stream_a, block);
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
    write (stream_a, block);
    write (stream_a, address);
    write (stream_a, signature);
}

mu_coin::confirm_ack::confirm_ack (mu_coin::uint256_union const & block_a) :
block (block_a)
{
}

void mu_coin::publish_nak::serialize (mu_coin::stream & stream_a)
{
    assert (conflict != nullptr);
    write (stream_a, mu_coin::message_type::publish_nak);
    write (stream_a, block);
    mu_coin::serialize_block (stream_a, *conflict);
}

bool mu_coin::confirm_ack::operator == (mu_coin::confirm_ack const & other_a) const
{
    auto result (block == other_a.block && address == other_a.address && signature == other_a.signature);
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
    auto result (read (stream_a, block));
    if (!result)
    {
        winner = mu_coin::deserialize_block (stream_a);
        result = winner == nullptr;
        if (!result)
        {
            result = read (stream_a, loser);
            if (!result)
            {
                result = read (stream_a, address);
                if (!result)
                {
                    result = read (stream_a, signature);
                }
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
    block = mu_coin::deserialize_block (stream_a);
    auto result (block == nullptr);
    return result;
}

bool mu_coin::confirm_unk::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    read (stream_a, type);
    assert (type == mu_coin::message_type::confirm_unk);
    auto result (read (stream_a, block));
    return result;
}

bool mu_coin::publish_nak::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    read (stream_a, type);
    assert (type == mu_coin::message_type::publish_nak);
    auto result (read (stream_a, block));
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

void mu_coin::confirm_req::serialize (mu_coin::stream & stream_a)
{
    assert (block != nullptr);
    write (stream_a, mu_coin::message_type::confirm_req);
    mu_coin::serialize_block (stream_a, *block);
}

mu_coin::confirm_req::confirm_req (std::unique_ptr <mu_coin::block> block_a) :
block (std::move (block_a))
{
}

void mu_coin::publish_err::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::publish_err);
    write (stream_a, block);
}

void mu_coin::publish_err::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.publish_err (*this);
}

bool mu_coin::publish_err::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    read (stream_a, type);
    assert (type == mu_coin::message_type::publish_err);
    auto result (read (stream_a, block));
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
            else if (action == "wallet_create")
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


mu_coin::uint256_union mu_coin::open_block::hash () const
{
    return hashables.hash ();
}

mu_coin::block_hash mu_coin::open_block::previous () const
{
    assert (false);
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

mu_coin::uint256_union mu_coin::open_hashables::hash () const
{
    mu_coin::uint256_union result;
    CryptoPP::SHA256 hash;
    hash.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash.Update (source.bytes.data (), sizeof (source.bytes));
    hash.Final (result.bytes.data ());
    return result;
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
    write (stream_a, block);
}

mu_coin::uint256_union mu_coin::change_block::hash () const
{
    return hashables.hash ();
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

mu_coin::uint256_union mu_coin::change_hashables::hash () const
{
    mu_coin::uint256_union result;
    CryptoPP::SHA256 hash;
    hash.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash.Update (previous.bytes.data (), sizeof (previous.bytes));
    hash.Final (result.bytes.data ());
    return result;
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
        assert (false);
    }
    void receive_block (mu_coin::receive_block const & block_a) override
    {
        assert (false);
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
    mu_coin::block_hash latest;
    auto latest_error (store.latest_get (account_l, latest));
    assert (!latest_error);
    rollback_visitor rollback (*this);
    while (latest != frontier_a)
    {
        auto block (store.block_get (latest));
        block->visit (rollback);
        auto latest_error (store.latest_get (account_l, latest));
        assert (!latest_error);
    }
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