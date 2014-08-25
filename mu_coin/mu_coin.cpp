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
    bool constexpr ledger_logging ()
    {
        return true;
    }
    bool constexpr ledger_duplicate_logging ()
    {
        return ledger_logging () && false;
    }
    bool constexpr network_logging ()
    {
        return true;
    }
    bool constexpr network_message_logging ()
    {
        return network_logging () && false;
    }
    bool constexpr network_publish_logging ()
    {
        return network_logging () && false;
    }
    bool constexpr network_packet_logging ()
    {
        return network_logging () && false;
    }
    bool constexpr network_keepalive_logging ()
    {
        return network_logging () && false;
    }
}

CryptoPP::AutoSeededRandomPool random_pool;
std::chrono::seconds constexpr mu_coin::processor::period;
std::chrono::seconds constexpr mu_coin::processor::cutoff;

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

mu_coin::uint256_union & mu_coin::uint256_union::operator = (leveldb::Slice const & slice_a)
{
    assert (slice_a.size () == 32);
    mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (slice_a.data ()), slice_a.size ());
    auto error (deserialize (stream));
    assert (!error);
    return *this;
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
    mu_coin::frontier frontier;
    auto none (store.latest_get (address_a, frontier));
    if (!none)
    {
        balance_visitor visitor (store);
        visitor.compute (frontier.hash);
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
    store.checksum_put (0, 0, 0);
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
        result = previous != nullptr ? mu_coin::process_result::progress : mu_coin::process_result::gap_previous;  // Have we seen the previous block before? (Harmless)
        if (result == mu_coin::process_result::progress)
        {
			auto account (ledger.account (block_a.hashables.previous));
            mu_coin::frontier frontier;
            auto latest_error (ledger.store.latest_get (account, frontier));
            assert (!latest_error);
            result = validate_message (account, message, block_a.signature) ? mu_coin::process_result::bad_signature : mu_coin::process_result::progress; // Is this block signed correctly (Malformed)
            if (result == mu_coin::process_result::progress)
            {
                result = frontier.hash == block_a.hashables.previous ? mu_coin::process_result::progress : mu_coin::process_result::fork; // Is the previous block the latest (Malicious)
                if (result == mu_coin::process_result::progress)
                {
					ledger.move_representation (ledger.representative (block_a.hashables.previous), block_a.hashables.representative, ledger.balance (block_a.hashables.previous));
                    ledger.store.block_put (message, block_a);
                    ledger.change_latest (account, message);
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
        result = previous != nullptr ? mu_coin::process_result::progress : mu_coin::process_result::gap_previous; // Have we seen the previous block before? (Harmless)
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
                    mu_coin::frontier frontier;
                    auto latest_error (ledger.store.latest_get (account, frontier));
                    assert (!latest_error);
                    result = frontier.hash == block_a.hashables.previous ? mu_coin::process_result::progress : mu_coin::process_result::fork;
                    if (result == mu_coin::process_result::progress)
                    {
                        ledger.store.block_put (message, block_a);
                        ledger.change_latest (account, message);
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
        result = source_block == nullptr ? mu_coin::process_result::gap_source : mu_coin::process_result::progress; // Have we seen the source block? (Harmless)
        if (result == mu_coin::process_result::progress)
        {
            auto source_send (dynamic_cast <mu_coin::send_block *> (source_block.get ()));
            result = source_send == nullptr ? mu_coin::process_result::not_receive_from_send : mu_coin::process_result::progress; // Are we receiving from a send (Malformed)
            if (result == mu_coin::process_result::progress)
            {
                result = mu_coin::validate_message (source_send->hashables.destination, hash, block_a.signature) ? mu_coin::process_result::bad_signature : mu_coin::process_result::progress; // Is the signature valid (Malformed)
                if (result == mu_coin::process_result::progress)
                {
                    mu_coin::frontier frontier;
                    result = ledger.store.latest_get (source_send->hashables.destination, frontier) ? mu_coin::process_result::gap_previous : mu_coin::process_result::progress;  //Have we seen the previous block? No entries for address at all (Harmless)
                    if (result == mu_coin::process_result::progress)
                    {
                        result = frontier.hash == block_a.hashables.previous ? mu_coin::process_result::progress : mu_coin::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
                        if (result == mu_coin::process_result::progress)
                        {
                            ledger.store.pending_del (source_send->hash ());
                            ledger.store.block_put (hash, block_a);
                            ledger.change_latest (source_send->hashables.destination, hash);
                            ledger.move_representation (ledger.representative (block_a.hashables.source), ledger.representative (hash), ledger.amount (block_a.hashables.source));
                        }
                        else
                        {
                            result = ledger.store.block_get (frontier.hash) ? mu_coin::process_result::fork : mu_coin::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
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
        result = source_block == nullptr ? mu_coin::process_result::gap_source : mu_coin::process_result::progress; // Have we seen the source block? (Harmless)
        if (result == mu_coin::process_result::progress)
        {
            auto source_send (dynamic_cast <mu_coin::send_block *> (source_block.get ()));
            result = source_send == nullptr ? mu_coin::process_result::not_receive_from_send : mu_coin::process_result::progress; // Are we receiving from a send (Malformed)
            if (result == mu_coin::process_result::progress)
            {
                result = mu_coin::validate_message (source_send->hashables.destination, hash, block_a.signature) ? mu_coin::process_result::bad_signature : mu_coin::process_result::progress; // Is the signature valid (Malformed)
                if (result == mu_coin::process_result::progress)
                {
                    mu_coin::frontier frontier;
                    result = ledger.store.latest_get (source_send->hashables.destination, frontier) ? mu_coin::process_result::progress : mu_coin::process_result::fork; // Has this account already been opened? (Malicious)
                    if (result == mu_coin::process_result::progress)
                    {
                        ledger.store.pending_del (source_send->hash ());
                        ledger.store.block_put (hash, block_a);
                        ledger.change_latest (source_send->hashables.destination, hash);
						ledger.move_representation (ledger.representative (block_a.hashables.source), ledger.representative (hash), ledger.amount (block_a.hashables.source));
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
            default:
                break;
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

mu_coin::block_store::block_store (boost::filesystem::path const & path_a)
{
    leveldb::DB * db;
    boost::filesystem::create_directories (path_a);
    leveldb::Options options;
    options.create_if_missing = true;
    auto status1 (leveldb::DB::Open (options, (path_a / "addresses.ldb").native ().c_str (), &db));
    addresses.reset (db);
    assert (status1.ok ());
    auto status2 (leveldb::DB::Open (options, (path_a / "blocks.ldb").native ().c_str (), &db));
    blocks.reset (db);
    assert (status2.ok ());
    auto status3 (leveldb::DB::Open (options, (path_a / "pending.ldb").native ().c_str (), &db));
    pending.reset (db);
    assert (status3.ok ());
    auto status4 (leveldb::DB::Open (options, (path_a / "representation.ldb").native ().c_str (), &db));
    representation.reset (db);
    assert (status4.ok ());
    auto status5 (leveldb::DB::Open (options, (path_a / "forks.ldb").native ().c_str (), &db));
    forks.reset (db);
    assert (status5.ok ());
    auto status6 (leveldb::DB::Open (options, (path_a / "bootstrap.ldb").native ().c_str (), &db));
    bootstrap.reset (db);
    assert (status6.ok ());
    auto status7 (leveldb::DB::Open (options, (path_a / "successors.ldb").native ().c_str (), &db));
    successors.reset (db);
    assert (status7.ok ());
    auto status8 (leveldb::DB::Open (options, (path_a / "checksum.ldb").native ().c_str (), &db));
    checksum.reset (db);
    assert (status8.ok ());
}

void mu_coin::block_store::block_put (mu_coin::block_hash const & hash_a, mu_coin::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        mu_coin::vectorstream stream (vector);
        mu_coin::serialize_block (stream, block_a);
    }
    auto status (blocks->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::block_get (mu_coin::block_hash const & hash_a)
{
    std::string value;
    auto status (blocks->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <mu_coin::block> result;
    if (status.ok ())
    {
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = mu_coin::deserialize_block (stream);
        assert (result != nullptr);
    }
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
    store_a.latest_put (send2.hashables.destination, {open.hash (), store_a.now ()});
    store_a.representation_put (send2.hashables.destination, send1.hashables.balance.number ());
}

bool mu_coin::block_store::latest_get (mu_coin::address const & address_a, mu_coin::frontier & frontier_a)
{
    std::string value;
    auto status (addresses->Get (leveldb::ReadOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    bool result;
    if (status.IsNotFound ())
    {
        result = true;
    }
    else
    {
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = read (stream, frontier_a.hash.bytes);
        if (!result)
        {
            result = read (stream, frontier_a.time);
        }
        assert (!result);
        result = false;
    }
    return result;
}

void mu_coin::block_store::latest_put (mu_coin::address const & address_a, mu_coin::frontier const & frontier_a)
{
    std::vector <uint8_t> vector;
    {
        mu_coin::vectorstream stream (vector);
        frontier_a.serialize (stream);
    }
    auto status (addresses->Put (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

void mu_coin::block_store::pending_put (mu_coin::identifier const & identifier_a)
{
    auto status (pending->Put (leveldb::WriteOptions (), leveldb::Slice (identifier_a.chars.data (), identifier_a.chars.size ()), leveldb::Slice (nullptr, 0)));
    assert (status.ok ());
}

void mu_coin::block_store::pending_del (mu_coin::identifier const & identifier_a)
{
    auto status (pending->Delete (leveldb::WriteOptions (), leveldb::Slice (identifier_a.chars.data (), identifier_a.chars.size ())));
    assert (status.ok ());
}

bool mu_coin::block_store::pending_get (mu_coin::identifier const & identifier_a)
{
    std::string value;
    auto status (pending->Get (leveldb::ReadOptions (), leveldb::Slice (identifier_a.chars.data (), identifier_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    bool result;
    if (status.IsNotFound ())
    {
        result = true;
    }
    else
    {
        result = false;
    }
    return result;
}

mu_coin::network::network (boost::asio::io_service & service_a, uint16_t port, mu_coin::client_impl & client_a) :
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
    std::unique_lock <std::mutex> lock (mutex);
    socket.async_receive_from (boost::asio::buffer (buffer), remote,
        [this] (boost::system::error_code const & error, size_t size_a)
        {
            receive_action (error, size_a);
        });
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
    if (network_keepalive_logging ())
    {
        client.log.add (boost::str (boost::format ("Kepalive req sent to %1%") % endpoint_a));
    }
    auto & client_l (client);
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, &client_l] (boost::system::error_code const & ec, size_t)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l.log.add (boost::str (boost::format ("Error sending keepalive: %1%") % ec.message ()));
                }
            }
        });
}

void mu_coin::network::publish_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <mu_coin::block> block)
{
    if (network_publish_logging ())
    {
        client.log.add (boost::str (boost::format ("Publish %1% to %2%") % block->hash ().to_string () % endpoint_a));
    }
    mu_coin::publish_req message (std::move (block));
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        mu_coin::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    auto & client_l (client);
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, &client_l] (boost::system::error_code const & ec, size_t size)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l.log.add (boost::str (boost::format ("Error sending publish: %1%") % ec.message ()));
                }
            }
        });
}

void mu_coin::network::confirm_block (boost::asio::ip::udp::endpoint const & endpoint_a, mu_coin::uint256_union const & session_a, mu_coin::block const & block)
{
    mu_coin::confirm_req message;
	message.session = session_a;
	message.block = block.clone ();
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        mu_coin::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_logging ())
    {
        client.log.add (boost::str (boost::format ("Sending confirm req to %1%") % endpoint_a));
    }
    auto & client_l (client);
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, &client_l] (boost::system::error_code const & ec, size_t size)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l.log.add (boost::str (boost::format ("Error sending confirm request: %1%") % ec.message ()));
                }
            }
        });
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
                auto known_peer (client.peers.known_peer (sender));
                if (!known_peer)
                {
                    send_keepalive (sender);
                }
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
                            if (network_keepalive_logging ())
                            {
                                client.log.add (boost::str (boost::format ("Received keepalive req from %1%") % sender));
                            }
                            mu_coin::keepalive_ack ack_message;
                            client.peers.random_fill (ack_message.peers);
                            ack_message.checksum = client.ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ());
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
                            if (network_keepalive_logging ())
                            {
                                client.log.add (boost::str (boost::format ("Sending keepalive ack to %2%") % sender));
                            }
                            auto & client_l (client);
                            send_buffer (ack_bytes->data (), ack_bytes->size (), sender, [ack_bytes, &client_l] (boost::system::error_code const & error, size_t size_a)
                                {
                                    if (network_logging ())
                                    {
                                        if (error)
                                        {
                                            client_l.log.add (boost::str (boost::format ("Error sending keepalive ack: %1%") % error.message ()));
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
                            if (network_keepalive_logging ())
                            {
                                client.log.add (boost::str (boost::format ("Received keepalive ack from %1%") % sender));
                            }
                            mu_coin::keepalive_req req_message;
                            client.peers.random_fill (req_message.peers);
                            std::shared_ptr <std::vector <uint8_t>> req_bytes (new std::vector <uint8_t>);
                            {
                                mu_coin::vectorstream stream (*req_bytes);
                                req_message.serialize (stream);
                            }
                            merge_peers (req_bytes, incoming.peers);
                            client.peers.incoming_from_peer (sender);
							if (!known_peer && incoming.checksum != client.ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ()))
							{
								client.processor.bootstrap (mu_coin::tcp_endpoint (sender.address (), sender.port ()),
                                    [] ()
                                    {
                                    });
							}
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
                            if (network_message_logging ())
                            {
                                client.log.add (boost::str (boost::format ("Received publish req rom %1%") % sender));
                            }
                            client.processor.process_receive_republish (std::move (incoming.block), sender);
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
                        mu_coin::confirm_req incoming;
                        mu_coin::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            if (network_message_logging ())
                            {
                                client.log.add (boost::str (boost::format ("Received confirm req from %1%") % sender));
                            }
                            auto result (client.ledger.process (*incoming.block));
                            switch (result)
                            {
                                case mu_coin::process_result::old:
                                case mu_coin::process_result::progress:
                                {
                                    client.processor.process_confirmation (incoming.session, incoming.block->hash (), sender);
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
                        auto incoming (new mu_coin::confirm_ack);
                        mu_coin::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming->deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            if (network_message_logging ())
                            {
                                client.log.add (boost::str (boost::format ("Received Confirm from %1%") % sender));
                            }
                            client.processor.confirm_ack (std::unique_ptr <mu_coin::confirm_ack> {incoming}, sender);
                        }
                        break;
                    }
                    case mu_coin::message_type::confirm_nak:
                    {
                        ++confirm_nak_count;
                        if (network_message_logging ())
                        {
                            client.log.add (boost::str (boost::format ("Received confirm nak from %1%") %  sender));
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
            if (network_logging ())
            {
                client.log.add ("Reserved sender");
            }
        }
    }
    else
    {
        if (network_logging ())
        {
            client.log.add ("Receive error");
        }
    }
}

void mu_coin::network::merge_peers (std::shared_ptr <std::vector <uint8_t>> const & bytes_a, std::array <mu_coin::endpoint, 24> const & peers_a)
{
    for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i) // Amplify attack, send to the same IP many times
    {
        if (!client.peers.contacting_peer (*i) && *i != endpoint ())
        {
            if (network_keepalive_logging ())
            {
                client.log.add (boost::str (boost::format ("Sending keepalive req to %1%") % i));
            }
            auto & client_l (client);
            auto endpoint (*i);
            send_buffer (bytes_a->data (), bytes_a->size (), endpoint, [bytes_a, &client_l] (boost::system::error_code const & error, size_t size_a)
                {
                    if (network_logging ())
                    {
                        if (error)
                        {
                            client_l.log.add (boost::str (boost::format ("Error sending keepalive request: %1%") % error.message ()));
                        }
                    }
                });
        }
        else
        {
            if (network_logging ())
            {
                if (mu_coin::reserved_address (*i))
                {
                    if (i->address ().to_v4 ().to_ulong () != 0 || i->port () != 0)
                    {
                        client.log.add (boost::str (boost::format ("Keepalive req contained reserved address")));
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

mu_coin::wallet::wallet (mu_coin::uint256_union const & password_a, boost::filesystem::path const & path_a) :
password (password_a)
{
    leveldb::Options options;
    options.create_if_missing = true;
    auto status (leveldb::DB::Open (options, path_a.native().c_str (), &handle));
    assert (status.ok ());
}

mu_coin::wallet::wallet (mu_coin::uint256_union const & password_a, mu_coin::wallet_temp_t const &) :
wallet (password_a, boost::filesystem::unique_path ())
{
}

void mu_coin::wallet::insert (mu_coin::public_key const & pub, mu_coin::private_key const & prv, mu_coin::uint256_union const & key_a)
{
    mu_coin::uint256_union encrypted (prv, key_a, pub.owords [0]);
    auto status (handle->Put (leveldb::WriteOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
    assert (status.ok ());
}

void mu_coin::wallet::insert (mu_coin::private_key const & prv, mu_coin::uint256_union const & key)
{
    mu_coin::public_key pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    insert (pub, prv, key);
}

bool mu_coin::wallet::fetch (mu_coin::public_key const & pub, mu_coin::secret_key const & key_a, mu_coin::private_key & prv)
{
    auto result (false);
    std::string value;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ()), &value));
    if (status.ok ())
    {
        mu_coin::uint256_union encrypted;
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto result2 (read (stream, encrypted.bytes));
        assert (!result2);
        prv = encrypted.prv (key_a, pub.owords [0]);
        mu_coin::public_key compare;
        ed25519_publickey (prv.bytes.data (), compare.bytes.data ());
        if (!(pub == compare))
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

mu_coin::key_iterator::key_iterator (leveldb::DB * db_a) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

mu_coin::key_iterator::key_iterator (leveldb::DB * db_a, std::nullptr_t) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

mu_coin::key_iterator::key_iterator (leveldb::DB * db_a, mu_coin::uint256_union const & key_a) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    iterator->Seek (leveldb::Slice (key_a.chars.data (), key_a.chars.size ()));
    set_current ();
}

void mu_coin::key_iterator::set_current ()
{
    if (iterator->Valid ())
    {
        current.first = iterator->key ();
        current.second = iterator->value ();
    }
    else
    {
        current.first.clear ();
        current.second.clear ();
    }
}

mu_coin::key_iterator & mu_coin::key_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}

mu_coin::key_entry & mu_coin::key_iterator::operator -> ()
{
    return current;
}

mu_coin::key_iterator mu_coin::wallet::begin ()
{
    mu_coin::key_iterator result (handle);
    return result;
}

mu_coin::key_iterator mu_coin::wallet::find (mu_coin::uint256_union const & key)
{
    mu_coin::key_iterator result (handle, key);
    mu_coin::key_iterator end (handle, nullptr);
    if (result != end)
    {
        if (result.current.first == key)
        {
            return result;
        }
        else
        {
            return end;
        }
    }
    else
    {
        return end;
    }
}

mu_coin::key_iterator mu_coin::wallet::end ()
{
    return mu_coin::key_iterator (handle, nullptr);
}

bool mu_coin::key_iterator::operator == (mu_coin::key_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
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
            mu_coin::frontier frontier;
            assert (!ledger_a.store.latest_get (account, frontier));
            auto amount (std::min (remaining, balance));
            remaining -= amount;
            std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
            block->hashables.destination = destination;
            block->hashables.previous = frontier.hash;
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

void mu_coin::processor_service::run ()
{
    std::unique_lock <std::mutex> lock (mutex);
    while (!done)
    {
        if (!operations.empty ())
        {
            auto & operation_l (operations.top ());
            if (operation_l.wakeup < std::chrono::system_clock::now ())
            {
                auto operation (operation_l);
                operations.pop ();
                lock.unlock ();
                operation.function ();
                lock.lock ();
            }
            else
            {
                condition.wait_until (lock, operation_l.wakeup);
            }
        }
        else
        {
            condition.wait (lock);
        }
    }
}

size_t mu_coin::processor_service::poll_one ()
{
    std::unique_lock <std::mutex> lock (mutex);
    size_t result (0);
    if (!operations.empty ())
    {
        auto & operation_l (operations.top ());
        if (operation_l.wakeup < std::chrono::system_clock::now ())
        {
            auto operation (operation_l);
            operations.pop ();
            lock.unlock ();
            operation.function ();
            result = 1;
        }
    }
    return result;
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

mu_coin::processor::processor (mu_coin::client_impl & client_a) :
client (client_a)
{
}

void mu_coin::processor::stop ()
{
    confirm_listeners.clear ();
}

bool mu_coin::operation::operator > (mu_coin::operation const & other_a) const
{
    return wakeup > other_a.wakeup;
}

mu_coin::client_impl::client_impl (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, boost::filesystem::path const & wallet_path_a, boost::filesystem::path const & block_store_path_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a, mu_coin::genesis const & genesis_a) :
genesis (genesis_a),
representative (representative_a),
store (block_store_path_a),
ledger (store),
wallet (0, wallet_path_a),
network (*service_a, port_a, *this),
bootstrap (*service_a, port_a, *this),
rpc (service_a, pool_a, command_port_a, *this),
processor (*this),
transactions (ledger, wallet, processor),
peers (network.endpoint ()),
service (processor_a)
{
    genesis_a.initialize (store);
    ledger.checksum_update (genesis.hash ());
}

mu_coin::client_impl::client_impl (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a, mu_coin::genesis const & genesis_a) :
client_impl (service_a, pool_a, port_a, command_port_a, boost::filesystem::unique_path (), boost::filesystem::unique_path (), processor_a, representative_a, genesis_a)
{
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, boost::filesystem::path const & wallet_path_a, boost::filesystem::path const & block_store_path_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a, mu_coin::genesis const & genesis_a) :
client_m (std::make_shared <mu_coin::client_impl> (service_a, pool_a, port_a, command_port_a, wallet_path_a, block_store_path_a, processor_a, representative_a, genesis_a))
{
}

mu_coin::client::client (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, uint16_t command_port_a, mu_coin::processor_service & processor_a, mu_coin::address const & representative_a, mu_coin::genesis const & genesis_a) :
client_m (std::make_shared <mu_coin::client_impl> (service_a, pool_a, port_a, command_port_a, processor_a, representative_a, genesis_a))
{
}

mu_coin::client::~client ()
{
    client_m->stop ();
}

namespace
{
class publish_processor : public std::enable_shared_from_this <publish_processor>
{
public:
    publish_processor (std::shared_ptr <mu_coin::client_impl> client_a, std::unique_ptr <mu_coin::block> incoming_a, mu_coin::endpoint const & sender_a) :
    client (client_a),
    incoming (std::move (incoming_a)),
    sender (sender_a),
    attempts (0)
    {
    }
    void run ()
    {
        auto hash (incoming->hash ());
        auto list (client->peers.list ());
        if (network_publish_logging ())
        {
            client->log.add (boost::str (boost::format ("Publishing %1% to %2% peers") % hash.to_string () % list.size ()));
        }
        for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
        {
            if (i->endpoint != sender)
            {
                client->network.publish_block (i->endpoint, incoming->clone ());
            }
        }
        if (attempts < 0)
        {
            ++attempts;
            auto this_l (shared_from_this ());
            client->service.add (std::chrono::system_clock::now () + std::chrono::seconds (15), [this_l] () {this_l->run ();});
            if (network_publish_logging ())
            {
                client->log.add (boost::str (boost::format ("Queueing another publish for %1%") % hash.to_string ()));
            }
        }
        else
        {
            if (network_publish_logging ())
            {
                client->log.add (boost::str (boost::format ("Done publishing for %1%") % hash.to_string ()));
            }
        }
    }
    std::shared_ptr <mu_coin::client_impl> client;
    std::unique_ptr <mu_coin::block> incoming;
    mu_coin::endpoint sender;
    int attempts;
};
}

void mu_coin::processor::republish (std::unique_ptr <mu_coin::block> incoming_a, mu_coin::endpoint const & sender_a)
{
    auto republisher (std::make_shared <publish_processor> (client.shared (), incoming_a->clone (), sender_a));
    republisher->run ();
}

namespace {
class republish_visitor : public mu_coin::block_visitor
{
public:
    republish_visitor (std::shared_ptr <mu_coin::client_impl> client_a, std::unique_ptr <mu_coin::block> incoming_a, mu_coin::endpoint const & sender_a) :
    client (client_a),
    incoming (std::move (incoming_a)),
    sender (sender_a)
    {
        assert (client_a->store.block_exists (incoming->hash ()));
    }
    void send_block (mu_coin::send_block const & block_a)
    {
        if (client->wallet.find (block_a.hashables.destination) == client->wallet.end ())
        {
            client->processor.republish (std::move (incoming), sender);
        }
    }
    void receive_block (mu_coin::receive_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    void open_block (mu_coin::open_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    void change_block (mu_coin::change_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    std::shared_ptr <mu_coin::client_impl> client;
    std::unique_ptr <mu_coin::block> incoming;
    mu_coin::endpoint sender;
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
    void frontier_req (mu_coin::frontier_req const &) override
    {
        assert (false);
    }
    mu_coin::receivable_processor & processor;
    mu_coin::endpoint sender;
};
}

mu_coin::gap_cache::gap_cache () :
max (128)
{
}


void mu_coin::gap_cache::add (mu_coin::block const & block_a, mu_coin::block_hash needed_a)
{
    auto existing (blocks.find (needed_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [] (mu_coin::gap_information & info) {info.arrival = std::chrono::system_clock::now ();});
    }
    else
    {
        blocks.insert ({std::chrono::system_clock::now (), needed_a, block_a.clone ()});
        if (blocks.size () > max)
        {
            blocks.get <1> ().erase (blocks.get <1> ().begin ());
        }
    }
}

std::unique_ptr <mu_coin::block> mu_coin::gap_cache::get (mu_coin::block_hash const & hash_a)
{
    std::unique_ptr <mu_coin::block> result;
    auto existing (blocks.find (hash_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [&] (mu_coin::gap_information & info) {result.swap (info.block);});
        blocks.erase (existing);
    }
    return result;
}

mu_coin::receivable_processor::receivable_processor (std::unique_ptr <mu_coin::block> incoming_a, std::shared_ptr <mu_coin::client_impl> client_a) :
threshold (client_a->ledger.supply () / 2),
incoming (std::move (incoming_a)),
client (client_a),
complete (false)
{
    ed25519_randombytes_unsafe (session.bytes.data (), sizeof (session.bytes));
}

mu_coin::receivable_processor::~receivable_processor ()
{
}

void mu_coin::receivable_processor::start ()
{
    auto this_l (shared_from_this ());
    client->service.add (std::chrono::system_clock::now (), [this_l] () {this_l->initiate_confirmation ();});
}

void mu_coin::receivable_processor::initiate_confirmation ()
{
    mu_coin::uint256_t weight (0);
    if (client->wallet.find (client->representative) != client->wallet.end ())
    {
        weight = client->ledger.weight (client->representative);
    }
    std::string rep_weight (weight.convert_to <std::string> ());
    process_acknowledged (weight);
    if (!complete)
    {
        auto this_l (shared_from_this ());
        client->processor.add_confirm_listener (session, [this_l] (std::unique_ptr <mu_coin::message> message_a, mu_coin::endpoint const & endpoint_a) {this_l->confirm_ack (std::move (message_a), endpoint_a);});
        auto list (client->peers.list ());
        for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
        {
            client->network.confirm_block (i->endpoint, session, *incoming);
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
    client->service.add (timeout, [this_l] () {this_l->timeout_action ();});
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
            lock.unlock ();
            assert (dynamic_cast <mu_coin::send_block *> (incoming.get ()) != nullptr);
            auto & send (static_cast <mu_coin::send_block &> (*incoming.get ()));
            mu_coin::private_key prv;
            if (!client->wallet.fetch (send.hashables.destination, client->wallet.password, prv))
            {
                auto error (client->transactions.receive (send, prv, client->representative));
                prv.bytes.fill (0);
                assert (!error);
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
        auto weight (processor.client->ledger.weight (message.address));
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
        auto weight (processor.client->ledger.weight (message.address));
        processor.nacked += weight;
    }
    else
    {
        // Signature didn't match.
    }
}

void mu_coin::processor::process_receivable (mu_coin::block const & incoming)
{
    auto processor (std::make_shared <receivable_processor> (incoming.clone (), client.shared ()));
    processor->start ();
}

void mu_coin::processor::process_receive_republish (std::unique_ptr <mu_coin::block> incoming, mu_coin::endpoint const & sender_a)
{
    std::unique_ptr <mu_coin::block> block (std::move (incoming));
    do
    {
        auto hash (block->hash ());
        auto process_result (process_receive (*block));
        switch (process_result)
        {
            case mu_coin::process_result::progress:
            {
                republish_visitor visitor (client.shared (), std::move (block), sender_a);
                visitor.incoming->visit (visitor);
                break;
            }
            default:
            {
                break;
            }
        }
        block = client.gap_cache.get (hash);
    }
    while (block != nullptr);
}

namespace
{
class receivable_visitor : public mu_coin::block_visitor
{
public:
    receivable_visitor (mu_coin::client_impl & client_a, mu_coin::block const & incoming_a) :
    client (client_a),
    incoming (incoming_a)
    {
    }
    void send_block (mu_coin::send_block const & block_a) override
    {
        if (client.wallet.find (block_a.hashables.destination) != client.wallet.end ())
        {
            client.processor.process_receivable (incoming);
        }
    }
    void receive_block (mu_coin::receive_block const &) override
    {
    }
    void open_block (mu_coin::open_block const &) override
    {
    }
    void change_block (mu_coin::change_block const &) override
    {
    }
    mu_coin::client_impl & client;
    mu_coin::block const & incoming;
};
class progress_log_visitor : public mu_coin::block_visitor
{
public:
    progress_log_visitor (mu_coin::client_impl & client_a) :
    client (client_a)
    {
    }
    void send_block (mu_coin::send_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Sending from:\n\t%1% to:\n\t%2% amount:\n\t%3% previous:\n\t%4% block:\n\t%5%") % client.ledger.account (block_a.hash ()).to_string () % block_a.hashables.destination.to_string () % client.ledger.amount (block_a.hash ()) % block_a.hashables.previous.to_string () % block_a.hash ().to_string ()));
    }
    void receive_block (mu_coin::receive_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Receiving from:\n\t%1% to:\n\t%2% previous:\n\t%3% block:\n\t%4%") % client.ledger.account (block_a.hashables.source).to_string () % client.ledger.account (block_a.hash ()).to_string () %block_a.hashables.previous.to_string () % block_a.hash ().to_string ()));
    }
    void open_block (mu_coin::open_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Open from:\n\t%1% to:\n\t%2% block:\n\t%3%") % client.ledger.account (block_a.hashables.source).to_string () % client.ledger.account (block_a.hash ()).to_string () % block_a.hash ().to_string ()));
    }
    void change_block (mu_coin::change_block const & block_a) override
    {
    }
    mu_coin::client_impl & client;
};
}

mu_coin::process_result mu_coin::processor::process_receive (mu_coin::block const & incoming)
{
    auto result (client.ledger.process (incoming));
    switch (result)
    {
        case mu_coin::process_result::progress:
        {
            if (ledger_logging ())
            {
                progress_log_visitor logger (client);
                incoming.visit (logger);
            }
            receivable_visitor visitor (client, incoming);
            incoming.visit (visitor);
            break;
        }
        case mu_coin::process_result::gap_previous:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Gap previous for: %1%") % incoming.hash ().to_string ()));
            }
            auto previous (incoming.previous ());
            client.gap_cache.add (incoming, previous);
            break;
        }
        case mu_coin::process_result::gap_source:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Gap source for: %1%") % incoming.hash ().to_string ()));
            }
            auto source (incoming.source ());
            client.gap_cache.add (incoming, source);
            break;
        }
        case mu_coin::process_result::old:
        {
            if (ledger_duplicate_logging ())
            {
                client.log.add (boost::str (boost::format ("Old for: %1%") % incoming.hash ().to_string ()));
            }
            break;
        }
        case mu_coin::process_result::bad_signature:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Bad signature for: %1%") % incoming.hash ().to_string ()));
            }
            break;
        }
        case mu_coin::process_result::overspend:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Overspend for: %1%") % incoming.hash ().to_string ()));
            }
            break;
        }
        case mu_coin::process_result::overreceive:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Overreceive for: %1%") % incoming.hash ().to_string ()));
            }
            break;
        }
        case mu_coin::process_result::not_receive_from_send:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Not receive from spend for: %1%") % incoming.hash ().to_string ()));
            }
            break;
        }
        case mu_coin::process_result::fork:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Fork for: %1%") % incoming.hash ().to_string ()));
            }
            assert (false);
            break;
        }
    }
    return result;
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
	write (stream_a, checksum);
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
	read (stream_a, checksum);
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

mu_coin::account_iterator::account_iterator (leveldb::DB & db_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

mu_coin::account_iterator::account_iterator (leveldb::DB & db_a, std::nullptr_t) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

void mu_coin::account_iterator::set_current ()
{
    if (iterator->Valid ())
    {
        current.first = iterator->key ();
        auto slice (iterator->value ());
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (slice.data ()), slice.size ());
        auto error (current.second.deserialize (stream));
        assert (!error);
    }
    else
    {
        current.first.clear ();
        current.second.hash.clear ();
        current.second.time = 0;
    }
}

mu_coin::account_iterator & mu_coin::account_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}
mu_coin::account_entry & mu_coin::account_iterator::operator -> ()
{
    return current;
}

void mu_coin::frontier::serialize (mu_coin::stream & stream_a) const
{
    write (stream_a, hash.bytes);
    write (stream_a, time);
}

bool mu_coin::frontier::deserialize (mu_coin::stream & stream_a)
{
    auto result (read (stream_a, hash.bytes));
    if (!result)
    {
        result = read (stream_a, time);
    }
    return result;
}

bool mu_coin::account_iterator::operator == (mu_coin::account_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
}

bool mu_coin::account_iterator::operator != (mu_coin::account_iterator const & other_a) const
{
    return !(*this == other_a);
}

mu_coin::block_iterator::block_iterator (leveldb::DB & db_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

mu_coin::block_iterator::block_iterator (leveldb::DB & db_a, std::nullptr_t) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

void mu_coin::block_iterator::set_current ()
{
    if (iterator->Valid ())
    {
        current.first = iterator->key ();
        auto slice (iterator->value ());
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (slice.data ()), slice.size ());
        current.second = mu_coin::deserialize_block (stream);
        assert (current.second != nullptr);
    }
    else
    {
        current.first.clear ();
        current.second.release ();
    }
}

mu_coin::block_iterator & mu_coin::block_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}

mu_coin::block_entry & mu_coin::block_iterator::operator -> ()
{
    return current;
}

bool mu_coin::block_iterator::operator == (mu_coin::block_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
}

bool mu_coin::block_iterator::operator != (mu_coin::block_iterator const & other_a) const
{
    return !(*this == other_a);
}

mu_coin::block_iterator mu_coin::block_store::blocks_begin ()
{
    mu_coin::block_iterator result (*blocks);
    return result;
}

mu_coin::block_iterator mu_coin::block_store::blocks_end ()
{
    mu_coin::block_iterator result (*blocks, nullptr);
    return result;
}

mu_coin::account_iterator mu_coin::block_store::latest_begin ()
{
    mu_coin::account_iterator result (*addresses);
    return result;
}

mu_coin::account_iterator mu_coin::block_store::latest_end ()
{
    mu_coin::account_iterator result (*addresses, nullptr);
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

bool mu_coin::client_impl::send (mu_coin::public_key const & address, mu_coin::uint256_t const & coins, mu_coin::uint256_union const & password)
{
    return transactions.send (address, coins, password);
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
        genesis.initialize (clients.back ()->client_m->store);
    }
    for (auto i (clients.begin ()), j (clients.end ()); i != j; ++i)
    {
        (*i)->client_m->start ();
    }
    for (auto i (clients.begin ()), j (clients.begin () + 1), n (clients.end ()); j != n; ++i, ++j)
    {
        auto starting1 ((*i)->client_m->peers.size ());
        auto starting2 ((*j)->client_m->peers.size ());
        (*j)->client_m->network.send_keepalive ((*i)->client_m->network.endpoint ());
        do {
            service->run_one ();
        } while ((*i)->client_m->peers.size () == starting1 || (*j)->client_m->peers.size () == starting2);
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
    auto & client_l (client);
    client.network.send_buffer (bytes->data (), bytes->size (), sender, [bytes, &client_l] (boost::system::error_code const & ec, size_t size_a)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l.log.add (boost::str (boost::format ("Error sending confirmation response: %1%") % ec.message ()));
                }
            }
        });
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

void mu_coin::confirm_nak::serialize (mu_coin::stream & stream_a)
{
    assert (false);
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

mu_coin::rpc::rpc (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, mu_coin::client_impl & client_a) :
server (decltype (server)::options (*this).address ("0.0.0.0").port (std::to_string (port_a)).io_service (service_a).thread_pool (pool_a)),
client (client_a)
{
}

void mu_coin::rpc::start ()
{
    server.listen ();
}

void mu_coin::rpc::stop ()
{
    server.stop ();
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
    std::string value;
    auto status (representation->Get (leveldb::ReadOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    mu_coin::uint256_t result;
    if (status.ok ())
    {
        mu_coin::uint256_union rep;
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error (rep.deserialize (stream));
        assert (!error);
        result = rep.number ();
    }
    else
    {
        result = 0;
    }
    return result;
}

void mu_coin::block_store::representation_put (mu_coin::address const & address_a, mu_coin::uint256_t const & representation_a)
{
    mu_coin::uint256_union rep (representation_a);
    auto status (representation->Put (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), leveldb::Slice (rep.chars.data (), rep.chars.size ())));
    assert (status.ok ());
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
    std::string value;
    auto status (forks->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <mu_coin::block> result;
    if (status.ok ())
    {
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = mu_coin::deserialize_block (stream);
        assert (result != nullptr);
    }
    return result;
}

void mu_coin::block_store::fork_put (mu_coin::block_hash const & hash_a, mu_coin::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        mu_coin::vectorstream stream (vector);
        mu_coin::serialize_block (stream, block_a);
    }
    auto status (forks->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
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
        ledger.change_latest (account, block_a.hashables.previous);
		ledger.store.block_del (hash);
    }
    void receive_block (mu_coin::receive_block const & block_a) override
    {
		auto hash (block_a.hash ());
		ledger.move_representation (ledger.representative (hash), ledger.account (block_a.hashables.source), ledger.amount (block_a.hashables.source));
        ledger.change_latest (ledger.account (hash), block_a.hashables.previous);
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source);
    }
    void open_block (mu_coin::open_block const & block_a) override
    {
		auto hash (block_a.hash ());
		ledger.move_representation (ledger.representative (hash), ledger.account (block_a.hashables.source), ledger.amount (block_a.hashables.source));
        ledger.change_latest (ledger.account (hash), 0);
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source);
    }
    void change_block (mu_coin::change_block const & block_a) override
    {
		ledger.move_representation (block_a.hashables.representative, ledger.representative (block_a.hashables.previous), ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (block_a.hash ());
        ledger.change_latest (ledger.account (block_a.hashables.previous), block_a.hashables.previous);
    }
    mu_coin::ledger & ledger;
};
}

void mu_coin::block_store::block_del (mu_coin::block_hash const & hash_a)
{
    auto status (blocks->Delete (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

void mu_coin::ledger::rollback (mu_coin::block_hash const & frontier_a)
{
	auto account_l (account (frontier_a));
    rollback_visitor rollback (*this);
    mu_coin::frontier frontier;
	do
	{
		auto latest_error (store.latest_get (account_l, frontier));
		assert (!latest_error);
        auto block (store.block_get (frontier.hash));
        block->visit (rollback);
		
	} while (frontier.hash != frontier_a);
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
    auto destination_previous (store.representation_get (destination_a));
    store.representation_put (destination_a, destination_previous + amount_a);
}

mu_coin::block_hash mu_coin::ledger::latest (mu_coin::address const & address_a)
{
    mu_coin::frontier frontier;
	auto latest_error (store.latest_get (address_a, frontier));
	assert (!latest_error);
	return frontier.hash;
}

void mu_coin::block_store::latest_del (mu_coin::address const & address_a)
{
    auto status (addresses->Delete (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ())));
    assert (status.ok ());
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
        assert (false && "This isn't unlocking, test");
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

void mu_coin::client_impl::start ()
{
    rpc.start ();
    network.receive ();
    processor.ongoing_keepalive ();
    bootstrap.start ();
}

void mu_coin::client_impl::stop ()
{
    rpc.stop ();
    network.stop ();
    bootstrap.stop ();
    processor.stop ();
}

void mu_coin::processor::bootstrap (boost::asio::ip::tcp::endpoint const & endpoint_a, std::function <void ()> const & complete_action_a)
{
    auto processor (std::make_shared <mu_coin::bootstrap_initiator> (client.shared (), complete_action_a));
    processor->run (endpoint_a);
}

mu_coin::bootstrap_receiver::bootstrap_receiver (boost::asio::io_service & service_a, uint16_t port_a, mu_coin::client_impl & client_a) :
acceptor (service_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::any (), port_a)),
service (service_a),
client (client_a)
{
}

void mu_coin::bootstrap_receiver::start ()
{
    acceptor.open (local.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (local);
    acceptor.listen ();
    accept_connection ();
}

void mu_coin::bootstrap_receiver::stop ()
{
    on = false;
    acceptor.close ();
}

void mu_coin::bootstrap_receiver::accept_connection ()
{
    auto socket (std::make_shared <boost::asio::ip::tcp::socket> (service));
    acceptor.async_accept (*socket, [this, socket] (boost::system::error_code const & error) {accept_action (error, socket); accept_connection ();});
}

void mu_coin::bootstrap_receiver::accept_action (boost::system::error_code const & ec, std::shared_ptr <boost::asio::ip::tcp::socket> socket_a)
{
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (socket_a, client.shared ()));
    connection->receive ();
}

mu_coin::bootstrap_connection::bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket> socket_a, std::shared_ptr <mu_coin::client_impl> client_a) :
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
                boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (mu_coin::uint256_union) + sizeof (mu_coin::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->receive_bulk_req_action (ec, size_a);});
                break;
            }
			case mu_coin::message_type::frontier_req:
			{
				auto this_l (shared_from_this ());
				boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (mu_coin::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t)), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->receive_frontier_req_action (ec, size_a);});
				break;
			}
            default:
            {
                if (network_logging ())
                {
                    client->log.add (boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast <uint8_t> (type)));
                }
                break;
            }
        }
    }
    else
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error while receiving type %1%") % ec.message ()));
        }
    }
}

void mu_coin::bootstrap_connection::receive_bulk_req_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
        mu_coin::bufferstream stream (receive_buffer.data (), sizeof (mu_coin::message_type) + sizeof (mu_coin::uint256_union) + sizeof (mu_coin::uint256_union));
        auto error (request->deserialize (stream));
        if (!error)
        {
            receive ();
            if (network_logging ())
            {
                client->log.add (boost::str (boost::format ("Received bulk request for %1% down to %2%") % request->start.to_string () % request->end.to_string ()));
            }
			add_request (std::unique_ptr <mu_coin::message> (request.release ()));
        }
    }
}

void mu_coin::bootstrap_connection::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		std::unique_ptr <mu_coin::frontier_req> request (new mu_coin::frontier_req);
		mu_coin::bufferstream stream (receive_buffer.data (), sizeof (mu_coin::message_type) + sizeof (mu_coin::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			receive ();
			if (network_logging ())
			{
				client->log.add (boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age));
			}
			add_request (std::unique_ptr <mu_coin::message> (request.release ()));
		}
	}
    else
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error sending receiving frontier request %1%") % ec.message ()));
        }
    }
}

void mu_coin::bootstrap_connection::add_request (std::unique_ptr <mu_coin::message> message_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next ();
	}
}

void mu_coin::bootstrap_connection::finish_request ()
{
	std::lock_guard <std::mutex> lock (mutex);
	requests.pop ();
	if (!requests.empty ())
	{
		run_next ();
	}
}

namespace
{
class request_response_visitor : public mu_coin::message_visitor
{
public:
    request_response_visitor (std::shared_ptr <mu_coin::bootstrap_connection> connection_a) :
    connection (connection_a)
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
    void confirm_req (mu_coin::confirm_req const &)
    {
        assert (false);
    }
    void confirm_ack (mu_coin::confirm_ack const &)
    {
        assert (false);
    }
    void confirm_nak (mu_coin::confirm_nak const &)
    {
        assert (false);
    }
    void confirm_unk (mu_coin::confirm_unk const &)
    {
        assert (false);
    }
    void bulk_req (mu_coin::bulk_req const &)
    {
        auto response (std::make_shared <mu_coin::bulk_req_response> (connection, std::unique_ptr <mu_coin::bulk_req> (static_cast <mu_coin::bulk_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    void frontier_req (mu_coin::frontier_req const &)
    {
        auto response (std::make_shared <mu_coin::frontier_req_response> (connection, std::unique_ptr <mu_coin::frontier_req> (static_cast <mu_coin::frontier_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    std::shared_ptr <mu_coin::bootstrap_connection> connection;
};
}

void mu_coin::bootstrap_connection::run_next ()
{
	assert (!requests.empty ());
    request_response_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void mu_coin::bulk_req_response::set_current_end ()
{
    assert (request != nullptr);
    auto end_exists (request->end.is_zero () || connection->client->store.block_exists (request->end));
    if (end_exists)
    {
        mu_coin::frontier frontier;
        auto no_address (connection->client->store.latest_get (request->start, frontier));
        if (no_address)
        {
            current = request->end;
        }
        else
        {
            if (!request->end.is_zero ())
            {
                account_visitor visitor (connection->client->store);
                visitor.compute (request->end);
                if (visitor.result == request->start)
                {
                    current = frontier.hash;
                }
                else
                {
                    current = request->end;
                }
            }
            else
            {
                current = frontier.hash;
            }
        }
    }
    else
    {
        current = request->end;
    }
}

void mu_coin::bulk_req_response::send_next ()
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
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Sending block: %1%") % block->hash ().to_string ()));
        }
        async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->sent_action (ec, size_a);});
    }
    else
    {
        send_finished ();
    }
}

std::unique_ptr <mu_coin::block> mu_coin::bulk_req_response::get_next ()
{
    std::unique_ptr <mu_coin::block> result;
    if (current != request->end)
    {
        result = connection->client->store.block_get (current);
        assert (result != nullptr);
        auto previous (result->previous ());
        if (!previous.is_zero ())
        {
            current = previous;
        }
        else
        {
            request->end = current;
        }
    }
    return result;
}

void mu_coin::bulk_req_response::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
}

void mu_coin::bulk_req_response::send_finished ()
{
    send_buffer.clear ();
    send_buffer.push_back (static_cast <uint8_t> (mu_coin::block_type::not_a_block));
    auto this_l (shared_from_this ());
    if (network_logging ())
    {
        connection->client->log.add ("Bulk sending finished");
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->no_block_sent (ec, size_a);});
}

void mu_coin::bulk_req_response::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
		connection->finish_request ();
    }
}

mu_coin::account_iterator mu_coin::block_store::latest_begin (mu_coin::address const & address_a)
{
    mu_coin::account_iterator result (*addresses, address_a);
    return result;
}

mu_coin::account_iterator::account_iterator (leveldb::DB & db_a, mu_coin::address const & address_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->Seek (leveldb::Slice (address_a.chars.data (), address_a.chars.size ()));
    set_current ();
}

namespace
{
class request_visitor : public mu_coin::message_visitor
{
public:
    request_visitor (std::shared_ptr <mu_coin::bootstrap_initiator> connection_a) :
    connection (connection_a)
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
    void confirm_req (mu_coin::confirm_req const &)
    {
        assert (false);
    }
    void confirm_ack (mu_coin::confirm_ack const &)
    {
        assert (false);
    }
    void confirm_nak (mu_coin::confirm_nak const &)
    {
        assert (false);
    }
    void confirm_unk (mu_coin::confirm_unk const &)
    {
        assert (false);
    }
    void bulk_req (mu_coin::bulk_req const &)
    {
        auto response (std::make_shared <mu_coin::bulk_req_initiator> (connection, std::unique_ptr <mu_coin::bulk_req> (static_cast <mu_coin::bulk_req *> (connection->requests.front ().release ()))));
        response->receive_block ();
    }
    void frontier_req (mu_coin::frontier_req const &)
    {
        auto response (std::make_shared <mu_coin::frontier_req_initiator> (connection, std::unique_ptr <mu_coin::frontier_req> (static_cast <mu_coin::frontier_req *> (connection->requests.front ().release ()))));
        response->receive_frontier ();
    }
    std::shared_ptr <mu_coin::bootstrap_initiator> connection;
};
}

mu_coin::bootstrap_initiator::bootstrap_initiator (std::shared_ptr <mu_coin::client_impl> client_a, std::function <void ()> const & complete_action_a) :
client (client_a),
socket (client_a->network.service),
complete_action (complete_action_a)
{
}

void mu_coin::bootstrap_initiator::run (boost::asio::ip::tcp::endpoint const & endpoint_a)
{
    if (network_logging ())
    {
        client->log.add (boost::str (boost::format ("Initiating bootstrap connection to %1%") % endpoint_a));
    }
    auto this_l (shared_from_this ());
    socket.async_connect (endpoint_a, [this_l] (boost::system::error_code const & ec) {this_l->connect_action (ec);});
}

void mu_coin::bootstrap_initiator::connect_action (boost::system::error_code const & ec)
{
    if (!ec)
    {
        send_frontier_request ();
    }
    else
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error initiating bootstrap connection %1%") % ec.message ()));
        }
    }
}

void mu_coin::bootstrap_initiator::send_frontier_request ()
{
    std::unique_ptr <mu_coin::frontier_req> request (new mu_coin::frontier_req);
    request->start.clear ();
    request->age = std::numeric_limits <decltype (request->age)>::max ();
    request->count = std::numeric_limits <decltype (request->age)>::max ();
    add_request (std::move (request));
}

void mu_coin::bootstrap_initiator::sent_request (boost::system::error_code const & ec, size_t size_a)
{
    if (ec)
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ()));
        }
    }
}

void mu_coin::bootstrap_initiator::add_request (std::unique_ptr <mu_coin::message> message_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    send_buffer.clear ();
    {
        mu_coin::vectorstream stream (send_buffer);
        message_a->serialize (stream);
    }
    auto startup (requests.empty ());
    requests.push (std::move (message_a));
    if (startup)
    {
        run_receiver ();
    }
    auto this_l (shared_from_this ());
    boost::asio::async_write (socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->sent_request (ec, size_a);});
}

void mu_coin::bootstrap_initiator::run_receiver ()
{
    assert (!mutex.try_lock ());
    assert (requests.front () != nullptr);
    request_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void mu_coin::bootstrap_initiator::finish_request ()
{
    std::lock_guard <std::mutex> lock (mutex);
    assert (!requests.empty ());
    requests.pop ();
    if (!requests.empty ())
    {
        run_receiver ();
    }
}

void mu_coin::bulk_req_initiator::receive_block ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_type (ec, size_a);});
}

void mu_coin::bulk_req_initiator::received_type (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        auto this_l (shared_from_this ());
        mu_coin::block_type type (static_cast <mu_coin::block_type> (receive_buffer [0]));
        switch (type)
        {
            case mu_coin::block_type::send:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 64 + 32 + 32 + 32), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case mu_coin::block_type::receive:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 64 + 32 + 32), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case mu_coin::block_type::open:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 32 + 32 + 64), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case mu_coin::block_type::change:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 32 + 32 + 64), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case mu_coin::block_type::not_a_block:
            {
                auto error (process_end ());
                if (error)
                {
                    connection->client->log.add ("Error processing end_block");
                }
                break;
            }
            default:
            {
                connection->client->log.add ("Unknown type received as block type");
                break;
            }
        }
    }
    else
    {
        connection->client->log.add (boost::str (boost::format ("Error receiving block type %1%") % ec.message ()));
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

bool mu_coin::bulk_req_initiator::process_end ()
{
    bool result;
    if (expecting == request->end)
    {
        mu_coin::process_result processing;
        std::unique_ptr <mu_coin::block> block;
        do
        {
            block = connection->client->store.bootstrap_get (expecting);
            if (block != nullptr)
            {
                processing = connection->client->processor.process_receive (*block);
                expecting = block->hash ();
            }
        } while (block != nullptr && processing == mu_coin::process_result::progress);
        result = processing != mu_coin::process_result::progress;
    }
    else if (expecting == request->start)
    {
        result = false;
    }
    else
    {
        result = true;
    }
    connection->finish_request ();
    return result;
}

mu_coin::block_hash mu_coin::genesis::hash () const
{
    return open.hash ();
}

void mu_coin::bulk_req_initiator::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		mu_coin::bufferstream stream (receive_buffer.data (), 1 + size_a);
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

bool mu_coin::bulk_req_initiator::process_block (mu_coin::block const & block)
{
    assert (!connection->requests.empty ());
    bool result;
    auto hash (block.hash ());
    if (network_logging ())
    {
        connection->client->log.add (boost::str (boost::format ("Received block: %1%") % hash.to_string ()));
    }
    if (expecting != request->end && (expecting == request->start || hash == expecting))
    {
        auto previous (block.previous ());
        connection->client->store.bootstrap_put (previous, block);
        expecting = previous;
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Expecting: %1%") % expecting.to_string ()));
        }
        result = false;
    }
    else
    {
		if (network_logging ())
		{
            connection->client->log.add (boost::str (boost::format ("Block hash: %1% did not match expecting %1%") % expecting.to_string ()));
		}
        result = true;
    }
    return result;
}

bool mu_coin::block_store::block_exists (mu_coin::block_hash const & hash_a)
{
    bool result;
    std::unique_ptr <leveldb::Iterator> iterator (blocks->NewIterator (leveldb::ReadOptions ()));
    iterator->Seek (leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()));
    if (iterator->Valid ())
    {
        mu_coin::uint256_union hash;
        hash = iterator->key ();
        if (hash == hash_a)
        {
            result = true;
        }
        else
        {
            result = false;
        }
    }
    else
    {
        result = false;
    }
    return result;
}

void mu_coin::block_store::bootstrap_put (mu_coin::block_hash const & hash_a, mu_coin::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        mu_coin::vectorstream stream (vector);
        mu_coin::serialize_block (stream, block_a);
    }
    auto status (bootstrap->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok () | status.IsNotFound ());
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::bootstrap_get (mu_coin::block_hash const & hash_a)
{
    std::string value;
    auto status (bootstrap->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <mu_coin::block> result;
    if (status.ok ())
    {
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = mu_coin::deserialize_block (stream);
        assert (result != nullptr);
    }
    return result;
}

void mu_coin::block_store::bootstrap_del (mu_coin::block_hash const & hash_a)
{
    auto status (bootstrap->Delete (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

mu_coin::endpoint mu_coin::network::endpoint ()
{
    return mu_coin::endpoint (boost::asio::ip::address_v4::loopback (), socket.local_endpoint ().port ());
}

boost::asio::ip::tcp::endpoint mu_coin::bootstrap_receiver::endpoint ()
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

mu_coin::bootstrap_initiator::~bootstrap_initiator ()
{
    complete_action ();
    if (network_logging ())
    {
        client->log.add ("Exiting bootstrap processor");
    }
}

mu_coin::bootstrap_connection::~bootstrap_connection ()
{
    if (network_logging ())
    {
        client->log.add ("Exiting bootstrap connection");
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

mu_coin::block_hash mu_coin::send_block::source () const
{
    return 0;
}

mu_coin::block_hash mu_coin::receive_block::source () const
{
    return hashables.source;
}

mu_coin::block_hash mu_coin::open_block::source () const
{
    return hashables.source;
}

mu_coin::block_hash mu_coin::change_block::source () const
{
    return 0;
}

void mu_coin::log::add (std::string const & string_a)
{
    items.push_back (std::make_pair (std::chrono::system_clock::now (), string_a));
}

void mu_coin::log::dump_cerr ()
{
    for (auto & i: items)
    {
        std::cerr << i.first << ' ' << i.second << std::endl;
    }
}

mu_coin::log::log () :
items (1024)
{
}

std::ostream & operator << (std::ostream & stream_a, std::chrono::system_clock::time_point const & time_a)
{
    time_t last_contact (std::chrono::system_clock::to_time_t (time_a));
    std::string string (ctime (&last_contact));
    string.pop_back ();
    stream_a << string;
    return stream_a;
}

void mu_coin::network::send_buffer (uint8_t const * data_a, size_t size_a, mu_coin::endpoint const & endpoint_a, std::function <void (boost::system::error_code const &, size_t)> callback_a)
{
    std::unique_lock <std::mutex> lock (mutex);
    auto do_send (sends.empty ());
    sends.push (std::make_tuple (data_a, size_a, endpoint_a, callback_a));
    if (do_send)
    {
        if (network_packet_logging ())
        {
            client.log.add ("Sending packet");
        }
        socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint_a, [this] (boost::system::error_code const & ec, size_t size_a) {send_complete (ec, size_a);});
    }
}

void mu_coin::network::send_complete (boost::system::error_code const & ec, size_t size_a)
{
    if (network_packet_logging ())
    {
        client.log.add ("Packet send complete");
    }
    std::tuple <uint8_t const *, size_t, mu_coin::endpoint, std::function <void (boost::system::error_code const &, size_t)>> self;
    {
        std::unique_lock <std::mutex> lock (mutex);
        assert (!sends.empty ());
        self = sends.front ();
        sends.pop ();
        if (!sends.empty ())
        {
            auto & front (sends.front ());
            if (network_packet_logging ())
            {
                if (network_packet_logging ())
                {
                    client.log.add ("Sending packet");
                }
            }
            socket.async_send_to (boost::asio::buffer (std::get <0> (front), std::get <1> (front)), std::get <2> (front), [this] (boost::system::error_code const & ec, size_t size_a) {send_complete (ec, size_a);});
        }
    }
    std::get <3> (self) (ec, size_a);
}

uint64_t mu_coin::block_store::now ()
{
    boost::posix_time::ptime epoch (boost::gregorian::date (1970, 1, 1));
    auto now (boost::posix_time::second_clock::universal_time ());
    auto diff (now - epoch);
    return diff.total_seconds ();
}

mu_coin::bulk_req_response::bulk_req_response (std::shared_ptr <mu_coin::bootstrap_connection> const & connection_a, std::unique_ptr <mu_coin::bulk_req> request_a) :
connection (connection_a),
request (std::move (request_a))
{
    set_current_end ();
}

mu_coin::frontier_req_response::frontier_req_response (std::shared_ptr <mu_coin::bootstrap_connection> const & connection_a, std::unique_ptr <mu_coin::frontier_req> request_a) :
iterator (connection_a->client->store.latest_begin (request_a->start)),
connection (connection_a),
request (std::move (request_a))
{
    skip_old ();
}

void mu_coin::frontier_req_response::skip_old ()
{
    if (request->age != std::numeric_limits<decltype (request->age)>::max ())
    {
        auto now (connection->client->store.now ());
        while (iterator != connection->client->ledger.store.latest_end () && (now - iterator->second.time) >= request->age)
        {
            ++iterator;
        }
    }
}

void mu_coin::frontier_req_response::send_next ()
{
	auto pair (get_next ());
    if (!pair.first.is_zero ())
    {
        {
            send_buffer.clear ();
            mu_coin::vectorstream stream (send_buffer);
            write (stream, pair.first.bytes);
            write (stream, pair.second.bytes);
        }
        auto this_l (shared_from_this ());
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Sending frontier for %1% %2%") % pair.first.to_string () % pair.second.to_string ()));
        }
        async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->sent_action (ec, size_a);});
    }
    else
    {
        send_finished ();
    }
}

void mu_coin::frontier_req_response::send_finished ()
{
    {
        send_buffer.clear ();
        mu_coin::vectorstream stream (send_buffer);
        mu_coin::uint256_union zero (0);
        write (stream, zero.bytes);
        write (stream, zero.bytes);
    }
    auto this_l (shared_from_this ());
    if (network_logging ())
    {
        connection->client->log.add ("Frontier sending finished");
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->no_block_sent (ec, size_a);});
}

void mu_coin::frontier_req_response::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
		connection->finish_request ();
    }
    else
    {
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Error sending frontier finish %1%") % ec.message ()));
        }
    }
}

void mu_coin::frontier_req_response::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
    else
    {
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Error sending frontier pair %1%") % ec.message ()));
        }
    }
}

std::pair <mu_coin::uint256_union, mu_coin::uint256_union> mu_coin::frontier_req_response::get_next ()
{
    std::pair <mu_coin::uint256_union, mu_coin::uint256_union> result (0, 0);
    if (iterator != connection->client->ledger.store.latest_end ())
    {
        result.first = iterator->first;
        result.second = iterator->second.hash;
        ++iterator;
    }
    return result;
}

bool mu_coin::frontier_req::deserialize (mu_coin::stream & stream_a)
{
    mu_coin::message_type type;
    auto result (read (stream_a, type));
    if (!result)
    {
        assert (type == mu_coin::message_type::frontier_req);
        result = read (stream_a, start.bytes);
        if (!result)
        {
            result = read (stream_a, age);
            if (!result)
            {
                result = read (stream_a, count);
            }
        }
    }
    return result;
}

void mu_coin::frontier_req::serialize (mu_coin::stream & stream_a)
{
    write (stream_a, mu_coin::message_type::frontier_req);
    write (stream_a, start.bytes);
    write (stream_a, age);
    write (stream_a, count);
}

void mu_coin::frontier_req::visit (mu_coin::message_visitor & visitor_a)
{
    visitor_a.frontier_req (*this);
}

bool mu_coin::frontier_req::operator == (mu_coin::frontier_req const & other_a) const
{
    return start == other_a.start && age == other_a.age && count == other_a.count;
}

mu_coin::bulk_req_initiator::bulk_req_initiator (std::shared_ptr <mu_coin::bootstrap_initiator> const & connection_a, std::unique_ptr <mu_coin::bulk_req> request_a) :
request (std::move (request_a)),
expecting (request->start),
connection (connection_a)
{
    assert (!connection_a->requests.empty ());
    assert (connection_a->requests.front () == nullptr);
}

mu_coin::bulk_req_initiator::~bulk_req_initiator ()
{
    if (network_logging ())
    {
        connection->client->log.add ("Exiting bulk_req initiator");
    }
}

mu_coin::frontier_req_initiator::frontier_req_initiator (std::shared_ptr <mu_coin::bootstrap_initiator> const & connection_a, std::unique_ptr <mu_coin::frontier_req> request_a) :
request (std::move (request_a)),
connection (connection_a)
{
}

mu_coin::frontier_req_initiator::~frontier_req_initiator ()
{
    if (network_logging ())
    {
        connection->client->log.add ("Exiting frontier_req initiator");
    }
}

void mu_coin::frontier_req_initiator::receive_frontier ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), sizeof (mu_coin::uint256_union) + sizeof (mu_coin::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_frontier (ec, size_a);});
}

void mu_coin::frontier_req_initiator::received_frontier (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == sizeof (mu_coin::uint256_union) + sizeof (mu_coin::uint256_union));
        mu_coin::address address;
        mu_coin::bufferstream address_stream (receive_buffer.data (), sizeof (mu_coin::uint256_union));
        auto error1 (address.deserialize (address_stream));
        assert (!error1);
        mu_coin::block_hash latest;
        mu_coin::bufferstream latest_stream (receive_buffer.data () + sizeof (mu_coin::uint256_union), sizeof (mu_coin::uint256_union));
        auto error2 (latest.deserialize (latest_stream));
        assert (!error2);
        if (!address.is_zero ())
        {
            mu_coin::frontier frontier;
            auto unknown (connection->client->store.latest_get (address, frontier));
            if (unknown)
            {
                std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
                request->start = address;
                request->end.clear ();
                connection->add_request (std::move (request));
            }
            else
            {
                auto exists (connection->client->store.block_exists (latest));
                if (!exists)
                {
                    std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
                    request->start = address;
                    request->end = frontier.hash;
                    connection->add_request (std::move (request));
                }
            }
            receive_frontier ();
        }
        else
        {
            connection->finish_request ();
        }
    }
    else
    {
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Error while receiving frontier %1%") % ec.message ()));
        }
    }
}

void mu_coin::block_store::checksum_put (uint64_t prefix, uint8_t mask, mu_coin::uint256_union const & hash_a)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    auto status (checksum->Put (leveldb::WriteOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

bool mu_coin::block_store::checksum_get (uint64_t prefix, uint8_t mask, mu_coin::uint256_union & hash_a)
{
    assert ((prefix & 0xff) == 0);
    std::string value;
    uint64_t key (prefix | mask);
    auto status (checksum->Get (leveldb::ReadOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)), &value));
    assert (status.ok () || status.IsNotFound ());
    bool result;
    if (status.ok ())
    {
        result = false;
        mu_coin::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error (hash_a.deserialize (stream));
        assert (!error);
    }
    else
    {
        result = true;
    }
    return result;
}

void mu_coin::block_store::checksum_del (uint64_t prefix, uint8_t mask)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    checksum->Delete (leveldb::WriteOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)));
}

mu_coin::uint256_union & mu_coin::uint256_union::operator ^= (mu_coin::uint256_union const & other_a)
{
    auto j (other_a.qwords.begin ());
    for (auto i (qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j)
    {
        *i ^= *j;
    }
    return *this;
}

mu_coin::uint256_union mu_coin::uint256_union::operator ^ (mu_coin::uint256_union const & other_a) const
{
    mu_coin::uint256_union result;
    auto k (result.qwords.begin ());
    for (auto i (qwords.begin ()), j (other_a.qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j, ++k)
    {
        *k = *i ^ *j;
    }
    return result;
}

mu_coin::checksum mu_coin::ledger::checksum (mu_coin::address const & begin_a, mu_coin::address const & end_a)
{
    mu_coin::checksum result;
    auto error (store.checksum_get (0, 0, result));
    assert (!error);
    return result;
}

void mu_coin::ledger::checksum_update (mu_coin::block_hash const & hash_a)
{
    mu_coin::checksum value;
    auto error (store.checksum_get (0, 0, value));
    assert (!error);
    value ^= hash_a;
    store.checksum_put (0, 0, value);
}

void mu_coin::ledger::change_latest (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::frontier frontier;
    auto exists (!store.latest_get (address_a, frontier));
    if (exists)
    {
        checksum_update (frontier.hash);
    }
    if (!hash_a.is_zero())
    {
        frontier.hash = hash_a;
        frontier.time = store.now ();
        store.latest_put (address_a, frontier);
        checksum_update (hash_a);
    }
    else
    {
        store.latest_del (address_a);
    }
}

bool mu_coin::keepalive_ack::operator == (mu_coin::keepalive_ack const & other_a) const
{
	return (peers == other_a.peers) && (checksum == other_a.checksum);
}

bool mu_coin::peer_container::known_peer (mu_coin::endpoint const & endpoint_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (peers.find (endpoint_a));
    return existing != peers.end () && existing->last_contact > std::chrono::system_clock::now () - mu_coin::processor::cutoff;
}

std::shared_ptr <mu_coin::client_impl> mu_coin::client_impl::shared ()
{
    return shared_from_this ();
}

namespace
{
class traffic_generator : public std::enable_shared_from_this <traffic_generator>
{
public:
    traffic_generator (uint32_t count_a, uint32_t wait_a, std::shared_ptr <mu_coin::client_impl> client_a, mu_coin::system & system_a) :
    count (count_a),
    wait (wait_a),
    client (client_a),
    system (system_a)
    {
    }
    void run ()
    {
        auto count_l (count - 1);
        count = count_l - 1;
        system.generate_activity (*client);
        if (count_l > 0)
        {
            auto this_l (shared_from_this ());
            client->service.add (std::chrono::system_clock::now () + std::chrono::milliseconds (wait), [this_l] () {this_l->run ();});
        }
    }
    uint32_t count;
    uint32_t wait;
    std::shared_ptr <mu_coin::client_impl> client;
    mu_coin::system & system;
};
}

void mu_coin::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a)
{
    for (size_t i (0), n (clients.size ()); i != n; ++i)
    {
        generate_usage_traffic (count_a, wait_a, i);
    }
}

void mu_coin::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a, size_t index_a)
{
    assert (clients.size () > index_a);
    assert (count_a > 0);
    auto generate (std::make_shared <traffic_generator> (count_a, wait_a, clients [index_a]->client_m, *this));
    generate->run ();
}

void mu_coin::system::generate_activity (mu_coin::client_impl & client_a)
{
    auto what (random_pool.GenerateByte ());
    if (what < 0xc0 && client_a.store.latest_begin () != client_a.store.latest_end ())
    {
        generate_send_existing (client_a);
    }
    else
    {
        generate_send_new (client_a);
    }
}

mu_coin::uint256_t mu_coin::system::get_random_amount (mu_coin::client_impl & client_a)
{
    mu_coin::uint512_t balance (client_a.balance ());
    std::string balance_text (balance.convert_to <std::string> ());
    mu_coin::uint256_union random_amount;
    random_pool.GenerateBlock (random_amount.bytes.data (), sizeof (random_amount.bytes));
    auto result (((mu_coin::uint512_t {random_amount.number ()} * balance) / mu_coin::uint512_t {std::numeric_limits <mu_coin::uint256_t>::max ()}).convert_to <mu_coin::uint256_t> ());
    std::string text (result.convert_to <std::string> ());
    return result;
}

void mu_coin::system::generate_send_existing (mu_coin::client_impl & client_a)
{
    mu_coin::address account;
    random_pool.GenerateBlock (account.bytes.data (), sizeof (account.bytes));
    mu_coin::account_iterator entry (client_a.store.latest_begin (account));
    if (entry == client_a.store.latest_end ())
    {
        entry = client_a.store.latest_begin ();
    }
    assert (entry != client_a.store.latest_end ());
    client_a.send (entry->first, get_random_amount (client_a), client_a.wallet.password);
}

void mu_coin::system::generate_send_new (mu_coin::client_impl & client_a)
{
    mu_coin::keypair key;
    client_a.wallet.insert (key.prv, client_a.wallet.password);
    client_a.send (key.pub, get_random_amount (client_a), client_a.wallet.password);
}

void mu_coin::system::generate_mass_activity (uint32_t count_a, mu_coin::client_impl & client_a)
{
    for (uint32_t i (0); i < count_a; ++i)
    {
        if ((i & 0xff) == 0)
        {
            std::cerr << boost::str (boost::format ("Mass activity iteration %1%\n") % i);
        }
        generate_activity (client_a);
    }
}

mu_coin::uint256_t mu_coin::client_impl::balance ()
{
    mu_coin::uint256_t result;
    for (auto i (wallet.begin ()), n (wallet.end ()); i !=  n; ++i)
    {
        auto pub (i->first);
        auto account_balance (ledger.account_balance (pub));
        result += account_balance;
    }
    return result;
}

mu_coin::transactions::transactions (mu_coin::ledger & ledger_a, mu_coin::wallet & wallet_a, mu_coin::processor & processor_a) :
ledger (ledger_a),
wallet (wallet_a),
processor (processor_a)
{
}

bool mu_coin::transactions::receive (mu_coin::send_block const & send_a, mu_coin::private_key const & prv_a, mu_coin::address const & representative_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto hash (send_a.hash ());
    bool result;
    if (!ledger.store.pending_get (hash))
    {
        mu_coin::frontier frontier;
        auto new_address (ledger.store.latest_get (send_a.hashables.destination, frontier));
        if (new_address)
        {
            balance_visitor visitor (ledger.store);
            visitor.compute (send_a.hashables.previous);
            auto open (new mu_coin::open_block);
            open->hashables.source = hash;
            open->hashables.representative = representative_a;
            mu_coin::sign_message (prv_a, send_a.hashables.destination, open->hash (), open->signature);
            processor.process_receive_republish (std::unique_ptr <mu_coin::block> (open), mu_coin::endpoint {});
        }
        else
        {
            balance_visitor visitor (ledger.store);
            visitor.compute (send_a.hashables.previous);
            auto receive (new mu_coin::receive_block);
            receive->hashables.previous = frontier.hash;
            receive->hashables.source = hash;
            mu_coin::sign_message (prv_a, send_a.hashables.destination, receive->hash (), receive->signature);
            processor.process_receive_republish (std::unique_ptr <mu_coin::block> (receive), mu_coin::endpoint {});
        }
        result = false;
    }
    else
    {
        result = true;
        // Ledger doesn't have this marked as available to receive anymore
    }
    return result;
}

bool mu_coin::transactions::send (mu_coin::address const & address_a, mu_coin::uint256_t const & coins_a, mu_coin::secret_key const & key_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    std::vector <std::unique_ptr <mu_coin::send_block>> blocks;
    auto result (wallet.generate_send (ledger, address_a, coins_a, key_a, blocks));
    if (!result)
    {
        for (auto i (blocks.begin ()), j (blocks.end ()); i != j; ++i)
        {
            processor.process_receive_republish (std::move (*i), mu_coin::endpoint {});
        }
    }
    return result;
}