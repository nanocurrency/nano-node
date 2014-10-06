#include <mu_coin/mu_coin.hpp>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/osrng.h>

#include <ed25519-donna/ed25519.h>

#include <unordered_set>
#include <memory>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace
{
    bool constexpr ledger_logging ()
    {
        return false;
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
    bool constexpr client_lifetime_tracing ()
    {
        return false;
    }
    bool constexpr insufficient_work_logging ()
    {
        return network_logging () && true;
    }
    bool constexpr log_to_cerr ()
    {
        return true;
    }
}

CryptoPP::AutoSeededRandomPool random_pool;
std::chrono::seconds constexpr rai::processor::period;
std::chrono::seconds constexpr rai::processor::cutoff;
rai::keypair rai::test_genesis_key ("E49C03BB7404C10B388AE56322217306B57F3DCBB3A5F060A2F420AD7AA3F034");
rai::address rai::genesis_address (rai::test_genesis_key.pub);

rai::uint256_union::uint256_union (boost::multiprecision::uint256_t const & number_a)
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

rai::uint256_union & rai::uint256_union::operator = (leveldb::Slice const & slice_a)
{
    assert (slice_a.size () == 32);
    rai::bufferstream stream (reinterpret_cast <uint8_t const *> (slice_a.data ()), slice_a.size ());
    auto error (deserialize (stream));
    assert (!error);
    return *this;
}

rai::uint512_union::uint512_union (boost::multiprecision::uint512_t const & number_a)
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

void rai::uint256_union::clear ()
{
    bytes.fill (0);
}

void rai::uint512_union::clear ()
{
    bytes.fill (0);
}

void hash_number (CryptoPP::SHA3 & hash_a, boost::multiprecision::uint256_t const & number_a)
{
    rai::uint256_union bytes (number_a);
    hash_a.Update (bytes.bytes.data (), sizeof (bytes));
}

rai::uint256_t rai::uint256_union::number () const
{
    rai::uint256_union temp (*this);
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

boost::multiprecision::uint512_t rai::uint512_union::number ()
{
    rai::uint512_union temp (*this);
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

void rai::sign_message (rai::private_key const & private_key, rai::public_key const & public_key, rai::uint256_union const & message, rai::uint512_union & signature)
{
    ed25519_sign (message.bytes.data (), sizeof (message.bytes), private_key.bytes.data (), public_key.bytes.data (), signature.bytes.data ());
}

bool rai::validate_message (rai::public_key const & public_key, rai::uint256_union const & message, rai::uint512_union const & signature)
{
    auto result (0 != ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), public_key.bytes.data (), signature.bytes.data ()));
    return result;
}

namespace {
class ledger_processor : public rai::block_visitor
{
public:
    ledger_processor (rai::ledger &);
    void send_block (rai::send_block const &) override;
    void receive_block (rai::receive_block const &) override;
    void open_block (rai::open_block const &) override;
    void change_block (rai::change_block const &) override;
    rai::ledger & ledger;
    rai::process_result result;
};

class amount_visitor : public rai::block_visitor
{
public:
    amount_visitor (rai::block_store &);
    void compute (rai::block_hash const &);
    void send_block (rai::send_block const &) override;
    void receive_block (rai::receive_block const &) override;
    void open_block (rai::open_block const &) override;
    void change_block (rai::change_block const &) override;
    void from_send (rai::block_hash const &);
    rai::block_store & store;
    rai::uint256_t result;
};

class balance_visitor : public rai::block_visitor
{
public:
    balance_visitor (rai::block_store &);
    void compute (rai::block_hash const &);
    void send_block (rai::send_block const &) override;
    void receive_block (rai::receive_block const &) override;
    void open_block (rai::open_block const &) override;
    void change_block (rai::change_block const &) override;
    rai::block_store & store;
    rai::uint256_t result;
};

class account_visitor : public rai::block_visitor
{
public:
    account_visitor (rai::block_store & store_a) :
    store (store_a)
    {
    }
    void compute (rai::block_hash const & hash_block)
    {
        auto block (store.block_get (hash_block));
        assert (block != nullptr);
        block->visit (*this);
    }
    void send_block (rai::send_block const & block_a) override
    {
        account_visitor prev (store);
        prev.compute (block_a.hashables.previous);
        result = prev.result;
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        from_previous (block_a.hashables.source);
    }
    void open_block (rai::open_block const & block_a) override
    {
        from_previous (block_a.hashables.source);
    }
    void change_block (rai::change_block const & block_a) override
    {
        account_visitor prev (store);
        prev.compute (block_a.hashables.previous);
        result = prev.result;
    }
    void from_previous (rai::block_hash const & hash_a)
    {
        auto block (store.block_get (hash_a));
        assert (block != nullptr);
        assert (dynamic_cast <rai::send_block *> (block.get ()) != nullptr);
        auto send (static_cast <rai::send_block *> (block.get ()));
        result = send->hashables.destination;
    }
    rai::block_store & store;
    rai::address result;
};

amount_visitor::amount_visitor (rai::block_store & store_a) :
store (store_a)
{
}

void amount_visitor::send_block (rai::send_block const & block_a)
{
    balance_visitor prev (store);
    prev.compute (block_a.hashables.previous);
    result = prev.result - block_a.hashables.balance.number ();
}

void amount_visitor::receive_block (rai::receive_block const & block_a)
{
    from_send (block_a.hashables.source);
}

void amount_visitor::open_block (rai::open_block const & block_a)
{
    from_send (block_a.hashables.source);
}

void amount_visitor::change_block (rai::change_block const & block_a)
{
    
}

void amount_visitor::from_send (rai::block_hash const & hash_a)
{
    balance_visitor source (store);
    source.compute (hash_a);
    auto source_block (store.block_get (hash_a));
    assert (source_block != nullptr);
    balance_visitor source_prev (store);
    source_prev.compute (source_block->previous ());
}

balance_visitor::balance_visitor (rai::block_store & store_a):
store (store_a),
result (0)
{
}

void balance_visitor::send_block (rai::send_block const & block_a)
{
    result = block_a.hashables.balance.number ();
}

void balance_visitor::receive_block (rai::receive_block const & block_a)
{
    balance_visitor prev (store);
    prev.compute (block_a.hashables.previous);
    amount_visitor source (store);
    source.compute (block_a.hashables.source);
    result = prev.result + source.result;
}

void balance_visitor::open_block (rai::open_block const & block_a)
{
    amount_visitor source (store);
    source.compute (block_a.hashables.source);
    result = source.result;
}

void balance_visitor::change_block (rai::change_block const & block_a)
{
    balance_visitor prev (store);
    prev.compute (block_a.hashables.previous);
    result = prev.result;
}
}

rai::uint256_t rai::ledger::balance (rai::block_hash const & hash_a)
{
	balance_visitor visitor (store);
	visitor.compute (hash_a);
	return visitor.result;
}

rai::uint256_t rai::ledger::account_balance (rai::address const & address_a)
{
    rai::uint256_t result (0);
    rai::frontier frontier;
    auto none (store.latest_get (address_a, frontier));
    if (!none)
    {
        result = frontier.balance.number ();
    }
    return result;
}

rai::process_result rai::ledger::process (rai::block const & block_a)
{
    ledger_processor processor (*this);
    block_a.visit (processor);
    return processor.result;
}

rai::keypair::keypair ()
{
    ed25519_randombytes_unsafe (prv.bytes.data (), sizeof (prv.bytes));
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

rai::keypair::keypair (std::string const & prv_a)
{
    auto error (prv.decode_hex (prv_a));
    assert (!error);
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

rai::ledger::ledger (rai::block_store & store_a) :
store (store_a)
{
    store.checksum_put (0, 0, 0);
}

bool rai::uint256_union::operator == (rai::uint256_union const & other_a) const
{
    return bytes == other_a.bytes;
}

bool rai::uint512_union::operator == (rai::uint512_union const & other_a) const
{
    return bytes == other_a.bytes;
}

void rai::uint256_union::serialize (rai::stream & stream_a) const
{
    write (stream_a, bytes);
}

bool rai::uint256_union::deserialize (rai::stream & stream_a)
{
    return read (stream_a, bytes);
}

rai::uint256_union::uint256_union (rai::private_key const & prv, uint256_union const & key, uint128_union const & iv)
{
    rai::uint256_union exponent (prv);
    CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
    enc.ProcessData (bytes.data (), exponent.bytes.data (), sizeof (exponent.bytes));
}

rai::private_key rai::uint256_union::prv (rai::secret_key const & key_a, uint128_union const & iv) const
{
    CryptoPP::AES::Decryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
    rai::private_key result;
    dec.ProcessData (result.bytes.data (), bytes.data (), sizeof (bytes));
    return result;
}

rai::uint256_union::uint256_union (std::string const & password_a)
{
    CryptoPP::SHA3 hash (32);
    hash.Update (reinterpret_cast <uint8_t const *> (password_a.c_str ()), password_a.size ());
    hash.Final (bytes.data ());
}

void rai::send_block::visit (rai::block_visitor & visitor_a) const
{
    visitor_a.send_block (*this);
}

void rai::receive_block::visit (rai::block_visitor & visitor_a) const
{
    visitor_a.receive_block (*this);
}

void rai::send_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

void rai::send_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
    hash_a.Update (balance.bytes.data (), sizeof (balance.bytes));
    hash_a.Update (destination.bytes.data (), sizeof (destination.bytes));
}

void rai::send_block::serialize (rai::stream & stream_a) const
{
    write (stream_a, signature.bytes);
    write (stream_a, hashables.previous.bytes);
    write (stream_a, hashables.balance.bytes);
    write (stream_a, hashables.destination.bytes);
}

bool rai::send_block::deserialize (rai::stream & stream_a)
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

void rai::receive_block::sign (rai::private_key const & prv, rai::public_key const & pub, rai::uint256_union const & hash_a)
{
    sign_message (prv, pub, hash_a, signature);
}

bool rai::receive_block::operator == (rai::receive_block const & other_a) const
{
    auto result (signature == other_a.signature && hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source);
    return result;
}

bool rai::receive_block::deserialize (rai::stream & stream_a)
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

void rai::receive_block::serialize (rai::stream & stream_a) const
{
    write (stream_a, signature.bytes);
    write (stream_a, hashables.previous.bytes);
    write (stream_a, hashables.source.bytes);
}

void rai::receive_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

void rai::receive_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (source.bytes.data (), sizeof (source.bytes));
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
}

namespace
{
    class representative_visitor : public rai::block_visitor
    {
    public:
        representative_visitor (rai::block_store & store_a) :
        store (store_a)
        {
        }
        void compute (rai::block_hash const & hash_a)
        {
            auto block (store.block_get (hash_a));
            assert (block != nullptr);
            block->visit (*this);
        }
        void send_block (rai::send_block const & block_a) override
        {
            representative_visitor visitor (store);
            visitor.compute (block_a.previous ());
            result = visitor.result;
        }
        void receive_block (rai::receive_block const & block_a) override
        {
            representative_visitor visitor (store);
            visitor.compute (block_a.previous ());
            result = visitor.result;
        }
        void open_block (rai::open_block const & block_a) override
        {
            result = block_a.hashables.representative;
        }
        void change_block (rai::change_block const & block_a) override
        {
            result = block_a.hashables.representative;
        }
        rai::block_store & store;
        rai::address result;
    };
}

void ledger_processor::change_block (rai::change_block const & block_a)
{
    rai::uint256_union message (block_a.hash ());
    auto existing (ledger.store.block_exists (message));
    result = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Harmless)
    if (result == rai::process_result::progress)
    {
        auto previous (ledger.store.block_exists (block_a.hashables.previous));
        result = previous ? rai::process_result::progress : rai::process_result::gap_previous;  // Have we seen the previous block before? (Harmless)
        if (result == rai::process_result::progress)
        {
			auto account (ledger.account (block_a.hashables.previous));
            rai::frontier frontier;
            auto latest_error (ledger.store.latest_get (account, frontier));
            assert (!latest_error);
            result = validate_message (account, message, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Malformed)
            if (result == rai::process_result::progress)
            {
                result = frontier.hash == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::fork; // Is the previous block the latest (Malicious)
                if (result == rai::process_result::progress)
                {
					ledger.move_representation (frontier.representative, block_a.hashables.representative, ledger.balance (block_a.hashables.previous));
                    ledger.store.block_put (message, block_a);
                    ledger.change_latest (account, message, block_a.hashables.representative, frontier.balance);
                }
            }
        }
    }
}

void ledger_processor::send_block (rai::send_block const & block_a)
{
    rai::uint256_union message (block_a.hash ());
    auto existing (ledger.store.block_exists (message));
    result = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Harmless)
    if (result == rai::process_result::progress)
    {
        auto previous (ledger.store.block_exists (block_a.hashables.previous));
        result = previous ? rai::process_result::progress : rai::process_result::gap_previous; // Have we seen the previous block before? (Harmless)
        if (result == rai::process_result::progress)
        {
			auto account (ledger.account (block_a.hashables.previous));
            result = validate_message (account, message, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Malformed)
            if (result == rai::process_result::progress)
            {
                rai::frontier frontier;
                auto latest_error (ledger.store.latest_get (account, frontier));
                assert (!latest_error);
                result = frontier.balance.number () >= block_a.hashables.balance.number () ? rai::process_result::progress : rai::process_result::overspend; // Is this trying to spend more than they have (Malicious)
                if (result == rai::process_result::progress)
                {
                    result = frontier.hash == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::fork;
                    if (result == rai::process_result::progress)
                    {
                        ledger.store.block_put (message, block_a);
                        ledger.change_latest (account, message, frontier.representative, block_a.hashables.balance);
                        ledger.store.pending_put (message, account, frontier.balance.number () - block_a.hashables.balance.number (), block_a.hashables.destination);
                    }
                }
            }
        }
    }
}

void ledger_processor::receive_block (rai::receive_block const & block_a)
{
    auto hash (block_a.hash ());
    auto existing (ledger.store.block_exists (hash));
    result = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block already?  (Harmless)
    if (result == rai::process_result::progress)
    {
        auto source_missing (!ledger.store.block_exists (block_a.hashables.source));
        result = source_missing ? rai::process_result::gap_source : rai::process_result::progress; // Have we seen the source block? (Harmless)
        if (result == rai::process_result::progress)
        {
            rai::address source_account;
            rai::uint256_union amount;
            rai::address destination_account;
            result = ledger.store.pending_get (block_a.hashables.source, source_account, amount, destination_account) ? rai::process_result::overreceive : rai::process_result::progress; // Has this source already been received (Malformed)
            if (result == rai::process_result::progress)
            {
                result = rai::validate_message (destination_account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
                if (result == rai::process_result::progress)
                {
                    rai::frontier frontier;
                    result = ledger.store.latest_get (destination_account, frontier) ? rai::process_result::gap_previous : rai::process_result::progress;  //Have we seen the previous block? No entries for address at all (Harmless)
                    if (result == rai::process_result::progress)
                    {
                        result = frontier.hash == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
                        if (result == rai::process_result::progress)
                        {
                            rai::frontier source_frontier;
                            auto error (ledger.store.latest_get (source_account, source_frontier));
                            assert (!error);
                            ledger.store.pending_del (block_a.hashables.source);
                            ledger.store.block_put (hash, block_a);
                            ledger.change_latest (destination_account, hash, frontier.representative, frontier.balance.number () + amount.number ());
                            ledger.move_representation (source_frontier.representative, frontier.representative, amount.number ());
                        }
                        else
                        {
                            result = ledger.store.block_get (frontier.hash) ? rai::process_result::fork : rai::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
                        }
                    }
                }
            }
        }
    }
}

void ledger_processor::open_block (rai::open_block const & block_a)
{
    auto hash (block_a.hash ());
    auto existing (ledger.store.block_exists (hash));
    result = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block already? (Harmless)
    if (result == rai::process_result::progress)
    {
        auto source_missing (!ledger.store.block_exists (block_a.hashables.source));
        result = source_missing ? rai::process_result::gap_source : rai::process_result::progress; // Have we seen the source block? (Harmless)
        if (result == rai::process_result::progress)
        {
            rai::address source_account;
            rai::uint256_union amount;
            rai::address destination_account;
            result = ledger.store.pending_get (block_a.hashables.source, source_account, amount, destination_account) ? rai::process_result::overreceive : rai::process_result::progress; // Has this source already been received (Malformed)
            if (result == rai::process_result::progress)
            {
                result = rai::validate_message (destination_account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
                if (result == rai::process_result::progress)
                {
                    rai::frontier frontier;
                    result = ledger.store.latest_get (destination_account, frontier) ? rai::process_result::progress : rai::process_result::fork; // Has this account already been opened? (Malicious)
                    if (result == rai::process_result::progress)
                    {
                        rai::frontier source_frontier;
                        auto error (ledger.store.latest_get (source_account, source_frontier));
                        assert (!error);
                        ledger.store.pending_del (block_a.hashables.source);
                        ledger.store.block_put (hash, block_a);
                        ledger.change_latest (destination_account, hash, block_a.hashables.representative, amount.number ());
                        ledger.move_representation (source_frontier.representative, block_a.hashables.representative, amount.number ());
                    }
                }
            }
        }
    }
}

ledger_processor::ledger_processor (rai::ledger & ledger_a) :
ledger (ledger_a),
result (rai::process_result::progress)
{
}

rai::send_block::send_block (send_block const & other_a) :
hashables (other_a.hashables),
signature (other_a.signature)
{
}

bool rai::receive_block::validate (rai::public_key const & key, rai::uint256_t const & hash) const
{
    return validate_message (key, hash, signature);
}

bool rai::send_block::operator == (rai::block const & other_a) const
{
    auto other_l (dynamic_cast <rai::send_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool rai::receive_block::operator == (rai::block const & other_a) const
{
    auto other_l (dynamic_cast <rai::receive_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

std::unique_ptr <rai::block> rai::send_block::clone () const
{
    return std::unique_ptr <rai::block> (new rai::send_block (*this));
}

std::unique_ptr <rai::block> rai::receive_block::clone () const
{
    return std::unique_ptr <rai::block> (new rai::receive_block (*this));
}

std::unique_ptr <rai::block> rai::deserialize_block (rai::stream & stream_a)
{
    rai::block_type type;
    auto error (read (stream_a, type));
    std::unique_ptr <rai::block> result;
    if (!error)
    {
        switch (type)
        {
            case rai::block_type::receive:
            {
                std::unique_ptr <rai::receive_block> obj (new rai::receive_block);
                auto error (obj->deserialize (stream_a));
                if (!error)
                {
                    result = std::move (obj);
                }
                break;
            }
            case rai::block_type::send:
            {
                std::unique_ptr <rai::send_block> obj (new rai::send_block);
                auto error (obj->deserialize (stream_a));
                if (!error)
                {
                    result = std::move (obj);
                }
                break;
            }
            case rai::block_type::open:
            {
                std::unique_ptr <rai::open_block> obj (new rai::open_block);
                auto error (obj->deserialize (stream_a));
                if (!error)
                {
                    result = std::move (obj);
                }
                break;
            }
            case rai::block_type::change:
            {
                std::unique_ptr <rai::change_block> obj (new rai::change_block);
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

void rai::serialize_block (rai::stream & stream_a, rai::block const & block_a)
{
    write (stream_a, block_a.type ());
    block_a.serialize (stream_a);
}

rai::block_type rai::send_block::type () const
{
    return rai::block_type::send;
}

rai::block_type rai::receive_block::type () const
{
    return rai::block_type::receive;
}

void rai::uint256_union::encode_hex (std::string & text) const
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (64) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool rai::uint256_union::decode_hex (std::string const & text)
{
    auto result (text.size () > 64);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::hex << std::noshowbase;
        rai::uint256_t number_l;
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

void rai::uint256_union::encode_dec (std::string & text) const
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::dec << std::noshowbase;
    stream << number ();
    text = stream.str ();
}

bool rai::uint256_union::decode_dec (std::string const & text)
{
    auto result (text.size () > 78);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::dec << std::noshowbase;
        rai::uint256_t number_l;
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

void rai::uint512_union::encode_hex (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (128) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool rai::uint512_union::decode_hex (std::string const & text)
{
    auto result (text.size () > 128);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::hex << std::noshowbase;
        rai::uint512_t number_l;
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

rai::block_store_temp_t rai::block_store_temp;

rai::block_store::block_store (block_store_temp_t const &) :
block_store (boost::filesystem::unique_path ())
{
}

rai::block_store::block_store (boost::filesystem::path const & path_a)
{
    leveldb::DB * db;
    boost::filesystem::create_directories (path_a);
    leveldb::Options options;
    options.create_if_missing = true;
    auto status1 (leveldb::DB::Open (options, (path_a / "addresses.ldb").string (), &db));
    addresses.reset (db);
    assert (status1.ok ());
    auto status2 (leveldb::DB::Open (options, (path_a / "blocks.ldb").string (), &db));
    blocks.reset (db);
    assert (status2.ok ());
    auto status3 (leveldb::DB::Open (options, (path_a / "pending.ldb").string (), &db));
    pending.reset (db);
    assert (status3.ok ());
    auto status4 (leveldb::DB::Open (options, (path_a / "representation.ldb").string (), &db));
    representation.reset (db);
    assert (status4.ok ());
    auto status5 (leveldb::DB::Open (options, (path_a / "forks.ldb").string (), &db));
    forks.reset (db);
    assert (status5.ok ());
    auto status6 (leveldb::DB::Open (options, (path_a / "bootstrap.ldb").string (), &db));
    bootstrap.reset (db);
    assert (status6.ok ());
    auto status8 (leveldb::DB::Open (options, (path_a / "checksum.ldb").string (), &db));
    checksum.reset (db);
    assert (status8.ok ());
}

void rai::block_store::block_put (rai::block_hash const & hash_a, rai::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        rai::serialize_block (stream, block_a);
    }
    auto status (blocks->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

std::unique_ptr <rai::block> rai::block_store::block_get (rai::block_hash const & hash_a)
{
    std::string value;
    auto status (blocks->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <rai::block> result;
    if (status.ok ())
    {
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = rai::deserialize_block (stream);
        assert (result != nullptr);
    }
    return result;
}

rai::genesis::genesis ()
{
    send1.hashables.destination.clear ();
    send1.hashables.balance = std::numeric_limits <rai::uint256_t>::max ();
    send1.hashables.previous.clear ();
    send1.signature.clear ();
    send2.hashables.destination = genesis_address;
    send2.hashables.balance.clear ();
    send2.hashables.previous = send1.hash ();
    send2.signature.clear ();
    open.hashables.source = send2.hash ();
    open.hashables.representative = genesis_address;
    open.signature.clear ();
}

void rai::genesis::initialize (rai::block_store & store_a) const
{
    assert (store_a.latest_begin () == store_a.latest_end ());
    store_a.block_put (send1.hash (), send1);
    store_a.block_put (send2.hash (), send2);
    store_a.block_put (open.hash (), open);
    store_a.latest_put (send2.hashables.destination, {open.hash (), open.hashables.representative, send1.hashables.balance, store_a.now ()});
    store_a.representation_put (send2.hashables.destination, send1.hashables.balance.number ());
    store_a.checksum_put (0, 0, hash ());
}

bool rai::block_store::latest_get (rai::address const & address_a, rai::frontier & frontier_a)
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
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = frontier_a.deserialize (stream);
        assert (!result);
    }
    return result;
}

void rai::block_store::latest_put (rai::address const & address_a, rai::frontier const & frontier_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        frontier_a.serialize (stream);
    }
    auto status (addresses->Put (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

void rai::block_store::pending_put (rai::identifier const & identifier_a, rai::address const & source_a, rai::uint256_union const & amount_a, rai::address const & destination_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        source_a.serialize (stream);
        amount_a.serialize (stream);
        destination_a.serialize (stream);
    }
    auto status (pending->Put (leveldb::WriteOptions (), leveldb::Slice (identifier_a.chars.data (), identifier_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

void rai::block_store::pending_del (rai::identifier const & identifier_a)
{
    auto status (pending->Delete (leveldb::WriteOptions (), leveldb::Slice (identifier_a.chars.data (), identifier_a.chars.size ())));
    assert (status.ok ());
}

bool rai::block_store::pending_exists (rai::address const & address_a)
{
    std::unique_ptr <leveldb::Iterator> iterator (pending->NewIterator (leveldb::ReadOptions {}));
    iterator->Seek (leveldb::Slice (address_a.chars.data (), address_a.chars.size ()));
    bool result;
    if (iterator->Valid ())
    {
        result = true;
    }
    else
    {
        result = false;
    }
    return result;
}

bool rai::block_store::pending_get (rai::identifier const & identifier_a, rai::address & source_a, rai::uint256_union & amount_a, rai::address & destination_a)
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
        assert (value.size () == sizeof (source_a.bytes) + sizeof (amount_a.bytes) + sizeof (destination_a.bytes));
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error1 (source_a.deserialize (stream));
        assert (!error1);
        auto error2 (amount_a.deserialize (stream));
        assert (!error2);
        auto error3 (destination_a.deserialize (stream));
        assert (!error3);
    }
    return result;
}

rai::network::network (boost::asio::io_service & service_a, uint16_t port, rai::client & client_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4::any (), port)),
service (service_a),
client (client_a),
keepalive_req_count (0),
keepalive_ack_count (0),
publish_req_count (0),
confirm_req_count (0),
confirm_ack_count (0),
confirm_unk_count (0),
bad_sender_count (0),
unknown_count (0),
error_count (0),
insufficient_work_count (0),
on (true)
{
}

void rai::network::receive ()
{
    std::unique_lock <std::mutex> lock (mutex);
    socket.async_receive_from (boost::asio::buffer (buffer), remote,
        [this] (boost::system::error_code const & error, size_t size_a)
        {
            receive_action (error, size_a);
        });
}

void rai::network::stop ()
{
    on = false;
    socket.close ();
}

void rai::network::send_keepalive (boost::asio::ip::udp::endpoint const & endpoint_a)
{
    rai::keepalive_req message;
    client.peers.random_fill (message.peers);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
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

void rai::network::publish_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <rai::block> block)
{
    if (network_publish_logging ())
    {
        client.log.add (boost::str (boost::format ("Publish %1% to %2%") % block->hash ().to_string () % endpoint_a));
    }
    rai::publish_req message (std::move (block));
    rai::work work;
    message.work = work.create (message.block->hash ());
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
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

void rai::network::send_confirm_req (boost::asio::ip::udp::endpoint const & endpoint_a, rai::block const & block)
{
    rai::confirm_req message;
	message.block = block.clone ();
    rai::work work;
    message.work = work.create (message.block->hash ());
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
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

void rai::network::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (!rai::reserved_address (remote) && remote != endpoint ())
        {
            if (size_a >= sizeof (rai::message_type))
            {
                auto sender (remote);
                auto known_peer (client.peers.known_peer (sender));
                if (!known_peer)
                {
                    send_keepalive (sender);
                }
                rai::bufferstream type_stream (buffer.data (), size_a);
                rai::message_type type;
                read (type_stream, type);
                switch (type)
                {
                    case rai::message_type::keepalive_req:
                    {
                        rai::keepalive_req incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
							++keepalive_req_count;
							client.processor.process_message (incoming, sender, known_peer);
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::keepalive_ack:
                    {
                        rai::keepalive_ack incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            ++keepalive_ack_count;
							client.processor.process_message (incoming, sender, known_peer);
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::publish_req:
                    {
                        rai::publish_req incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            if (!work.validate (incoming.block->hash (), incoming.work))
                            {
                                ++publish_req_count;
                                client.processor.process_message (incoming, sender, known_peer);
                            }
                            else
                            {
                                ++insufficient_work_count;
                                if (insufficient_work_logging ())
                                {
                                    client.log.add ("Insufficient work for publish_req");
                                }
                            }
                        }
                        else
                        {
                            ++error_count;
                        }
                        break;
                    }
                    case rai::message_type::confirm_req:
                    {
                        rai::confirm_req incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            if (!work.validate (incoming.block->hash (), incoming.work))
                            {
                                ++confirm_req_count;
                                client.processor.process_message (incoming, sender, known_peer);
                            }
                            else
                            {
                                ++insufficient_work_count;
                                if (insufficient_work_logging ())
                                {
                                    client.log.add ("Insufficient work for confirm_req");
                                }
                            }
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::confirm_ack:
                    {
                        rai::confirm_ack incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
							++confirm_ack_count;
							client.processor.process_message (incoming, sender, known_peer);
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::confirm_unk:
                    {
                        ++confirm_unk_count;
                        auto incoming (new rai::confirm_unk);
                        rai::bufferstream stream (buffer.data (), size_a);
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

void rai::network::merge_peers (std::shared_ptr <std::vector <uint8_t>> const & bytes_a, std::array <rai::endpoint, 24> const & peers_a)
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
                if (rai::reserved_address (*i))
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

rai::publish_req::publish_req (std::unique_ptr <rai::block> block_a) :
block (std::move (block_a))
{
}

bool rai::publish_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (!result);
    if (!result)
    {
        result = read (stream_a, work);
        if (!result)
        {
            block = rai::deserialize_block (stream_a);
            result = block == nullptr;
        }
    }
    return result;
}

void rai::publish_req::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::publish_req);
    write (stream_a, work);
    rai::serialize_block (stream_a, *block);
}

rai::wallet::wallet (boost::filesystem::path const & path_a) :
password (hash_password (""))
{
    boost::filesystem::create_directories (path_a);
    leveldb::Options options;
    options.create_if_missing = true;
    auto status (leveldb::DB::Open (options, (path_a / "wallet.ldb").string (), &handle));
    assert (status.ok ());
    rai::uint256_union wallet_password_key;
    wallet_password_key.clear ();
    std::string wallet_password_value;
    auto wallet_password_status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (wallet_password_key.chars.data (), wallet_password_key.chars.size ()), &wallet_password_value));
    if (wallet_password_status.IsNotFound ())
    {
        rai::uint256_union zero;
        zero.clear ();
        rai::uint256_union wallet_key;
        random_pool.GenerateBlock (wallet_key.bytes.data (), sizeof (wallet_key.bytes));
        rai::uint256_union encrypted (wallet_key, password, password.owords [0]);
        auto status1 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (zero.chars.data (), zero.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
        assert (status1.ok ());
        rai::uint256_union one (1);
        rai::uint256_union check (zero, wallet_key, wallet_key.owords [0]);
        auto status2 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (one.chars.data (), one.chars.size ()), leveldb::Slice (check.chars.data (), check.chars.size ())));
        assert (status2.ok ());
    }
}

void rai::wallet::insert (rai::private_key const & prv)
{
    rai::public_key pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    rai::uint256_union encrypted (prv, wallet_key (), pub.owords [0]);
    auto status (handle->Put (leveldb::WriteOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
    assert (status.ok ());
}

bool rai::wallet::fetch (rai::public_key const & pub, rai::private_key & prv)
{
    auto result (false);
    std::string value;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ()), &value));
    if (status.ok ())
    {
        rai::uint256_union encrypted;
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto result2 (read (stream, encrypted.bytes));
        assert (!result2);
        prv = encrypted.prv (wallet_key (), pub.owords [0]);
        rai::public_key compare;
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

rai::key_iterator::key_iterator (leveldb::DB * db_a) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

rai::key_iterator::key_iterator (leveldb::DB * db_a, std::nullptr_t) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

rai::key_iterator::key_iterator (leveldb::DB * db_a, rai::uint256_union const & key_a) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    iterator->Seek (leveldb::Slice (key_a.chars.data (), key_a.chars.size ()));
    set_current ();
}

void rai::key_iterator::set_current ()
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

rai::key_iterator & rai::key_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}

rai::key_entry & rai::key_iterator::operator -> ()
{
    return current;
}

rai::key_iterator rai::wallet::begin ()
{
    rai::key_iterator result (handle);
    assert (result != end ());
    ++result;
    assert (result != end ());
    ++result;
    return result;
}

rai::key_iterator rai::wallet::find (rai::uint256_union const & key)
{
    rai::key_iterator result (handle, key);
    rai::key_iterator end (handle, nullptr);
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

rai::key_iterator rai::wallet::end ()
{
    return rai::key_iterator (handle, nullptr);
}

bool rai::key_iterator::operator == (rai::key_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
}

bool rai::key_iterator::operator != (rai::key_iterator const & other_a) const
{
    return !(*this == other_a);
}

bool rai::wallet::generate_send (rai::ledger & ledger_a, rai::public_key const & destination, rai::uint256_t const & coins, std::vector <std::unique_ptr <rai::send_block>> & blocks)
{
    bool result (false);
    rai::uint256_t remaining (coins);
    for (auto i (begin ()), j (end ()); i != j && !result && !remaining.is_zero (); ++i)
    {
        auto account (i->first);
        auto balance (ledger_a.account_balance (account));
        if (!balance.is_zero ())
        {
            rai::frontier frontier;
            result = ledger_a.store.latest_get (account, frontier);
            assert (!result);
            auto amount (std::min (remaining, balance));
            remaining -= amount;
            std::unique_ptr <rai::send_block> block (new rai::send_block);
            block->hashables.destination = destination;
            block->hashables.previous = frontier.hash;
            block->hashables.balance = balance - amount;
            rai::private_key prv;
            result = fetch (account, prv);
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

rai::uint256_union::uint256_union (uint64_t value)
{
    *this = rai::uint256_t (value);
}

void rai::processor_service::run ()
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

size_t rai::processor_service::poll_one ()
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

size_t rai::processor_service::poll ()
{
    std::unique_lock <std::mutex> lock (mutex);
    size_t result (0);
    auto done_l (false);
    while (!done_l)
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
                ++result;
                lock.lock ();
            }
            else
            {
                done_l = true;
            }
        }
        else
        {
            done_l = true;
        }
    }
    return result;
}

void rai::processor_service::add (std::chrono::system_clock::time_point const & wakeup_a, std::function <void ()> const & operation)
{
    std::lock_guard <std::mutex> lock (mutex);
    operations.push (rai::operation ({wakeup_a, operation}));
    condition.notify_all ();
}

rai::processor_service::processor_service () :
done (false)
{
}

void rai::processor_service::stop ()
{
    std::lock_guard <std::mutex> lock (mutex);
    done = true;
    condition.notify_all ();
}

rai::processor::processor (rai::client & client_a) :
client (client_a)
{
}

void rai::processor::stop ()
{
}

bool rai::operation::operator > (rai::operation const & other_a) const
{
    return wakeup > other_a.wakeup;
}

rai::client::client (boost::shared_ptr <boost::asio::io_service> service_a, uint16_t port_a, boost::filesystem::path const & data_path_a, rai::processor_service & processor_a, rai::address const & representative_a) :
representative (representative_a),
store (data_path_a),
ledger (store),
conflicts (*this),
wallet (data_path_a),
network (*service_a, port_a, *this),
bootstrap (*service_a, port_a, *this),
processor (*this),
transactions (ledger, wallet, processor),
peers (network.endpoint ()),
service (processor_a),
scale ("100000000000000000000000000000000000000000000000000000000000000000") // 10 ^ 65
{
    if (client_lifetime_tracing ())
    {
        std::cerr << "Constructing client\n";
    }
    if (store.latest_begin () == store.latest_end ())
    {
        rai::genesis genesis;
        genesis.initialize (store);
    }
}

rai::client::client (boost::shared_ptr <boost::asio::io_service> service_a, uint16_t port_a, rai::processor_service & processor_a, rai::address const & representative_a) :
client (service_a, port_a, boost::filesystem::unique_path (), processor_a, representative_a)
{
}

rai::client::~client ()
{
    if (client_lifetime_tracing ())
    {
        std::cerr << "Destructing client\n";
    }
}

namespace
{
class publish_processor : public std::enable_shared_from_this <publish_processor>
{
public:
    publish_processor (std::shared_ptr <rai::client> client_a, std::unique_ptr <rai::block> incoming_a, rai::endpoint const & sender_a) :
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
    std::shared_ptr <rai::client> client;
    std::unique_ptr <rai::block> incoming;
    rai::endpoint sender;
    int attempts;
};
}

void rai::processor::republish (std::unique_ptr <rai::block> incoming_a, rai::endpoint const & sender_a)
{
    auto republisher (std::make_shared <publish_processor> (client.shared (), incoming_a->clone (), sender_a));
    republisher->run ();
}

namespace {
class republish_visitor : public rai::block_visitor
{
public:
    republish_visitor (std::shared_ptr <rai::client> client_a, std::unique_ptr <rai::block> incoming_a, rai::endpoint const & sender_a) :
    client (client_a),
    incoming (std::move (incoming_a)),
    sender (sender_a)
    {
        assert (client_a->store.block_exists (incoming->hash ()));
    }
    void send_block (rai::send_block const & block_a)
    {
        if (client->wallet.find (block_a.hashables.destination) == client->wallet.end ())
        {
            client->processor.republish (std::move (incoming), sender);
        }
    }
    void receive_block (rai::receive_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    void open_block (rai::open_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    void change_block (rai::change_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    std::shared_ptr <rai::client> client;
    std::unique_ptr <rai::block> incoming;
    rai::endpoint sender;
};
}

rai::gap_cache::gap_cache () :
max (128)
{
}

void rai::gap_cache::add (rai::block const & block_a, rai::block_hash needed_a)
{
    auto existing (blocks.find (needed_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [] (rai::gap_information & info) {info.arrival = std::chrono::system_clock::now ();});
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

std::unique_ptr <rai::block> rai::gap_cache::get (rai::block_hash const & hash_a)
{
    std::unique_ptr <rai::block> result;
    auto existing (blocks.find (hash_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [&] (rai::gap_information & info) {result.swap (info.block);});
        blocks.erase (existing);
    }
    return result;
}

void rai::votes::start_request (rai::block const & block_a)
{
    auto list (client->peers.list ());
    for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
    {
        client->network.send_confirm_req (i->endpoint, block_a);
    }
}

void rai::votes::announce_vote ()
{
    auto winner_l (winner ());
	assert (winner_l.first != nullptr);
    client->network.confirm_block (std::move (winner_l.first), sequence);
    auto now (std::chrono::system_clock::now ());
    if (now - last_vote < std::chrono::seconds (15))
    {
        auto this_l (shared_from_this ());
        client->service.add (now + std::chrono::seconds (15), [this_l] () {this_l->announce_vote ();});
    }
}

void rai::network::confirm_block (std::unique_ptr <rai::block> block_a, uint64_t sequence_a)
{
    rai::confirm_ack confirm;
    confirm.vote.address = client.representative;
    confirm.vote.sequence = sequence_a;
    confirm.vote.block = std::move (block_a);
    rai::private_key prv;
    auto error (client.wallet.fetch (client.representative, prv));
    assert (!error);
    rai::sign_message (prv, client.representative, confirm.vote.hash (), confirm.vote.signature);
    prv.clear ();
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        confirm.serialize (stream);
    }
    auto & client_l (client);
    auto list (client.peers.list ());
    for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
    {
        client.network.send_buffer (bytes->data (), bytes->size (), i->endpoint, [bytes, &client_l] (boost::system::error_code const & ec, size_t size_a)
            {
                if (network_logging ())
                {
                    if (ec)
                    {
                        client_l.log.add (boost::str (boost::format ("Error broadcasting confirmation: %1%") % ec.message ()));
                    }
                }
            });
    }
}

namespace
{
class root_visitor : public rai::block_visitor
{
public:
    root_visitor (rai::block_store & store_a) :
    store (store_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        result = block_a.previous ();
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        result = block_a.previous ();
    }
    void open_block (rai::open_block const & block_a) override
    {
        auto source (store.block_get (block_a.source ()));
        assert (source != nullptr);
        assert (dynamic_cast <rai::send_block *> (source.get ()) != nullptr);
        result = static_cast <rai::send_block *> (source.get ())->hashables.destination;
    }
    void change_block (rai::change_block const & block_a) override
    {
        result = block_a.previous ();
    }
    rai::block_store & store;
    rai::block_hash result;
};
}

rai::block_hash rai::block_store::root (rai::block const & block_a)
{
    root_visitor visitor (*this);
    block_a.visit (visitor);
    return visitor.result;
}

void rai::processor::process_receive_republish (std::unique_ptr <rai::block> incoming, rai::endpoint const & sender_a)
{
    std::unique_ptr <rai::block> block (std::move (incoming));
    do
    {
        auto hash (block->hash ());
        auto process_result (process_receive (*block));
        switch (process_result)
        {
            case rai::process_result::progress:
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
class receivable_visitor : public rai::block_visitor
{
public:
    receivable_visitor (rai::client & client_a, rai::block const & incoming_a) :
    client (client_a),
    incoming (incoming_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        if (client.wallet.find (block_a.hashables.destination) != client.wallet.end ())
        {
            auto root (incoming.previous ());
            assert (!root.is_zero ());
            client.conflicts.start (block_a, true);
        }
    }
    void receive_block (rai::receive_block const &) override
    {
    }
    void open_block (rai::open_block const &) override
    {
    }
    void change_block (rai::change_block const &) override
    {
    }
    rai::client & client;
    rai::block const & incoming;
};
    
class progress_log_visitor : public rai::block_visitor
{
public:
    progress_log_visitor (rai::client & client_a) :
    client (client_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Sending from:\n\t%1% to:\n\t%2% amount:\n\t%3% previous:\n\t%4% block:\n\t%5%") % client.ledger.account (block_a.hash ()).to_string () % block_a.hashables.destination.to_string () % client.ledger.amount (block_a.hash ()) % block_a.hashables.previous.to_string () % block_a.hash ().to_string ()));
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Receiving from:\n\t%1% to:\n\t%2% previous:\n\t%3% block:\n\t%4%") % client.ledger.account (block_a.hashables.source).to_string () % client.ledger.account (block_a.hash ()).to_string () %block_a.hashables.previous.to_string () % block_a.hash ().to_string ()));
    }
    void open_block (rai::open_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Open from:\n\t%1% to:\n\t%2% block:\n\t%3%") % client.ledger.account (block_a.hashables.source).to_string () % client.ledger.account (block_a.hash ()).to_string () % block_a.hash ().to_string ()));
    }
    void change_block (rai::change_block const & block_a) override
    {
    }
    rai::client & client;
};
	
class successor_visitor : public rai::block_visitor
{
public:
    void send_block (rai::send_block const & block_a) override
    {
    }
    void receive_block (rai::receive_block const & block_a) override
    {
    }
    void open_block (rai::open_block const & block_a) override
    {
    }
    void change_block (rai::change_block const & block_a) override
    {
    }
};
}

rai::process_result rai::processor::process_receive (rai::block const & block_a)
{
    auto result (client.ledger.process (block_a));
    switch (result)
    {
        case rai::process_result::progress:
        {
            if (ledger_logging ())
            {
                progress_log_visitor logger (client);
                block_a.visit (logger);
            }
            receivable_visitor visitor (client, block_a);
            block_a.visit (visitor);
            break;
        }
        case rai::process_result::gap_previous:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Gap previous for: %1%") % block_a.hash ().to_string ()));
            }
            auto previous (block_a.previous ());
            client.gap_cache.add (block_a, previous);
            break;
        }
        case rai::process_result::gap_source:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Gap source for: %1%") % block_a.hash ().to_string ()));
            }
            auto source (block_a.source ());
            client.gap_cache.add (block_a, source);
            break;
        }
        case rai::process_result::old:
        {
            if (ledger_duplicate_logging ())
            {
                client.log.add (boost::str (boost::format ("Old for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::bad_signature:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Bad signature for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::overspend:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Overspend for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::overreceive:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Overreceive for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::not_receive_from_send:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Not receive from spend for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::fork:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Fork for: %1%") % block_a.hash ().to_string ()));
            }
            client.conflicts.start (*client.ledger.successor (client.store.root (block_a)), false);
            break;
        }
    }
    return result;
}

void rai::peer_container::incoming_from_peer (rai::endpoint const & endpoint_a)
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
			peers.modify (existing, [] (rai::peer_information & info) {info.last_contact = std::chrono::system_clock::now (); info.last_attempt = std::chrono::system_clock::now ();});
		}
	}
}

std::vector <rai::peer_information> rai::peer_container::list ()
{
    std::vector <rai::peer_information> result;
    std::lock_guard <std::mutex> lock (mutex);
    result.reserve (peers.size ());
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        result.push_back (*i);
    }
    return result;
}

void rai::keepalive_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.keepalive_req (*this);
}

void rai::keepalive_ack::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.keepalive_ack (*this);
}

void rai::publish_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.publish_req (*this);
}

void rai::keepalive_ack::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::keepalive_ack);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address (i->address ().to_v4 ().to_ulong ());
        write (stream_a, address);
        write (stream_a, i->port ());
    }
	write (stream_a, checksum);
}

bool rai::keepalive_ack::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (type == rai::message_type::keepalive_ack);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = rai::endpoint (boost::asio::ip::address_v4 (address), port);
    }
	read (stream_a, checksum);
    return result;
}

void rai::keepalive_req::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::keepalive_req);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address (i->address ().to_v4 ().to_ulong ());
        write (stream_a, address);
        write (stream_a, i->port ());
    }
}

bool rai::keepalive_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (type == rai::message_type::keepalive_req);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = rai::endpoint (boost::asio::ip::address_v4 (address), port);
    }
    return result;
}

rai::uint256_t rai::ledger::supply ()
{
    return std::numeric_limits <rai::uint256_t>::max ();
}

size_t rai::processor_service::size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return operations.size ();
}

rai::account_iterator::account_iterator (leveldb::DB & db_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

rai::account_iterator::account_iterator (leveldb::DB & db_a, std::nullptr_t) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

void rai::account_iterator::set_current ()
{
    if (iterator->Valid ())
    {
        current.first = iterator->key ();
        auto slice (iterator->value ());
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (slice.data ()), slice.size ());
        auto error (current.second.deserialize (stream));
        assert (!error);
    }
    else
    {
        current.first.clear ();
        current.second.hash.clear ();
        current.second.representative.clear ();
        current.second.time = 0;
    }
}

rai::account_iterator & rai::account_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}
rai::account_entry & rai::account_iterator::operator -> ()
{
    return current;
}

void rai::frontier::serialize (rai::stream & stream_a) const
{
    write (stream_a, hash.bytes);
    write (stream_a, representative.bytes);
    write (stream_a, balance.bytes);
    write (stream_a, time);
}

bool rai::frontier::deserialize (rai::stream & stream_a)
{
    auto result (read (stream_a, hash.bytes));
    if (!result)
    {
        result = read (stream_a, representative.bytes);
        if (!result)
        {
            result = read (stream_a, balance.bytes);
            if (!result)
            {
                result = read (stream_a, time);
            }
        }
    }
    return result;
}

bool rai::account_iterator::operator == (rai::account_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
}

bool rai::account_iterator::operator != (rai::account_iterator const & other_a) const
{
    return !(*this == other_a);
}

rai::block_iterator::block_iterator (leveldb::DB & db_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

rai::block_iterator::block_iterator (leveldb::DB & db_a, std::nullptr_t) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

void rai::block_iterator::set_current ()
{
    if (iterator->Valid ())
    {
        current.first = iterator->key ();
        auto slice (iterator->value ());
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (slice.data ()), slice.size ());
        current.second = rai::deserialize_block (stream);
        assert (current.second != nullptr);
    }
    else
    {
        current.first.clear ();
        current.second.release ();
    }
}

rai::block_iterator & rai::block_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}

rai::block_entry & rai::block_iterator::operator -> ()
{
    return current;
}

bool rai::block_iterator::operator == (rai::block_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
}

bool rai::block_iterator::operator != (rai::block_iterator const & other_a) const
{
    return !(*this == other_a);
}

rai::block_iterator rai::block_store::blocks_begin ()
{
    rai::block_iterator result (*blocks);
    return result;
}

rai::block_iterator rai::block_store::blocks_end ()
{
    rai::block_iterator result (*blocks, nullptr);
    return result;
}

rai::account_iterator rai::block_store::latest_begin ()
{
    rai::account_iterator result (*addresses);
    return result;
}

rai::account_iterator rai::block_store::latest_end ()
{
    rai::account_iterator result (*addresses, nullptr);
    return result;
}

rai::block_entry * rai::block_entry::operator -> ()
{
    return this;
}

rai::account_entry * rai::account_entry::operator -> ()
{
    return this;
}

bool rai::send_block::operator == (rai::send_block const & other_a) const
{
    auto result (signature == other_a.signature && hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance);
    return result;
}

rai::block_hash rai::send_block::previous () const
{
    return hashables.previous;
}

rai::block_hash rai::receive_block::previous () const
{
    return hashables.previous;
}

void amount_visitor::compute (rai::block_hash const & block_hash)
{
    auto block (store.block_get (block_hash));
    assert (block != nullptr);
    block->visit (*this);
}

void balance_visitor::compute (rai::block_hash const & block_hash)
{
    auto block (store.block_get (block_hash));
    assert (block != nullptr);
    block->visit (*this);
}

bool rai::client::send (rai::public_key const & address, rai::uint256_t const & coins)
{
    return transactions.send (address, coins);
}

rai::system::system (uint16_t port_a, size_t count_a) :
service (new boost::asio::io_service)
{
    clients.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        auto client (std::make_shared <rai::client> (service, port_a + i, processor, rai::genesis_address));
        client->start ();
        clients.push_back (client);
    }
    for (auto i (clients.begin ()), j (clients.begin () + 1), n (clients.end ()); j != n; ++i, ++j)
    {
        auto starting1 ((*i)->peers.size ());
        auto starting2 ((*j)->peers.size ());
        (*j)->network.send_keepalive ((*i)->network.endpoint ());
        do {
            service->run_one ();
        } while ((*i)->peers.size () == starting1 || (*j)->peers.size () == starting2);
    }
}

rai::system::~system ()
{
    for (auto & i: clients)
    {
        i->stop ();
    }
}

void rai::processor::process_unknown (rai::vectorstream & stream_a)
{
	rai::confirm_unk outgoing;
	outgoing.rep_hint = client.representative;
	outgoing.serialize (stream_a);
}

void rai::processor::process_confirmation (rai::block const & block_a, rai::endpoint const & sender)
{
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
	{
		rai::vectorstream stream (*bytes);
		if (!client.is_representative ())
		{
			process_unknown (stream);
		}
		else
		{
			auto weight (client.ledger.weight (client.representative));
			if (weight.is_zero ())
			{
				process_unknown (stream);
			}
			else
			{
                rai::private_key prv;
                auto error (client.wallet.fetch (client.representative, prv));
                assert (!error);
				rai::confirm_ack outgoing;
				outgoing.vote.address = client.representative;
                outgoing.vote.block = block_a.clone ();
				outgoing.vote.sequence = 0;
				rai::sign_message (prv, client.representative, outgoing.vote.hash (), outgoing.vote.signature);
				assert (!rai::validate_message (client.representative, outgoing.vote.hash (), outgoing.vote.signature));
                outgoing.serialize (stream);
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

rai::key_entry * rai::key_entry::operator -> ()
{
    return this;
}

bool rai::confirm_ack::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (type == rai::message_type::confirm_ack);
    if (!result)
    {
        result = read (stream_a, vote.address);
        if (!result)
        {
            result = read (stream_a, vote.signature);
            if (!result)
            {
                result = read (stream_a, vote.sequence);
                if (!result)
                {
                    vote.block = rai::deserialize_block (stream_a);
                    result = vote.block == nullptr;
                }
            }
        }
    }
    return result;
}

void rai::confirm_ack::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::confirm_ack);
    write (stream_a, vote.address);
    write (stream_a, vote.signature);
    write (stream_a, vote.sequence);
    rai::serialize_block (stream_a, *vote.block);
}

bool rai::confirm_ack::operator == (rai::confirm_ack const & other_a) const
{
    auto result (vote.address == other_a.vote.address && *vote.block == *other_a.vote.block && vote.signature == other_a.vote.signature && vote.sequence == other_a.vote.sequence);
    return result;
}

void rai::confirm_ack::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_ack (*this);
}

bool rai::confirm_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    read (stream_a, type);
    assert (type == rai::message_type::confirm_req);
    auto result (read (stream_a, work));
    if (!result)
    {
        block = rai::deserialize_block (stream_a);
        result = block == nullptr;
    }
    return result;
}

bool rai::confirm_unk::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    read (stream_a, type);
    assert (type == rai::message_type::confirm_unk);
    auto result (read (stream_a, rep_hint));
    return result;
}

void rai::confirm_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_req (*this);
}

void rai::confirm_unk::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_unk (*this);
}

void rai::confirm_req::serialize (rai::stream & stream_a)
{
    assert (block != nullptr);
    write (stream_a, rai::message_type::confirm_req);
    write (stream_a, work);
    rai::serialize_block (stream_a, *block);
}

rai::rpc::rpc (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, rai::client & client_a, std::unordered_set <rai::uint256_union> const & api_keys_a) :
server (decltype (server)::options (*this).address ("0.0.0.0").port (std::to_string (port_a)).io_service (service_a).thread_pool (pool_a)),
client (client_a),
api_keys (api_keys_a)
{
}

void rai::rpc::start ()
{
    server.listen ();
}

void rai::rpc::stop ()
{
    server.stop ();
}

namespace
{
void set_response (boost::network::http::server <rai::rpc>::response & response, boost::property_tree::ptree & tree)
{
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    response.status = boost::network::http::server <rai::rpc>::response::ok;
    response.headers.push_back (boost::network::http::response_header_narrow {"Content-Type", "application/json"});
    response.content = ostream.str ();
}
}

void rai::rpc::operator () (boost::network::http::server <rai::rpc>::request const & request, boost::network::http::server <rai::rpc>::response & response)
{
    if (request.method == "POST")
    {
        try
        {
            boost::property_tree::ptree request_l;
            std::stringstream istream (request.body);
            boost::property_tree::read_json (istream, request_l);
            std::string key_text (request_l.get <std::string> ("key"));
            rai::uint256_union key;
            auto decode_error (key.decode_hex (key_text));
            if (!decode_error)
            {
                if (api_keys.find (key) != api_keys.end ())
                {
                    std::string action (request_l.get <std::string> ("action"));
                    if (action == "account_balance")
                    {
                        std::string account_text (request_l.get <std::string> ("account"));
                        rai::uint256_union account;
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
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Bad account number";
                        }
                    }
                    else if (action == "wallet_create")
                    {
                        rai::keypair new_key;
                        client.wallet.insert (new_key.prv);
                        boost::property_tree::ptree response_l;
                        std::string account;
                        new_key.pub.encode_hex (account);
                        response_l.put ("account", account);
                        set_response (response, response_l);
                    }
                    else if (action == "wallet_contains")
                    {
                        std::string account_text (request_l.get <std::string> ("account"));
                        rai::uint256_union account;
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
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
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
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Unknown command";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::unauthorized);
                    response.content = "API key is not authorized";
                }
            }
            else
            {
                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::unauthorized);
                response.content = "No API key given";
            }
        }
        catch (std::runtime_error const &)
        {
            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
            response.content = "Unable to parse JSON";
        }
    }
    else
    {
        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::method_not_allowed);
        response.content = "Can only POST requests";
    }
}


void rai::open_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

rai::block_hash rai::open_block::previous () const
{
    rai::block_hash result (0);
    return result;
}

void rai::open_block::serialize (rai::stream & stream_a) const
{
    write (stream_a, hashables.representative);
    write (stream_a, hashables.source);
    write (stream_a, signature);
}

bool rai::open_block::deserialize (rai::stream & stream_a)
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

void rai::open_block::visit (rai::block_visitor & visitor_a) const
{
    visitor_a.open_block (*this);
}

std::unique_ptr <rai::block> rai::open_block::clone () const
{
    return std::unique_ptr <rai::block> (new rai::open_block (*this));
}

rai::block_type rai::open_block::type () const
{
    return rai::block_type::open;
}

bool rai::open_block::operator == (rai::block const & other_a) const
{
    auto other_l (dynamic_cast <rai::open_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool rai::open_block::operator == (rai::open_block const & other_a) const
{
    return hashables.representative == other_a.hashables.representative && hashables.source == other_a.hashables.source && signature == other_a.signature;
}

void rai::open_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash_a.Update (source.bytes.data (), sizeof (source.bytes));
}

rai::uint256_t rai::block_store::representation_get (rai::address const & address_a)
{
    std::string value;
    auto status (representation->Get (leveldb::ReadOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    rai::uint256_t result;
    if (status.ok ())
    {
        rai::uint256_union rep;
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
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

void rai::block_store::representation_put (rai::address const & address_a, rai::uint256_t const & representation_a)
{
    rai::uint256_union rep (representation_a);
    auto status (representation->Put (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), leveldb::Slice (rep.chars.data (), rep.chars.size ())));
    assert (status.ok ());
}

rai::address rai::ledger::representative (rai::block_hash const & hash_a)
{
    auto result (representative_calculated (hash_a));
    //assert (result == representative_cached (hash_a));
    return result;
}

rai::address rai::ledger::representative_calculated (rai::block_hash const & hash_a)
{
	representative_visitor visitor (store);
	visitor.compute (hash_a);
	return visitor.result;
}

rai::address rai::ledger::representative_cached (rai::block_hash const & hash_a)
{
    assert (false);
}

rai::uint256_t rai::ledger::weight (rai::address const & address_a)
{
    return store.representation_get (address_a);
}

void rai::confirm_unk::serialize (rai::stream & stream_a)
{
    write (stream_a, rep_hint);
}

void rai::change_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

rai::block_hash rai::change_block::previous () const
{
    return hashables.previous;
}

void rai::change_block::serialize (rai::stream & stream_a) const
{
    write (stream_a, hashables.representative);
    write (stream_a, hashables.previous);
    write (stream_a, signature);
}

bool rai::change_block::deserialize (rai::stream & stream_a)
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

void rai::change_block::visit (rai::block_visitor & visitor_a) const
{
    visitor_a.change_block (*this);
}

std::unique_ptr <rai::block> rai::change_block::clone () const
{
    return std::unique_ptr <rai::block> (new rai::change_block (*this));
}

rai::block_type rai::change_block::type () const
{
    return rai::block_type::change;
}

bool rai::change_block::operator == (rai::block const & other_a) const
{
    auto other_l (dynamic_cast <rai::change_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool rai::change_block::operator == (rai::change_block const & other_a) const
{
    return signature == other_a.signature && hashables.representative == other_a.hashables.representative && hashables.previous == other_a.hashables.previous;
}

void rai::change_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
}

std::unique_ptr <rai::block> rai::block_store::fork_get (rai::block_hash const & hash_a)
{
    std::string value;
    auto status (forks->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <rai::block> result;
    if (status.ok ())
    {
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = rai::deserialize_block (stream);
        assert (result != nullptr);
    }
    return result;
}

void rai::block_store::fork_put (rai::block_hash const & hash_a, rai::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        rai::serialize_block (stream, block_a);
    }
    auto status (forks->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

bool rai::uint256_union::operator != (rai::uint256_union const & other_a) const
{
    return ! (*this == other_a);
}

namespace
{
class rollback_visitor : public rai::block_visitor
{
public:
    rollback_visitor (rai::ledger & ledger_a) :
    ledger (ledger_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
		auto hash (block_a.hash ());
        rai::address sender;
        rai::uint256_union amount;
        rai::address destination;
		while (ledger.store.pending_get (hash, sender, amount, destination))
		{
			ledger.rollback (ledger.latest (block_a.hashables.destination));
		}
        rai::frontier frontier;
        ledger.store.latest_get (sender, frontier);
		ledger.store.pending_del (hash);
        ledger.change_latest (sender, block_a.hashables.previous, frontier.representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (hash);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
		auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_address (ledger.account (hash));
		ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_address, block_a.hashables.previous, representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source, ledger.account (block_a.hashables.source), amount, destination_address);
    }
    void open_block (rai::open_block const & block_a) override
    {
		auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_address (ledger.account (hash));
		ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_address, 0, representative, 0);
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source, ledger.account (block_a.hashables.source), amount, destination_address);
    }
    void change_block (rai::change_block const & block_a) override
    {
        auto representative (ledger.representative (block_a.hashables.previous));
        auto account (ledger.account (block_a.hashables.previous));
        rai::frontier frontier;
        ledger.store.latest_get (account, frontier);
		ledger.move_representation (block_a.hashables.representative, representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (block_a.hash ());
        ledger.change_latest (account, block_a.hashables.previous, representative, frontier.balance);
    }
    rai::ledger & ledger;
};
}

void rai::block_store::block_del (rai::block_hash const & hash_a)
{
    auto status (blocks->Delete (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

void rai::ledger::rollback (rai::block_hash const & frontier_a)
{
	auto account_l (account (frontier_a));
    rollback_visitor rollback (*this);
    rai::frontier frontier;
	do
	{
		auto latest_error (store.latest_get (account_l, frontier));
		assert (!latest_error);
        auto block (store.block_get (frontier.hash));
        block->visit (rollback);
		
	} while (frontier.hash != frontier_a);
}

rai::address rai::ledger::account (rai::block_hash const & hash_a)
{
	account_visitor account (store);
	account.compute (hash_a);
	return account.result;
}

rai::uint256_t rai::ledger::amount (rai::block_hash const & hash_a)
{
	amount_visitor amount (store);
	amount.compute (hash_a);
	return amount.result;
}

void rai::ledger::move_representation (rai::address const & source_a, rai::address const & destination_a, rai::uint256_t const & amount_a)
{
	auto source_previous (store.representation_get (source_a));
	assert (source_previous >= amount_a);
    store.representation_put (source_a, source_previous - amount_a);
    auto destination_previous (store.representation_get (destination_a));
    store.representation_put (destination_a, destination_previous + amount_a);
}

rai::block_hash rai::ledger::latest (rai::address const & address_a)
{
    rai::frontier frontier;
	auto latest_error (store.latest_get (address_a, frontier));
	assert (!latest_error);
	return frontier.hash;
}

void rai::block_store::latest_del (rai::address const & address_a)
{
    auto status (addresses->Delete (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ())));
    assert (status.ok ());
}

bool rai::block_store::latest_exists (rai::address const & address_a)
{
    std::unique_ptr <leveldb::Iterator> existing (addresses->NewIterator (leveldb::ReadOptions {}));
    existing->Seek (leveldb::Slice (address_a.chars.data (), address_a.chars.size ()));
    bool result;
    if (existing->Valid ())
    {
        result = true;
    }
    else
    {
        result = false;
    }
    return result;
}

rai::uint256_union rai::vote::hash () const
{
	rai::uint256_union result;
    CryptoPP::SHA3 hash (32);
    hash.Update (block->hash ().bytes.data (), sizeof (result.bytes));
    union {
        uint64_t qword;
        std::array <uint8_t, 8> bytes;
    };
    qword = sequence;
    //std::reverse (bytes.begin (), bytes.end ());
    hash.Update (bytes.data (), sizeof (bytes));
    hash.Final (result.bytes.data ());
	return result;
}

rai::uint256_union rai::block::hash () const
{
    CryptoPP::SHA3 hash_l (32);
    hash (hash_l);
    rai::uint256_union result;
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

void rai::uint256_union::encode_base58check (std::string & destination_a) const
{
    assert (destination_a.empty ());
    destination_a.reserve (50);
    uint32_t check;
    CryptoPP::SHA3 hash (4);
    hash.Update (bytes.data (), sizeof (bytes));
    hash.Final (reinterpret_cast <uint8_t *> (&check));
    rai::uint512_t number_l (number ());
    number_l |= rai::uint512_t (check) << 256;
    number_l |= rai::uint512_t (13) << (256 + 32);
    while (!number_l.is_zero ())
    {
        auto r ((number_l % 58).convert_to <uint8_t> ());
        number_l /= 58;
        destination_a.push_back (base58_encode (r));
    }
    std::reverse (destination_a.begin (), destination_a.end ());
}

bool rai::uint256_union::decode_base58check (std::string const & source_a)
{
    auto result (source_a.size () != 50);
    if (!result)
    {
        rai::uint512_t number_l;
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
            *this = number_l.convert_to <rai::uint256_t> ();
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

bool rai::parse_endpoint (std::string const & string, rai::endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = rai::endpoint (address, port);
    }
    return result;
}

bool rai::parse_tcp_endpoint (std::string const & string, rai::tcp_endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = rai::tcp_endpoint (address, port);
    }
    return result;
}

void rai::bulk_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.bulk_req (*this);
}

bool rai::bulk_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    if (!result)
    {
        assert (type == rai::message_type::bulk_req);
        result = read (stream_a, start);
        if (!result)
        {
            result = read (stream_a, end);
        }
    }
    return result;
}

void rai::bulk_req::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::bulk_req);
    write (stream_a, start);
    write (stream_a, end);
}

void rai::client::start ()
{
    network.receive ();
    processor.ongoing_keepalive ();
    bootstrap.start ();
}

void rai::client::stop ()
{
    network.stop ();
    bootstrap.stop ();
    processor.stop ();
}

void rai::processor::bootstrap (boost::asio::ip::tcp::endpoint const & endpoint_a, std::function <void ()> const & complete_action_a)
{
    auto processor (std::make_shared <rai::bootstrap_initiator> (client.shared (), complete_action_a));
    processor->run (endpoint_a);
}

rai::bootstrap_receiver::bootstrap_receiver (boost::asio::io_service & service_a, uint16_t port_a, rai::client & client_a) :
acceptor (service_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::any (), port_a)),
service (service_a),
client (client_a)
{
}

void rai::bootstrap_receiver::start ()
{
    acceptor.open (local.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (local);
    acceptor.listen ();
    accept_connection ();
}

void rai::bootstrap_receiver::stop ()
{
    on = false;
    acceptor.close ();
}

void rai::bootstrap_receiver::accept_connection ()
{
    auto socket (std::make_shared <boost::asio::ip::tcp::socket> (service));
    acceptor.async_accept (*socket, [this, socket] (boost::system::error_code const & error) {accept_action (error, socket); accept_connection ();});
}

void rai::bootstrap_receiver::accept_action (boost::system::error_code const & ec, std::shared_ptr <boost::asio::ip::tcp::socket> socket_a)
{
    auto connection (std::make_shared <rai::bootstrap_connection> (socket_a, client.shared ()));
    connection->receive ();
}

rai::bootstrap_connection::bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket> socket_a, std::shared_ptr <rai::client> client_a) :
socket (socket_a),
client (client_a)
{
}

void rai::bootstrap_connection::receive ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->receive_type_action (ec, size_a);});
}

void rai::bootstrap_connection::receive_type_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
        rai::bufferstream type_stream (receive_buffer.data (), size_a);
        rai::message_type type;
        read (type_stream, type);
        switch (type)
        {
            case rai::message_type::bulk_req:
            {
                auto this_l (shared_from_this ());
                boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->receive_bulk_req_action (ec, size_a);});
                break;
            }
			case rai::message_type::frontier_req:
			{
				auto this_l (shared_from_this ());
				boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t)), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->receive_frontier_req_action (ec, size_a);});
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

void rai::bootstrap_connection::receive_bulk_req_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        std::unique_ptr <rai::bulk_req> request (new rai::bulk_req);
        rai::bufferstream stream (receive_buffer.data (), sizeof (rai::message_type) + sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        auto error (request->deserialize (stream));
        if (!error)
        {
            receive ();
            if (network_logging ())
            {
                client->log.add (boost::str (boost::format ("Received bulk request for %1% down to %2%") % request->start.to_string () % request->end.to_string ()));
            }
			add_request (std::unique_ptr <rai::message> (request.release ()));
        }
    }
}

void rai::bootstrap_connection::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
		rai::bufferstream stream (receive_buffer.data (), sizeof (rai::message_type) + sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			receive ();
			if (network_logging ())
			{
				client->log.add (boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age));
			}
			add_request (std::unique_ptr <rai::message> (request.release ()));
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

void rai::bootstrap_connection::add_request (std::unique_ptr <rai::message> message_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next ();
	}
}

void rai::bootstrap_connection::finish_request ()
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
class request_response_visitor : public rai::message_visitor
{
public:
    request_response_visitor (std::shared_ptr <rai::bootstrap_connection> connection_a) :
    connection (connection_a)
    {
    }
    void keepalive_req (rai::keepalive_req const &)
    {
        assert (false);
    }
    void keepalive_ack (rai::keepalive_ack const &)
    {
        assert (false);
    }
    void publish_req (rai::publish_req const &)
    {
        assert (false);
    }
    void confirm_req (rai::confirm_req const &)
    {
        assert (false);
    }
    void confirm_ack (rai::confirm_ack const &)
    {
        assert (false);
    }
    void confirm_unk (rai::confirm_unk const &)
    {
        assert (false);
    }
    void bulk_req (rai::bulk_req const &)
    {
        auto response (std::make_shared <rai::bulk_req_response> (connection, std::unique_ptr <rai::bulk_req> (static_cast <rai::bulk_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    void frontier_req (rai::frontier_req const &)
    {
        auto response (std::make_shared <rai::frontier_req_response> (connection, std::unique_ptr <rai::frontier_req> (static_cast <rai::frontier_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    std::shared_ptr <rai::bootstrap_connection> connection;
};
}

void rai::bootstrap_connection::run_next ()
{
	assert (!requests.empty ());
    request_response_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void rai::bulk_req_response::set_current_end ()
{
    assert (request != nullptr);
    auto end_exists (request->end.is_zero () || connection->client->store.block_exists (request->end));
    if (end_exists)
    {
        rai::frontier frontier;
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

void rai::bulk_req_response::send_next ()
{
    std::unique_ptr <rai::block> block (get_next ());
    if (block != nullptr)
    {
        {
            send_buffer.clear ();
            rai::vectorstream stream (send_buffer);
            rai::serialize_block (stream, *block);
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

std::unique_ptr <rai::block> rai::bulk_req_response::get_next ()
{
    std::unique_ptr <rai::block> result;
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

void rai::bulk_req_response::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
}

void rai::bulk_req_response::send_finished ()
{
    send_buffer.clear ();
    send_buffer.push_back (static_cast <uint8_t> (rai::block_type::not_a_block));
    auto this_l (shared_from_this ());
    if (network_logging ())
    {
        connection->client->log.add ("Bulk sending finished");
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->no_block_sent (ec, size_a);});
}

void rai::bulk_req_response::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
		connection->finish_request ();
    }
}

rai::account_iterator rai::block_store::latest_begin (rai::address const & address_a)
{
    rai::account_iterator result (*addresses, address_a);
    return result;
}

rai::account_iterator::account_iterator (leveldb::DB & db_a, rai::address const & address_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->Seek (leveldb::Slice (address_a.chars.data (), address_a.chars.size ()));
    set_current ();
}

namespace
{
class request_visitor : public rai::message_visitor
{
public:
    request_visitor (std::shared_ptr <rai::bootstrap_initiator> connection_a) :
    connection (connection_a)
    {
    }
    void keepalive_req (rai::keepalive_req const &)
    {
        assert (false);
    }
    void keepalive_ack (rai::keepalive_ack const &)
    {
        assert (false);
    }
    void publish_req (rai::publish_req const &)
    {
        assert (false);
    }
    void confirm_req (rai::confirm_req const &)
    {
        assert (false);
    }
    void confirm_ack (rai::confirm_ack const &)
    {
        assert (false);
    }
    void confirm_unk (rai::confirm_unk const &)
    {
        assert (false);
    }
    void bulk_req (rai::bulk_req const &)
    {
        auto response (std::make_shared <rai::bulk_req_initiator> (connection, std::unique_ptr <rai::bulk_req> (static_cast <rai::bulk_req *> (connection->requests.front ().release ()))));
        response->receive_block ();
    }
    void frontier_req (rai::frontier_req const &)
    {
        auto response (std::make_shared <rai::frontier_req_initiator> (connection, std::unique_ptr <rai::frontier_req> (static_cast <rai::frontier_req *> (connection->requests.front ().release ()))));
        response->receive_frontier ();
    }
    std::shared_ptr <rai::bootstrap_initiator> connection;
};
}

rai::bootstrap_initiator::bootstrap_initiator (std::shared_ptr <rai::client> client_a, std::function <void ()> const & complete_action_a) :
client (client_a),
socket (client_a->network.service),
complete_action (complete_action_a)
{
}

void rai::bootstrap_initiator::run (boost::asio::ip::tcp::endpoint const & endpoint_a)
{
    if (network_logging ())
    {
        client->log.add (boost::str (boost::format ("Initiating bootstrap connection to %1%") % endpoint_a));
    }
    auto this_l (shared_from_this ());
    socket.async_connect (endpoint_a, [this_l] (boost::system::error_code const & ec) {this_l->connect_action (ec);});
}

void rai::bootstrap_initiator::connect_action (boost::system::error_code const & ec)
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

void rai::bootstrap_initiator::send_frontier_request ()
{
    std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
    request->start.clear ();
    request->age = std::numeric_limits <decltype (request->age)>::max ();
    request->count = std::numeric_limits <decltype (request->age)>::max ();
    add_request (std::move (request));
}

void rai::bootstrap_initiator::sent_request (boost::system::error_code const & ec, size_t size_a)
{
    if (ec)
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ()));
        }
    }
}

void rai::bootstrap_initiator::add_request (std::unique_ptr <rai::message> message_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    send_buffer.clear ();
    {
        rai::vectorstream stream (send_buffer);
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

void rai::bootstrap_initiator::run_receiver ()
{
    assert (!mutex.try_lock ());
    assert (requests.front () != nullptr);
    request_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void rai::bootstrap_initiator::finish_request ()
{
    std::lock_guard <std::mutex> lock (mutex);
    assert (!requests.empty ());
    requests.pop ();
    if (!requests.empty ())
    {
        run_receiver ();
    }
}

void rai::bulk_req_initiator::receive_block ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_type (ec, size_a);});
}

void rai::bulk_req_initiator::received_type (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        auto this_l (shared_from_this ());
        rai::block_type type (static_cast <rai::block_type> (receive_buffer [0]));
        switch (type)
        {
            case rai::block_type::send:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 64 + 32 + 32 + 32), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case rai::block_type::receive:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 64 + 32 + 32), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case rai::block_type::open:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 32 + 32 + 64), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case rai::block_type::change:
            {
                boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, 32 + 32 + 64), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_block (ec, size_a);});
                break;
            }
            case rai::block_type::not_a_block:
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
class observed_visitor : public rai::block_visitor
{
public:
    observed_visitor () :
    address (0)
    {
    }
    void send_block (rai::send_block const & block_a)
    {
        address = block_a.hashables.destination;
    }
    void receive_block (rai::receive_block const &)
    {
    }
    void open_block (rai::open_block const &)
    {
    }
    void change_block (rai::change_block const &)
    {
    }
    rai::address address;
};
}

bool rai::bulk_req_initiator::process_end ()
{
    bool result;
    if (expecting == request->end)
    {
        rai::process_result processing;
        std::unique_ptr <rai::block> block;
        do
        {
            block = connection->client->store.bootstrap_get (expecting);
            if (block != nullptr)
            {
                processing = connection->client->processor.process_receive (*block);
                expecting = block->hash ();
            }
        } while (block != nullptr && processing == rai::process_result::progress);
        result = processing != rai::process_result::progress;
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

rai::block_hash rai::genesis::hash () const
{
    return open.hash ();
}

void rai::bulk_req_initiator::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		rai::bufferstream stream (receive_buffer.data (), 1 + size_a);
		auto block (rai::deserialize_block (stream));
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

bool rai::bulk_req_initiator::process_block (rai::block const & block)
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

bool rai::block_store::block_exists (rai::block_hash const & hash_a)
{
    bool result;
    std::unique_ptr <leveldb::Iterator> iterator (blocks->NewIterator (leveldb::ReadOptions ()));
    iterator->Seek (leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()));
    if (iterator->Valid ())
    {
        rai::uint256_union hash;
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

void rai::block_store::bootstrap_put (rai::block_hash const & hash_a, rai::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        rai::serialize_block (stream, block_a);
    }
    auto status (bootstrap->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok () | status.IsNotFound ());
}

std::unique_ptr <rai::block> rai::block_store::bootstrap_get (rai::block_hash const & hash_a)
{
    std::string value;
    auto status (bootstrap->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <rai::block> result;
    if (status.ok ())
    {
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = rai::deserialize_block (stream);
        assert (result != nullptr);
    }
    return result;
}

void rai::block_store::bootstrap_del (rai::block_hash const & hash_a)
{
    auto status (bootstrap->Delete (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

rai::endpoint rai::network::endpoint ()
{
    return rai::endpoint (boost::asio::ip::address_v4::loopback (), socket.local_endpoint ().port ());
}

boost::asio::ip::tcp::endpoint rai::bootstrap_receiver::endpoint ()
{
    return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), local.port ());
}

bool rai::uint256_union::is_zero () const
{
    return qwords [0] == 0 && qwords [1] == 0 && qwords [2] == 0 && qwords [3] == 0;
}

std::string rai::uint256_union::to_string () const
{
    std::string result;
    encode_hex (result);
    return result;
}

bool rai::uint256_union::operator < (rai::uint256_union const & other_a) const
{
    return number () < other_a.number ();
}

rai::bootstrap_initiator::~bootstrap_initiator ()
{
    complete_action ();
    if (network_logging ())
    {
        client->log.add ("Exiting bootstrap processor");
    }
}

rai::bootstrap_connection::~bootstrap_connection ()
{
    if (network_logging ())
    {
        client->log.add ("Exiting bootstrap connection");
    }
}

void rai::peer_container::random_fill (std::array <rai::endpoint, 24> & target_a)
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
    std::fill (target_a.begin () + std::min (peers.size (), target_a.size ()), target_a.end (), rai::endpoint ());
}

void rai::processor::ongoing_keepalive ()
{
    auto peers (client.peers.purge_list (std::chrono::system_clock::now () - cutoff));
    for (auto i (peers.begin ()), j (peers.end ()); i != j && std::chrono::system_clock::now () - i->last_attempt > period; ++i)
    {
        client.network.send_keepalive (i->endpoint);
    }
    client.service.add (std::chrono::system_clock::now () + period, [this] () { ongoing_keepalive ();});
}

std::vector <rai::peer_information> rai::peer_container::purge_list (std::chrono::system_clock::time_point const & cutoff)
{
    std::unique_lock <std::mutex> lock (mutex);
    auto pivot (peers.get <1> ().lower_bound (cutoff));
    std::vector <rai::peer_information> result (pivot, peers.get <1> ().end ());
    peers.get <1> ().erase (peers.get <1> ().begin (), pivot);
    return result;
}

size_t rai::peer_container::size ()
{
    std::unique_lock <std::mutex> lock (mutex);
    return peers.size ();
}

bool rai::peer_container::empty ()
{
    return size () == 0;
}

bool rai::peer_container::contacting_peer (rai::endpoint const & endpoint_a)
{
	auto result (rai::reserved_address (endpoint_a));
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

bool rai::reserved_address (rai::endpoint const & endpoint_a)
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

rai::peer_container::peer_container (rai::endpoint const & self_a) :
self (self_a)
{
}

rai::block_hash rai::send_block::source () const
{
    return 0;
}

rai::block_hash rai::receive_block::source () const
{
    return hashables.source;
}

rai::block_hash rai::open_block::source () const
{
    return hashables.source;
}

rai::block_hash rai::change_block::source () const
{
    return 0;
}

void rai::log::add (std::string const & string_a)
{
    if (log_to_cerr ())
    {
        std::cerr << string_a << std::endl;
    }
    items.push_back (std::make_pair (std::chrono::system_clock::now (), string_a));
}

void rai::log::dump_cerr ()
{
    for (auto & i: items)
    {
        std::cerr << i.first << ' ' << i.second << std::endl;
    }
}

rai::log::log () :
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

void rai::network::send_buffer (uint8_t const * data_a, size_t size_a, rai::endpoint const & endpoint_a, std::function <void (boost::system::error_code const &, size_t)> callback_a)
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

void rai::network::send_complete (boost::system::error_code const & ec, size_t size_a)
{
    if (network_packet_logging ())
    {
        client.log.add ("Packet send complete");
    }
    std::tuple <uint8_t const *, size_t, rai::endpoint, std::function <void (boost::system::error_code const &, size_t)>> self;
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

uint64_t rai::block_store::now ()
{
    boost::posix_time::ptime epoch (boost::gregorian::date (1970, 1, 1));
    auto now (boost::posix_time::second_clock::universal_time ());
    auto diff (now - epoch);
    return diff.total_seconds ();
}

rai::bulk_req_response::bulk_req_response (std::shared_ptr <rai::bootstrap_connection> const & connection_a, std::unique_ptr <rai::bulk_req> request_a) :
connection (connection_a),
request (std::move (request_a))
{
    set_current_end ();
}

rai::frontier_req_response::frontier_req_response (std::shared_ptr <rai::bootstrap_connection> const & connection_a, std::unique_ptr <rai::frontier_req> request_a) :
iterator (connection_a->client->store.latest_begin (request_a->start)),
connection (connection_a),
request (std::move (request_a))
{
    skip_old ();
}

void rai::frontier_req_response::skip_old ()
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

void rai::frontier_req_response::send_next ()
{
	auto pair (get_next ());
    if (!pair.first.is_zero ())
    {
        {
            send_buffer.clear ();
            rai::vectorstream stream (send_buffer);
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

void rai::frontier_req_response::send_finished ()
{
    {
        send_buffer.clear ();
        rai::vectorstream stream (send_buffer);
        rai::uint256_union zero (0);
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

void rai::frontier_req_response::no_block_sent (boost::system::error_code const & ec, size_t size_a)
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

void rai::frontier_req_response::sent_action (boost::system::error_code const & ec, size_t size_a)
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

std::pair <rai::uint256_union, rai::uint256_union> rai::frontier_req_response::get_next ()
{
    std::pair <rai::uint256_union, rai::uint256_union> result (0, 0);
    if (iterator != connection->client->ledger.store.latest_end ())
    {
        result.first = iterator->first;
        result.second = iterator->second.hash;
        ++iterator;
    }
    return result;
}

bool rai::frontier_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    if (!result)
    {
        assert (type == rai::message_type::frontier_req);
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

void rai::frontier_req::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::frontier_req);
    write (stream_a, start.bytes);
    write (stream_a, age);
    write (stream_a, count);
}

void rai::frontier_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.frontier_req (*this);
}

bool rai::frontier_req::operator == (rai::frontier_req const & other_a) const
{
    return start == other_a.start && age == other_a.age && count == other_a.count;
}

rai::bulk_req_initiator::bulk_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const & connection_a, std::unique_ptr <rai::bulk_req> request_a) :
request (std::move (request_a)),
expecting (request->start),
connection (connection_a)
{
    assert (!connection_a->requests.empty ());
    assert (connection_a->requests.front () == nullptr);
}

rai::bulk_req_initiator::~bulk_req_initiator ()
{
    if (network_logging ())
    {
        connection->client->log.add ("Exiting bulk_req initiator");
    }
}

rai::frontier_req_initiator::frontier_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const & connection_a, std::unique_ptr <rai::frontier_req> request_a) :
request (std::move (request_a)),
connection (connection_a)
{
}

rai::frontier_req_initiator::~frontier_req_initiator ()
{
    if (network_logging ())
    {
        connection->client->log.add ("Exiting frontier_req initiator");
    }
}

void rai::frontier_req_initiator::receive_frontier ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a) {this_l->received_frontier (ec, size_a);});
}

void rai::frontier_req_initiator::received_frontier (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        rai::address address;
        rai::bufferstream address_stream (receive_buffer.data (), sizeof (rai::uint256_union));
        auto error1 (address.deserialize (address_stream));
        assert (!error1);
        rai::block_hash latest;
        rai::bufferstream latest_stream (receive_buffer.data () + sizeof (rai::uint256_union), sizeof (rai::uint256_union));
        auto error2 (latest.deserialize (latest_stream));
        assert (!error2);
        if (!address.is_zero ())
        {
            rai::frontier frontier;
            auto unknown (connection->client->store.latest_get (address, frontier));
            if (unknown)
            {
                std::unique_ptr <rai::bulk_req> request (new rai::bulk_req);
                request->start = address;
                request->end.clear ();
                connection->add_request (std::move (request));
            }
            else
            {
                auto exists (connection->client->store.block_exists (latest));
                if (!exists)
                {
                    std::unique_ptr <rai::bulk_req> request (new rai::bulk_req);
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

void rai::block_store::checksum_put (uint64_t prefix, uint8_t mask, rai::uint256_union const & hash_a)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    auto status (checksum->Put (leveldb::WriteOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

bool rai::block_store::checksum_get (uint64_t prefix, uint8_t mask, rai::uint256_union & hash_a)
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
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error (hash_a.deserialize (stream));
        assert (!error);
    }
    else
    {
        result = true;
    }
    return result;
}

void rai::block_store::checksum_del (uint64_t prefix, uint8_t mask)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    checksum->Delete (leveldb::WriteOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)));
}

rai::uint256_union & rai::uint256_union::operator ^= (rai::uint256_union const & other_a)
{
    auto j (other_a.qwords.begin ());
    for (auto i (qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j)
    {
        *i ^= *j;
    }
    return *this;
}

rai::uint256_union rai::uint256_union::operator ^ (rai::uint256_union const & other_a) const
{
    rai::uint256_union result;
    auto k (result.qwords.begin ());
    for (auto i (qwords.begin ()), j (other_a.qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j, ++k)
    {
        *k = *i ^ *j;
    }
    return result;
}

rai::checksum rai::ledger::checksum (rai::address const & begin_a, rai::address const & end_a)
{
    rai::checksum result;
    auto error (store.checksum_get (0, 0, result));
    assert (!error);
    return result;
}

void rai::ledger::checksum_update (rai::block_hash const & hash_a)
{
    rai::checksum value;
    auto error (store.checksum_get (0, 0, value));
    assert (!error);
    value ^= hash_a;
    store.checksum_put (0, 0, value);
}

void rai::ledger::change_latest (rai::address const & address_a, rai::block_hash const & hash_a, rai::address const & representative_a, rai::uint256_union const & balance_a)
{
    rai::frontier frontier;
    auto exists (!store.latest_get (address_a, frontier));
    if (exists)
    {
        checksum_update (frontier.hash);
    }
    if (!hash_a.is_zero())
    {
        frontier.hash = hash_a;
        frontier.representative = representative_a;
        frontier.balance = balance_a;
        frontier.time = store.now ();
        store.latest_put (address_a, frontier);
        checksum_update (hash_a);
    }
    else
    {
        store.latest_del (address_a);
    }
}

bool rai::keepalive_ack::operator == (rai::keepalive_ack const & other_a) const
{
	return (peers == other_a.peers) && (checksum == other_a.checksum);
}

bool rai::peer_container::known_peer (rai::endpoint const & endpoint_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (peers.find (endpoint_a));
    return existing != peers.end () && existing->last_contact > std::chrono::system_clock::now () - rai::processor::cutoff;
}

std::shared_ptr <rai::client> rai::client::shared ()
{
    return shared_from_this ();
}

namespace
{
class traffic_generator : public std::enable_shared_from_this <traffic_generator>
{
public:
    traffic_generator (uint32_t count_a, uint32_t wait_a, std::shared_ptr <rai::client> client_a, rai::system & system_a) :
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
    std::shared_ptr <rai::client> client;
    rai::system & system;
};
}

void rai::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a)
{
    for (size_t i (0), n (clients.size ()); i != n; ++i)
    {
        generate_usage_traffic (count_a, wait_a, i);
    }
}

void rai::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a, size_t index_a)
{
    assert (clients.size () > index_a);
    assert (count_a > 0);
    auto generate (std::make_shared <traffic_generator> (count_a, wait_a, clients [index_a], *this));
    generate->run ();
}

void rai::system::generate_activity (rai::client & client_a)
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
    size_t polled;
    do
    {
        polled = 0;
        polled += service->poll ();
        polled += processor.poll ();
    } while (polled != 0);
}

rai::uint256_t rai::system::get_random_amount (rai::client & client_a)
{
    rai::uint512_t balance (client_a.balance ());
    std::string balance_text (balance.convert_to <std::string> ());
    rai::uint256_union random_amount;
    random_pool.GenerateBlock (random_amount.bytes.data (), sizeof (random_amount.bytes));
    auto result (((rai::uint512_t {random_amount.number ()} * balance) / rai::uint512_t {std::numeric_limits <rai::uint256_t>::max ()}).convert_to <rai::uint256_t> ());
    std::string text (result.convert_to <std::string> ());
    return result;
}

void rai::system::generate_send_existing (rai::client & client_a)
{
    rai::address account;
    random_pool.GenerateBlock (account.bytes.data (), sizeof (account.bytes));
    rai::account_iterator entry (client_a.store.latest_begin (account));
    if (entry == client_a.store.latest_end ())
    {
        entry = client_a.store.latest_begin ();
    }
    assert (entry != client_a.store.latest_end ());
    client_a.send (entry->first, get_random_amount (client_a));
}

void rai::system::generate_send_new (rai::client & client_a)
{
    rai::keypair key;
    client_a.wallet.insert (key.prv);
    client_a.send (key.pub, get_random_amount (client_a));
}

void rai::system::generate_mass_activity (uint32_t count_a, rai::client & client_a)
{
    auto previous (std::chrono::system_clock::now ());
    for (uint32_t i (0); i < count_a; ++i)
    {
        if ((i & 0x3ff) == 0)
        {
            auto now (std::chrono::system_clock::now ());
            auto ms (std::chrono::duration_cast <std::chrono::milliseconds> (now - previous).count ());
            std::cerr << boost::str (boost::format ("Mass activity iteration %1% ms %2% ms/t %3%\n") % i % ms % (ms / 256));
            previous = now;
        }
        generate_activity (client_a);
    }
}

rai::uint256_t rai::client::balance ()
{
    rai::uint256_t result;
    for (auto i (wallet.begin ()), n (wallet.end ()); i !=  n; ++i)
    {
        auto pub (i->first);
        auto account_balance (ledger.account_balance (pub));
        result += account_balance;
    }
    return result;
}

rai::transactions::transactions (rai::ledger & ledger_a, rai::wallet & wallet_a, rai::processor & processor_a) :
ledger (ledger_a),
wallet (wallet_a),
processor (processor_a)
{
}

bool rai::transactions::receive (rai::send_block const & send_a, rai::private_key const & prv_a, rai::address const & representative_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto hash (send_a.hash ());
    bool result;
    if (ledger.store.pending_exists (hash))
    {
        rai::frontier frontier;
        auto new_address (ledger.store.latest_get (send_a.hashables.destination, frontier));
        if (new_address)
        {
            auto open (new rai::open_block);
            open->hashables.source = hash;
            open->hashables.representative = representative_a;
            rai::sign_message (prv_a, send_a.hashables.destination, open->hash (), open->signature);
            processor.process_receive_republish (std::unique_ptr <rai::block> (open), rai::endpoint {});
        }
        else
        {
            auto receive (new rai::receive_block);
            receive->hashables.previous = frontier.hash;
            receive->hashables.source = hash;
            rai::sign_message (prv_a, send_a.hashables.destination, receive->hash (), receive->signature);
            processor.process_receive_republish (std::unique_ptr <rai::block> (receive), rai::endpoint {});
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

bool rai::transactions::send (rai::address const & address_a, rai::uint256_t const & coins_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    std::vector <std::unique_ptr <rai::send_block>> blocks;
    auto result (wallet.generate_send (ledger, address_a, coins_a, blocks));
    if (!result)
    {
        for (auto i (blocks.begin ()), j (blocks.end ()); i != j; ++i)
        {
            processor.process_receive_republish (std::move (*i), rai::endpoint {});
        }
    }
    return result;
}

bool rai::frontier::operator == (rai::frontier const & other_a) const
{
    return hash == other_a.hash && representative == other_a.representative && balance == other_a.balance && time == other_a.time;
}

void rai::votes::vote (rai::vote const & vote_a)
{
    if (!rai::validate_message (vote_a.address, vote_a.hash (), vote_a.signature))
    {
        auto existing (rep_votes.find (vote_a.address));
        if (existing == rep_votes.end ())
        {
            rep_votes.insert (std::make_pair (vote_a.address, std::make_pair (vote_a.sequence, vote_a.block->clone ())));
        }
        else
        {
            if (existing->second.first < vote_a.sequence)
            {
                existing->second.second = vote_a.block->clone ();
            }
        }
        assert (rep_votes.size () > 0);
        auto winner_l (winner ());
        if (winner_l.second > flip_threshold ())
        {
            if (!(*winner_l.first == *last_winner))
            {
                client->ledger.rollback (last_winner->hash ());
                client->processor.process_receive (*winner_l.first);
                last_winner = std::move (winner_l.first);
            }
        }
        if (!confirmed)
        {
            if (rep_votes.size () == 1)
            {
                if (winner_l.second > uncontested_threshold ())
                {
                    confirmed = true;
                    client->processor.process_confirmed (*last_winner);
                }
            }
            else
            {
                if (winner_l.second > contested_threshold ())
                {
                    confirmed = true;
                    client->processor.process_confirmed (*last_winner);
                }
            }
        }
    }
}

std::pair <std::unique_ptr <rai::block>, rai::uint256_t> rai::votes::winner ()
{
    std::unordered_map <rai::block_hash, std::pair <std::unique_ptr <block>, rai::uint256_t>> totals;
    for (auto & i: rep_votes)
    {
        auto hash (i.second.second->hash ());
        auto existing (totals.find (hash));
        if (existing == totals.end ())
        {
            totals.insert (std::make_pair (hash, std::make_pair (i.second.second->clone (), 0)));
            existing = totals.find (hash);
        }
        auto weight (client->ledger.weight (i.first));
        existing->second.second += weight;
    }
    std::pair <std::unique_ptr <rai::block>, rai::uint256_t> winner_l;
    for (auto & i: totals)
    {
        if (i.second.second >= winner_l.second)
        {
            winner_l.first = i.second.first->clone ();
            winner_l.second = i.second.second;
        }
    }
    return winner_l;
}

rai::votes::votes (std::shared_ptr <rai::client> client_a, rai::block const & block_a) :
client (client_a),
root (client_a->store.root (block_a)),
last_winner (block_a.clone ()),
sequence (0),
confirmed (false),
last_vote (std::chrono::system_clock::now ())
{
    assert (client_a->store.block_exists (block_a.hash ()));
    rai::keypair anonymous;
    rai::vote vote_l;
    vote_l.address = anonymous.pub;
    vote_l.sequence = 0;
    vote_l.block = block_a.clone ();
    rai::sign_message (anonymous.prv, anonymous.pub, vote_l.hash (), vote_l.signature);
    vote (vote_l);
}

void rai::votes::start ()
{
	client->representative_vote (*this, *last_winner);
    if (client->is_representative ())
    {
        announce_vote ();
    }
    auto client_l (client);
    auto root_l (root);
    auto destructable (std::make_shared <rai::destructable> ([client_l, root_l] () {client_l->conflicts.stop (root_l);}));
    timeout_action (destructable);
}

rai::destructable::destructable (std::function <void ()> operation_a) :
operation (operation_a)
{
}

rai::destructable::~destructable ()
{
    operation ();
}

void rai::votes::timeout_action (std::shared_ptr <rai::destructable> destructable_a)
{
    auto now (std::chrono::system_clock::now ());
    if (now - last_vote < std::chrono::seconds (15))
    {
        auto this_l (shared_from_this ());
        client->service.add (now + std::chrono::seconds (15), [this_l, destructable_a] () {this_l->timeout_action (destructable_a);});
    }
}

rai::uint256_t rai::votes::uncontested_threshold ()
{
    return client->ledger.supply () / 2;
}

rai::uint256_t rai::votes::contested_threshold ()
{
    return (client->ledger.supply () / 16) * 15;
}

rai::uint256_t rai::votes::flip_threshold ()
{
    return client->ledger.supply () / 2;
}

void rai::conflicts::start (rai::block const & block_a, bool request_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto root (client.store.root (block_a));
    auto existing (roots.find (root));
    if (existing == roots.end ())
    {
        auto votes (std::make_shared <rai::votes> (client.shared (), block_a));
		client.service.add (std::chrono::system_clock::now (), [votes] () {votes->start ();});
        roots.insert (std::make_pair (root, votes));
        if (request_a)
        {
            votes->start_request (block_a);
        }
    }
}

void rai::conflicts::update (rai::vote const & vote_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (roots.find (client.store.root (*vote_a.block)));
    if (existing != roots.end ())
    {
        existing->second->vote (vote_a);
    }
}

void rai::conflicts::stop (rai::block_hash const & root_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    assert (roots.find (root_a) != roots.end ());
    roots.erase (root_a);
}

rai::conflicts::conflicts (rai::client & client_a) :
client (client_a)
{
}

namespace
{
class network_message_visitor : public rai::message_visitor
{
public:
	network_message_visitor (rai::client & client_a, rai::endpoint const & sender_a, bool known_peer_a) :
	client (client_a),
	sender (sender_a),
	known_peer (known_peer_a)
	{
	}
	void keepalive_req (rai::keepalive_req const & message_a) override
	{
		if (network_keepalive_logging ())
		{
			client.log.add (boost::str (boost::format ("Received keepalive req from %1%") % sender));
		}
		rai::keepalive_ack ack_message;
		client.peers.random_fill (ack_message.peers);
		ack_message.checksum = client.ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ());
		std::shared_ptr <std::vector <uint8_t>> ack_bytes (new std::vector <uint8_t>);
		{
			rai::vectorstream stream (*ack_bytes);
			ack_message.serialize (stream);
		}
		rai::keepalive_req req_message;
		req_message.peers = ack_message.peers;
		std::shared_ptr <std::vector <uint8_t>> req_bytes (new std::vector <uint8_t>);
		{
			rai::vectorstream stream (*req_bytes);
			req_message.serialize (stream);
		}
		client.network.merge_peers (req_bytes, message_a.peers);
		if (network_keepalive_logging ())
		{
			client.log.add (boost::str (boost::format ("Sending keepalive ack to %1%") % sender));
		}
		auto & client_l (client);
		client.network.send_buffer (ack_bytes->data (), ack_bytes->size (), sender, [ack_bytes, &client_l] (boost::system::error_code const & error, size_t size_a)
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
	void keepalive_ack (rai::keepalive_ack const & message_a) override
	{
		if (network_keepalive_logging ())
		{
			client.log.add (boost::str (boost::format ("Received keepalive ack from %1%") % sender));
		}
		rai::keepalive_req req_message;
		client.peers.random_fill (req_message.peers);
		std::shared_ptr <std::vector <uint8_t>> req_bytes (new std::vector <uint8_t>);
		{
			rai::vectorstream stream (*req_bytes);
			req_message.serialize (stream);
		}
		client.network.merge_peers (req_bytes, message_a.peers);
		client.peers.incoming_from_peer (sender);
		if (!known_peer && message_a.checksum != client.ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()))
		{
			client.processor.bootstrap (rai::tcp_endpoint (sender.address (), sender.port ()),
										[] ()
										{
										});
		}
	}
	void publish_req (rai::publish_req const & message_a) override
	{
		if (network_message_logging ())
		{
			client.log.add (boost::str (boost::format ("Received publish req rom %1%") % sender));
		}
		client.processor.process_receive_republish (message_a.block->clone (), sender);
	}
	void confirm_req (rai::confirm_req const & message_a) override
	{
		if (network_message_logging ())
		{
			client.log.add (boost::str (boost::format ("Received confirm req from %1%") % sender));
		}
		auto result (client.ledger.process (*message_a.block));
		switch (result)
		{
			case rai::process_result::old:
			case rai::process_result::progress:
			{
				client.processor.process_confirmation (*message_a.block, sender);
				break;
			}
			default:
			{
				assert (false);
			}
		}
	}
	void confirm_ack (rai::confirm_ack const & message_a) override
	{
		if (network_message_logging ())
		{
			client.log.add (boost::str (boost::format ("Received Confirm from %1%") % sender));
		}
        client.processor.process_receive_republish (message_a.vote.block->clone (), sender);
        client.conflicts.update (message_a.vote);
	}
	void confirm_unk (rai::confirm_unk const &) override
	{
		assert (false);
	}
	void bulk_req (rai::bulk_req const &) override
	{
		assert (false);
	}
	void frontier_req (rai::frontier_req const &) override
	{
		assert (false);
	}
	rai::client & client;
	rai::endpoint sender;
	bool known_peer;
};
}

void rai::processor::process_message (rai::message & message_a, rai::endpoint const & endpoint_a, bool known_peer_a)
{
	network_message_visitor visitor (client, endpoint_a, known_peer_a);
	message_a.visit (visitor);
}

namespace
{
class confirmed_visitor : public rai::block_visitor
{
public:
    confirmed_visitor (rai::client & client_a) :
    client (client_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        rai::private_key prv;
        if (!client.wallet.fetch (block_a.hashables.destination, prv))
        {
            auto error (client.transactions.receive (block_a, prv, client.representative));
            prv.bytes.fill (0);
            assert (!error);
        }
        else
        {
            // Wallet doesn't contain key for this destination or couldn't decrypt
        }
    }
    void receive_block (rai::receive_block const &) override
    {
    }
    void open_block (rai::open_block const &) override
    {
    }
    void change_block (rai::change_block const &) override
    {
    }
    rai::client & client;
};
}

void rai::processor::process_confirmed (rai::block const & confirmed_a)
{
    confirmed_visitor visitor (client);
    confirmed_a.visit (visitor);
}

std::unique_ptr <rai::block> rai::ledger::successor (rai::block_hash const & block_a)
{
	assert (store.block_exists (block_a));
	auto account_l (account (block_a));
	auto latest_l (latest (account_l));
	assert (latest_l != block_a);
	std::unique_ptr <rai::block> result (store.block_get (latest_l));
	assert (result != nullptr);
	while (result->previous () != block_a)
	{
		auto previous_hash (result->previous ());
		result = store.block_get (previous_hash);
		assert (result != nullptr);
	} 
	return result;
}

bool rai::client::is_representative ()
{
    return wallet.find (representative) != wallet.end ();
}

void rai::client::representative_vote (rai::votes & votes_a, rai::block const & block_a)
{
	if (is_representative ())
	{
        rai::private_key prv;
        rai::vote vote_l;
        vote_l.address = representative;
        vote_l.sequence = 0;
        vote_l.block = block_a.clone ();
		wallet.fetch (representative, prv);
        rai::sign_message (prv, representative, vote_l.hash (), vote_l.signature);
        prv.clear ();
        votes_a.vote (vote_l);
	}
}

rai::uint256_union rai::wallet::check ()
{
    rai::uint256_union one (1);
    std::string check;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (one.chars.data (), one.chars.size ()), &check));
    assert (status.ok ());
    rai::uint256_union result;
    assert (check.size () == sizeof (rai::uint256_union));
    std::copy (check.begin (), check.end (), result.chars.begin ());
    return result;
}

rai::uint256_union rai::wallet::wallet_key ()
{
    rai::uint256_union zero;
    zero.clear ();
    std::string encrypted_wallet_key;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (zero.chars.data (), zero.chars.size ()), &encrypted_wallet_key));
    assert (status.ok ());
    assert (encrypted_wallet_key.size () == sizeof (rai::uint256_union));
    rai::uint256_union encrypted_key;
    std::copy (encrypted_wallet_key.begin (), encrypted_wallet_key.end (), encrypted_key.chars.begin ());
    return encrypted_key.prv (password, password.owords [0]);
}

bool rai::wallet::valid_password ()
{
    rai::uint256_union zero;
    zero.clear ();
    auto wallet_key_l (wallet_key ());
    rai::uint256_union check_l (zero, wallet_key_l, wallet_key_l.owords [0]);
    wallet_key_l.clear ();
    return check () == check_l;
}

bool rai::transactions::rekey (rai::uint256_union const & password_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    return wallet.rekey (password_a);
}

bool rai::wallet::rekey (rai::uint256_union const & password_a)
{
    bool result (false);
	if (valid_password ())
    {
        auto wallet_key_l (wallet_key ());
        password = password_a;
        rai::uint256_union zero;
        zero.clear ();
        rai::uint256_union encrypted (wallet_key_l, password_a, password_a.owords [0]);
        auto status1 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (zero.chars.data (), zero.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
        assert (status1.ok ());
    }
    else
    {
        result = true;
    }
    return result;
}

rai::uint256_union rai::wallet::hash_password (std::string const & password_a)
{
    CryptoPP::SHA3 hash (32);
    hash.Update (reinterpret_cast <uint8_t const *> (password_a.data ()), password_a.size ());
    rai::uint256_union result;
    hash.Final (result.bytes.data ());
    return result;
}

bool rai::confirm_req::operator == (rai::confirm_req const & other_a) const
{
    return work == other_a.work && *block == *other_a.block;
}

bool rai::publish_req::operator == (rai::publish_req const & other_a) const
{
    return work == other_a.work && *block == *other_a.block;
}

uint64_t rai::client::scale_down (rai::uint256_t const & amount_a)
{
    return (amount_a / scale).convert_to <uint64_t> ();
}

rai::uint256_t rai::client::scale_up (uint64_t amount_a)
{
    return scale * amount_a;
}

void rai::processor::find_network (std::vector <std::pair <std::string, std::string>> const & well_known_peers_a)
{
    auto resolver (std::make_shared <boost::asio::ip::udp::resolver> (client.network.service));
    auto client_l (client.shared ());
    for (auto & i: well_known_peers_a)
    {
        resolver->async_resolve (boost::asio::ip::udp::resolver::query (i.first, i.second),
                                 [client_l, resolver]
                                 (boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator values)
        {
            if (!ec)
            {
                for (; values != boost::asio::ip::udp::resolver::iterator (); ++values)
                {
                    client_l->network.send_keepalive (*values);
                }
            }
            else
            {
                client_l->log.add (boost::str (boost::format ("Unable to resolve raiblocks.net")));
            }
        });
    }
}

namespace
{
uint32_t R (uint32_t value_a, unsigned amount_a)
{
    return (value_a << amount_a) | (value_a >> (32 - amount_a));
}
}

rai::uint512_union rai::uint512_union::salsa20_8 ()
{
    rai::uint512_union result;
    auto & x (result.dwords);
    auto & in (dwords);
    int i;
    for (i = 0;i < 16;++i) x[i] = in[i];
    for (i = 8;i > 0;i -= 2) {
        x[ 4] ^= R(x[ 0]+x[12], 7);  x[ 8] ^= R(x[ 4]+x[ 0], 9);
        x[12] ^= R(x[ 8]+x[ 4],13);  x[ 0] ^= R(x[12]+x[ 8],18);
        x[ 9] ^= R(x[ 5]+x[ 1], 7);  x[13] ^= R(x[ 9]+x[ 5], 9);
        x[ 1] ^= R(x[13]+x[ 9],13);  x[ 5] ^= R(x[ 1]+x[13],18);
        x[14] ^= R(x[10]+x[ 6], 7);  x[ 2] ^= R(x[14]+x[10], 9);
        x[ 6] ^= R(x[ 2]+x[14],13);  x[10] ^= R(x[ 6]+x[ 2],18);
        x[ 3] ^= R(x[15]+x[11], 7);  x[ 7] ^= R(x[ 3]+x[15], 9);
        x[11] ^= R(x[ 7]+x[ 3],13);  x[15] ^= R(x[11]+x[ 7],18);
        x[ 1] ^= R(x[ 0]+x[ 3], 7);  x[ 2] ^= R(x[ 1]+x[ 0], 9);
        x[ 3] ^= R(x[ 2]+x[ 1],13);  x[ 0] ^= R(x[ 3]+x[ 2],18);
        x[ 6] ^= R(x[ 5]+x[ 4], 7);  x[ 7] ^= R(x[ 6]+x[ 5], 9);
        x[ 4] ^= R(x[ 7]+x[ 6],13);  x[ 5] ^= R(x[ 4]+x[ 7],18);
        x[11] ^= R(x[10]+x[ 9], 7);  x[ 8] ^= R(x[11]+x[10], 9);
        x[ 9] ^= R(x[ 8]+x[11],13);  x[10] ^= R(x[ 9]+x[ 8],18);
        x[12] ^= R(x[15]+x[14], 7);  x[13] ^= R(x[12]+x[15], 9);
        x[14] ^= R(x[13]+x[12],13);  x[15] ^= R(x[14]+x[13],18);
    }
    for (i = 0;i < 16;++i) x[i] += in[i];
    return result;
}

bool rai::uint512_union::operator != (rai::uint512_union const & other_a) const
{
    return ! (*this == other_a);
}

rai::work::work () :
entry_requirement (1024),
iteration_requirement (1024)
{
	threshold_requirement.decode_hex ("f000000000000000000000000000000000000000000000000000000000000000");
    entries.resize (entry_requirement);
}

rai::uint512_union & rai::uint512_union::operator ^= (rai::uint512_union const & other_a)
{
    uint256s [0] ^= other_a.uint256s [0];
    uint256s [1] ^= other_a.uint256s [1];
    return *this;
}

rai::uint256_union rai::work::generate (rai::uint256_union const & seed, rai::uint256_union const & nonce)
{
    auto mask (entries.size () - 1);
    for (auto & i: entries)
    {
        i.clear ();
    }
    rai::uint512_union value;
    value.uint256s [0] = seed;
    value.uint256s [1] = nonce;
    for (uint32_t i (0); i < iteration_requirement; ++i)
    {
        auto index (value.qwords [0] & mask);
        auto & entry (entries [index]);
        value ^= entry;
        value = value.salsa20_8 ();
        entry = value;
    }
    CryptoPP::SHA3 hash (32);
    for (auto & i: entries)
    {
        hash.Update (i.bytes.data (), i.bytes.size ());
    }
    rai::uint256_union result;
    hash.Final (result.bytes.data ());
    return result;
}

rai::uint256_union rai::work::create (rai::uint256_union const & seed)
{
    rai::uint256_union result;
    rai::uint256_union value;
    do
    {
        ed25519_randombytes_unsafe (result.bytes.data (), sizeof (result));
        value = generate (seed, result);
    } while (value < threshold_requirement);
    return result;
}

bool rai::work::validate (rai::uint256_union const & seed, rai::uint256_union const & nonce)
{
    auto value (generate (seed, nonce));
    return value < threshold_requirement;
}