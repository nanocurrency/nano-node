#include <rai/secure.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

namespace
{
    std::string rai_test_private_key = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
    std::string rai_test_public_key = "B241CC17B3684D22F304C7AF063D1B833124F7F1A4DAD07E6DA60D7D8F334911"; // U63Kt3B7yp2iQB4GsVWriGv34kk2qwhT7acKvn8yWZGdNVesJ8
    std::string rai_beta_public_key = "1A99D99731BC08252C8762FBB2CBB7BA3520039109FCE869C75406E722C636E3"; // TV67A7XWyLF7njTjTZC9zQ4iLftsVDRQUDmW7LieZzqZm2gMnz
    std::string rai_live_public_key = "0";
}

rai::keypair const rai::test_genesis_key (rai_test_private_key);
rai::account const rai::rai_test_account (rai_test_public_key);
rai::account const rai::rai_beta_account (rai_beta_public_key);
rai::account const rai::rai_live_account (rai_live_public_key);

rai::account const rai::genesis_account = rai_network == rai_networks::rai_test_network ? rai_test_account : rai_network == rai_networks::rai_beta_network ? rai_beta_account : rai_live_account;

CryptoPP::AutoSeededRandomPool rai::random_pool;

std::string rai::to_string_hex (uint64_t value_a)
{
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (16) << std::setfill ('0');
    stream << value_a;
    return stream.str ();
}

bool rai::from_string_hex (std::string const & value_a, uint64_t & target_a)
{
    auto result (value_a.size () == 8);
    if (!result)
    {
        std::stringstream stream (value_a);
        stream << std::hex << std::noshowbase;
        uint64_t number_l;
        try
        {
            stream >> number_l;
            target_a = number_l;
        }
        catch (std::runtime_error &)
        {
            result = true;
        }
    }
    return result;
}

// Divide the raw 128bit number to one that fits in 64bits
uint64_t rai::scale_down (rai::uint128_t const & amount_a)
{
    return (amount_a / rai::scale_64bit_base10).convert_to <uint64_t> ();
}

// Return the full 128bit amount number from a reduced 64bit one
rai::uint128_t rai::scale_up (uint64_t amount_a)
{
    return rai::scale_64bit_base10 * amount_a;
}

size_t rai::unique_ptr_block_hash::operator () (std::unique_ptr <rai::block> const & block_a) const
{
	auto hash (block_a->hash ());
	auto result (static_cast <size_t> (hash.qwords [0]));
	return result;
}

bool rai::unique_ptr_block_hash::operator () (std::unique_ptr <rai::block> const & lhs, std::unique_ptr <rai::block> const & rhs) const
{
	return *lhs == *rhs;
}

// Validate a vote and apply it to the current election or start a new election if it doesn't exist
bool rai::votes::vote (rai::vote const & vote_a)
{
	auto result (false);
	if (!rai::validate_message (vote_a.account, vote_a.hash (), vote_a.signature))
	{
		auto existing (rep_votes.find (vote_a.account));
		if (existing == rep_votes.end ())
		{
			result = true;
			rep_votes.insert (std::make_pair (vote_a.account, std::make_pair (vote_a.sequence, vote_a.block->clone ())));
		}
		else
		{
			if (existing->second.first < vote_a.sequence)
			{
				result = !(*existing->second.second == *vote_a.block);
				if (result)
				{
					existing->second.second = vote_a.block->clone ();
				}
			}
		}
	}
	return result;
}

std::map <rai::uint128_t, std::unique_ptr <rai::block>, std::greater <rai::uint128_t>> rai::votes::tally ()
{
	std::unordered_map <std::unique_ptr <block>, rai::uint128_t, rai::unique_ptr_block_hash, rai::unique_ptr_block_hash> totals;
	for (auto & i: rep_votes)
	{
		auto existing (totals.find (i.second.second));
		if (existing == totals.end ())
		{
			totals.insert (std::make_pair (i.second.second->clone (), 0));
			existing = totals.find (i.second.second);
			assert (existing != totals.end ());
		}
		auto weight (ledger.weight (i.first));
		existing->second += weight;
	}
	std::map <rai::uint128_t, std::unique_ptr <rai::block>, std::greater <rai::uint128_t>> result;
	for (auto & i: totals)
	{
		result [i.second] = i.first->clone ();
	}
	return result;
}

// Sum the weights for each vote and return the winning block with its vote tally
std::pair <rai::uint128_t, std::unique_ptr <rai::block>> rai::votes::winner ()
{
	auto tally_l (tally ());
	auto existing (tally_l.begin ());
	return std::make_pair (existing->first, existing->second->clone ());
}

rai::votes::votes (rai::ledger & ledger_a, rai::block const & block_a) :
ledger (ledger_a),
root (ledger.store.root (block_a)),
// Sequence 0 is the first response by a representative before a fork was observed
sequence (1)
{
}

// Create a new random keypair
rai::keypair::keypair ()
{
    random_pool.GenerateBlock (prv.bytes.data (), prv.bytes.size ());
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
rai::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

namespace
{
class xorshift1024star
{
public:
    xorshift1024star ():
    p (0)
    {
    }
    std::array <uint64_t, 16> s;
    unsigned p;
    uint64_t next ()
    {
        auto p_l (p);
        auto pn ((p_l + 1) & 15);
        p = pn;
        uint64_t s0 = s[ p_l ];
        uint64_t s1 = s[ pn ];
        s1 ^= s1 << 31; // a
        s1 ^= s1 >> 11; // b
        s0 ^= s0 >> 30; // c
        return ( s[ pn ] = s0 ^ s1 ) * 1181783497276652981LL;
    }
};
}

rai::work::work (size_t entries_a) :
threshold_requirement (0xfff0000000000000),
entries (entries_a),
data (new uint64_t [entries_a])
{
    assert ((entries_a & 0xf) == 0);
}

namespace {
size_t constexpr stepping (16);
}
rai::uint256_union rai::work::derive (CryptoPP::SHA3 & hash_a, rai::uint256_union const & input_a)
{
    auto entries_l (entries);
    auto mask (entries_l - 1);
    xorshift1024star rng;
    rng.s [0] = input_a.qwords [0];
    rng.s [1] = input_a.qwords [1];
    rng.s [2] = input_a.qwords [2];
    rng.s [3] = input_a.qwords [3];
    for (auto i (4), n (16); i != n; ++i)
    {
        rng.s [i] = 0;
    }
    // Random-fill buffer for an initialized starting point
    for (auto i (data.get ()), n (data.get () + entries_l); i != n; ++i)
    {
        auto next (rng.next ());
        *i = next;
    }
    auto previous (rng.next ());
    // Random-write buffer to break n+1 = f(n) relation
    for (size_t i (0), n (entries); i != n; ++i)
    {
        auto index (previous & mask);
        auto value (rng.next ());
        data [index] = value;
    }
    // Random-read buffer to prevent partial memorization
    union
    {
        std::array <uint64_t, stepping> qwords;
        std::array <uint8_t, stepping * sizeof (uint64_t)> bytes;
    } value;
    for (size_t i (0), n (entries); i != n; i += stepping)
    {
        for (size_t j (0), m (stepping); j != m; ++j)
        {
            auto index (rng.next () % (entries_l - (i + j)));
            value.qwords [j] = data [index];
            data [index] = data [entries_l - (i + j) - 1];
        }
        hash_a.Update (reinterpret_cast <uint8_t *> (value.bytes.data ()), stepping * sizeof (uint64_t));
    }
    rai::uint256_union result;
    hash_a.Final (result.bytes.data ());
    return result;
}

rai::uint256_union rai::work::kdf (std::string const & password_a, rai::uint256_union const & salt_a)
{
    rai::uint256_union input;
    CryptoPP::SHA3 hash (32);
    hash.Update (reinterpret_cast <uint8_t const *> (password_a.data ()), password_a.size ());
    hash.Final (input.bytes.data ());
    input ^= salt_a;
    hash.Restart ();
    auto result (derive (hash, input));
    return result;
}

uint64_t rai::work::generate (CryptoPP::SHA3 & hash, rai::uint256_union const & seed, uint64_t nonce)
{
    auto result (derive (hash, seed ^ nonce));
    return result.qwords [0] ^ result.qwords [1] ^ result.qwords [2] ^ result.qwords [3];
}

uint64_t rai::work::create (rai::uint256_union const & seed)
{
    xorshift1024star rng;
    rng.s.fill (0x0123456789abcdef);// No seed here, we're not securing anything, s just can't be 0 per the spec
    uint64_t result;
    rai::uint256_union value;
    CryptoPP::SHA3 hash (32);
    do
    {
        result = rng.next ();
        value = generate (hash, seed, result);
        hash.Restart ();
    } while (value < threshold_requirement);
    return result;
}

bool rai::work::validate (rai::uint256_union const & seed, uint64_t nonce)
{
    CryptoPP::SHA3 hash (32);
    auto value (generate (hash, seed, nonce));
    return value < threshold_requirement;
}

rai::ledger::ledger (bool & init_a, leveldb::Status const & store_init_a, rai::block_store & store_a) :
store (store_a),
send_observer ([] (rai::send_block const &, rai::account const &, rai::amount const &) {}),
receive_observer ([] (rai::receive_block const &, rai::account const &, rai::amount const &) {}),
open_observer ([] (rai::open_block const &, rai::account const &, rai::amount const &, rai::account const &) {}),
change_observer ([] (rai::change_block const &, rai::account const &, rai::account const &) {})
{
    if (store_init_a.ok ())
    {
        init_a = false;
    }
    else
    {
        init_a = true;
    }
}

rai::uint128_union::uint128_union (uint64_t value_a)
{
    qwords [0] = value_a;
    qwords [1] = 0;
}

rai::uint128_union::uint128_union (rai::uint128_t const & value_a)
{
    boost::multiprecision::uint256_t number_l (value_a);
    qwords [0] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [1] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
}

bool rai::uint128_union::operator == (rai::uint128_union const & other_a) const
{
    return qwords [0] == other_a.qwords [0] && qwords [1] == other_a.qwords [1];
}

rai::uint128_t rai::uint128_union::number () const
{
    boost::multiprecision::uint128_t result (qwords [1]);
    result <<= 64;
    result |= qwords [0];
    return result;
}

void rai::uint128_union::encode_hex (std::string & text) const
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (32) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool rai::uint128_union::decode_hex (std::string const & text)
{
    auto result (text.size () > 32);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::hex << std::noshowbase;
        rai::uint128_t number_l;
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

void rai::uint128_union::encode_dec (std::string & text) const
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::dec << std::noshowbase;
    stream << number ();
    text = stream.str ();
}

bool rai::uint128_union::decode_dec (std::string const & text)
{
    auto result (text.size () > 39);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::dec << std::noshowbase;
        rai::uint128_t number_l;
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

void rai::uint128_union::clear ()
{
    qwords.fill (0);
}

bool rai::uint128_union::is_zero () const
{
    return qwords [0] == 0 && qwords [1] == 0;
}

bool rai::uint256_union::operator == (rai::uint256_union const & other_a) const
{
	return bytes == other_a.bytes;
}

bool rai::uint512_union::operator == (rai::uint512_union const & other_a) const
{
	return bytes == other_a.bytes;
}

rai::uint256_union::uint256_union (rai::private_key const & prv, rai::secret_key const & key, uint128_union const & iv)
{
	rai::uint256_union exponent (prv);
	CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
    CryptoPP::CTR_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
	enc.ProcessData (bytes.data (), exponent.bytes.data (), sizeof (exponent.bytes));
}

rai::private_key rai::uint256_union::prv (rai::secret_key const & key_a, uint128_union const & iv) const
{
	CryptoPP::AES::Encryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
	rai::private_key result;
	dec.ProcessData (result.bytes.data (), bytes.data (), sizeof (bytes));
	return result;
}

void rai::send_block::visit (rai::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void rai::send_block::hash (CryptoPP::SHA3 & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t rai::send_block::block_work () const
{
    return work;
}

void rai::send_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
	hash_a.Update (destination.bytes.data (), sizeof (destination.bytes));
	hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
	hash_a.Update (balance.bytes.data (), sizeof (balance.bytes));
}

void rai::send_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
    write (stream_a, work);
}

void rai::send_block::serialize_json (std::string & string_a) const
{
    boost::property_tree::ptree tree;
    tree.put ("type", "send");
    std::string destination;
    hashables.destination.encode_base58check (destination);
    tree.put ("destination", destination);
    std::string previous;
    hashables.previous.encode_hex (previous);
    tree.put ("previous", previous);
    std::string balance;
    hashables.balance.encode_hex (balance);
    tree.put ("balance", balance);
    std::string signature_l;
    signature.encode_hex (signature_l);
    tree.put ("work", rai::to_string_hex (work));
    tree.put ("signature", signature_l);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
}

bool rai::send_block::deserialize (rai::stream & stream_a)
{
	auto result (false);
    result = read (stream_a, hashables.destination.bytes);
	if (!result)
	{
		result = read (stream_a, hashables.previous.bytes);
		if (!result)
		{
			result = read (stream_a, hashables.balance.bytes);
			if (!result)
			{
                result = read (stream_a, signature.bytes);
                if (!result)
                {
                    result = read (stream_a, work);
                }
			}
		}
	}
	return result;
}

bool rai::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
    auto result (false);
    try
    {
        assert (tree_a.get <std::string> ("type") == "send");
        auto destination_l (tree_a.get <std::string> ("destination"));
        auto previous_l (tree_a.get <std::string> ("previous"));
        auto balance_l (tree_a.get <std::string> ("balance"));
        auto work_l (tree_a.get <std::string> ("work"));
        auto signature_l (tree_a.get <std::string> ("signature"));
        result = hashables.destination.decode_base58check (destination_l);
        if (!result)
        {
            result = hashables.previous.decode_hex (previous_l);
            if (!result)
            {
                result = hashables.balance.decode_hex (balance_l);
                if (!result)
                {
                    result = rai::from_string_hex (work_l, work);
                    if (!result)
                    {
                        result = signature.decode_hex (signature_l);
                    }
                }
            }
        }
    }
    catch (std::runtime_error const &)
    {
        result = true;
    }
    return result;
}

void rai::receive_block::visit (rai::block_visitor & visitor_a) const
{
    visitor_a.receive_block (*this);
}

bool rai::receive_block::operator == (rai::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

bool rai::receive_block::deserialize (rai::stream & stream_a)
{
	auto result (false);
    result = read (stream_a, hashables.previous.bytes);
	if (!result)
	{
        result = read (stream_a, hashables.source.bytes);
		if (!result)
		{
            result = read (stream_a, signature.bytes);
            if (!result)
            {
                result = read (stream_a, work);
            }
		}
	}
	return result;
}

bool rai::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
    auto result (false);
    try
    {
        assert (tree_a.get <std::string> ("type") == "receive");
        auto previous_l (tree_a.get <std::string> ("previous"));
        auto source_l (tree_a.get <std::string> ("source"));
        auto work_l (tree_a.get <std::string> ("work"));
        auto signature_l (tree_a.get <std::string> ("signature"));
        result = hashables.previous.decode_hex (previous_l);
        if (!result)
        {
            result = hashables.source.decode_hex (source_l);
            if (!result)
            {
                result = rai::from_string_hex (work_l, work);
                if (!result)
                {
                    result = signature.decode_hex (signature_l);
                }
            }
        }
    }
    catch (std::runtime_error const &)
    {
        result = true;
    }
    return result;
}

void rai::receive_block::serialize (rai::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
    write (stream_a, work);
}

void rai::receive_block::serialize_json (std::string & string_a) const
{
    boost::property_tree::ptree tree;
    tree.put ("type", "receive");
    std::string previous;
    hashables.previous.encode_hex (previous);
    tree.put ("previous", previous);
    std::string source;
    hashables.source.encode_hex (source);
    tree.put ("source", source);
    std::string signature_l;
    signature.encode_hex (signature_l);
    tree.put ("work", rai::to_string_hex (work));
    tree.put ("signature", signature_l);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
}

void rai::receive_block::hash (CryptoPP::SHA3 & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t rai::receive_block::block_work () const
{
    return work;
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

rai::block_hash rai::receive_block::previous () const
{
    return hashables.previous;
}

rai::block_hash rai::receive_block::source () const
{
    return hashables.source;
}

std::unique_ptr <rai::block> rai::receive_block::clone () const
{
    return std::unique_ptr <rai::block> (new rai::receive_block (*this));
}

rai::block_type rai::receive_block::type () const
{
    return rai::block_type::receive;
}

void rai::receive_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
	hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
	hash_a.Update (source.bytes.data (), sizeof (source.bytes));
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

rai::uint256_union::uint256_union (std::string const & hex_a)
{
    decode_hex (hex_a);
}

void rai::uint256_union::clear ()
{
    qwords.fill (0);
}

rai::uint256_t rai::uint256_union::number () const
{
    boost::multiprecision::uint256_t result (qwords [3]);
    result <<= 64;
    result |= qwords [2];
    result <<= 64;
    result |= qwords [1];
    result <<= 64;
    result |= qwords [0];
    return result;
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
    auto result (false);
    if (!text.empty ())
    {
        if (text.size () <= 64)
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
        else
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

rai::uint256_union::uint256_union (uint64_t value)
{
    qwords [0] = value;
    qwords [1] = 0;
    qwords [2] = 0;
    qwords [3] = 0;
}

bool rai::uint256_union::operator != (rai::uint256_union const & other_a) const
{
    return ! (*this == other_a);
}

// Base58check is an encoding using [0-9][a-z][A-Z] excluding characters that can be confused
// Base56check also has a 32bit error correction code.
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

rai::uint256_union::uint256_union (rai::uint256_t const & number_a)
{
    boost::multiprecision::uint256_t number_l (number_a);
    qwords [0] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [1] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [2] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [3] = number_l.convert_to <uint64_t> ();
}

rai::uint256_union & rai::uint256_union::operator = (leveldb::Slice const & slice_a)
{
    assert (slice_a.size () == 32);
    rai::bufferstream stream (reinterpret_cast <uint8_t const *> (slice_a.data ()), slice_a.size ());
    auto error (rai::read (stream, *this));
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
}

void rai::uint512_union::clear ()
{
    bytes.fill (0);
}

boost::multiprecision::uint512_t rai::uint512_union::number () const
{
    boost::multiprecision::uint512_t result (qwords [7]);
    result <<= 64;
    result |= qwords [6];
    result <<= 64;
    result |= qwords [5];
    result <<= 64;
    result |= qwords [4];
    result <<= 64;
    result |= qwords [3];
    result <<= 64;
    result |= qwords [2];
    result <<= 64;
    result |= qwords [1];
    result <<= 64;
    result |= qwords [0];
    return result;
}

void rai::uint512_union::encode_hex (std::string & text) const
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

bool rai::uint512_union::operator != (rai::uint512_union const & other_a) const
{
    return ! (*this == other_a);
}

rai::uint512_union & rai::uint512_union::operator ^= (rai::uint512_union const & other_a)
{
    uint256s [0] ^= other_a.uint256s [0];
    uint256s [1] ^= other_a.uint256s [1];
    return *this;
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

rai::uint256_union rai::block::hash () const
{
    CryptoPP::SHA3 hash_l (32);
    hash (hash_l);
    rai::uint256_union result;
    hash_l.Final (result.bytes.data ());
    return result;
}

// Serialize a block prefixed with an 8-bit typecode
void rai::serialize_block (rai::stream & stream_a, rai::block const & block_a)
{
    write (stream_a, block_a.type ());
    block_a.serialize (stream_a);
}

std::unique_ptr <rai::block> rai::deserialize_block (rai::stream & stream_a, rai::block_type type_a)
{
    std::unique_ptr <rai::block> result;
    switch (type_a)
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
            bool error;
            std::unique_ptr <rai::change_block> obj (new rai::change_block (error, stream_a));
            if (!error)
            {
                result = std::move (obj);
            }
            break;
        }
        default:
            break;
    }
    return result;
}

std::unique_ptr <rai::block> rai::deserialize_block_json (boost::property_tree::ptree const & tree_a)
{
    std::unique_ptr <rai::block> result;
    try
    {
        auto type (tree_a.get <std::string> ("type"));
        if (type == "receive")
        {
            std::unique_ptr <rai::receive_block> obj (new rai::receive_block);
            auto error (obj->deserialize_json (tree_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
        else if (type == "send")
        {
            std::unique_ptr <rai::send_block> obj (new rai::send_block);
            auto error (obj->deserialize_json (tree_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
        else if (type == "open")
        {
            std::unique_ptr <rai::open_block> obj (new rai::open_block);
            auto error (obj->deserialize_json (tree_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
        else if (type == "change")
        {
            bool error;
            std::unique_ptr <rai::change_block> obj (new rai::change_block (error, tree_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
    }
    catch (std::runtime_error const &)
    {
    }
    return result;
}

std::unique_ptr <rai::block> rai::deserialize_block (rai::stream & stream_a)
{
    rai::block_type type;
    auto error (read (stream_a, type));
    std::unique_ptr <rai::block> result;
    if (!error)
    {
         result = rai::deserialize_block (stream_a, type);
    }
    return result;
}

rai::send_block::send_block (send_block const & other_a) :
hashables (other_a.hashables),
signature (other_a.signature),
work (other_a.work)
{
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

std::unique_ptr <rai::block> rai::send_block::clone () const
{
    return std::unique_ptr <rai::block> (new rai::send_block (*this));
}

rai::block_type rai::send_block::type () const
{
    return rai::block_type::send;
}

bool rai::send_block::operator == (rai::send_block const & other_a) const
{
    auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
    return result;
}

rai::block_hash rai::send_block::previous () const
{
    return hashables.previous;
}

rai::block_hash rai::send_block::source () const
{
    return 0;
}

void rai::open_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash_a.Update (source.bytes.data (), sizeof (source.bytes));
}

void rai::open_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

uint64_t rai::open_block::block_work () const
{
    return work;
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
    write (stream_a, work);
}

void rai::open_block::serialize_json (std::string & string_a) const
{
    boost::property_tree::ptree tree;
    tree.put ("type", "open");
    std::string representative;
    hashables.representative.encode_hex (representative);
    tree.put ("representative", representative);
    std::string source;
    hashables.source.encode_hex (source);
    tree.put ("source", source);
    std::string signature_l;
    signature.encode_hex (signature_l);
    tree.put ("work", rai::to_string_hex (work));
    tree.put ("signature", signature_l);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
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
            if (!result)
            {
                result = read (stream_a, work);
            }
        }
    }
    return result;
}

bool rai::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
    auto result (false);
    try
    {
        assert (tree_a.get <std::string> ("type") == "open");
        auto representative_l (tree_a.get <std::string> ("representative"));
        auto source_l (tree_a.get <std::string> ("source"));
        auto work_l (tree_a.get <std::string> ("work"));
        auto signature_l (tree_a.get <std::string> ("signature"));
        result = hashables.representative.decode_hex (representative_l);
        if (!result)
        {
            result = hashables.source.decode_hex (source_l);
            if (!result)
            {
                result = rai::from_string_hex (work_l, work);
                if (!result)
                {
                    result = signature.decode_hex (signature_l);
                }
            }
        }
    }
    catch (std::runtime_error const &)
    {
        result = true;
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
    return hashables.representative == other_a.hashables.representative && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature;
}

rai::block_hash rai::open_block::source () const
{
    return hashables.source;
}

rai::change_hashables::change_hashables (rai::account const & representative_a, rai::block_hash const & previous_a) :
representative (representative_a),
previous (previous_a)
{
}

rai::change_hashables::change_hashables (bool & error_a, rai::stream & stream_a)
{
    error_a = rai::read (stream_a, representative);
    if (!error_a)
    {
        error_a = rai::read (stream_a, previous);
    }
}

rai::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
    try
    {
        auto representative_l (tree_a.get <std::string> ("representative"));
        auto previous_l (tree_a.get <std::string> ("previous"));
        error_a = representative.decode_hex (representative_l);
        if (!error_a)
        {
            error_a = previous.decode_hex (previous_l);
        }
    }
    catch (std::runtime_error const &)
    {
        error_a = true;
    }
}

void rai::change_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
}

rai::change_block::change_block (rai::account const & representative_a, rai::block_hash const & previous_a, uint64_t work_a, rai::private_key const & prv_a, rai::public_key const & pub_a) :
hashables (representative_a, previous_a),
work (work_a)
{
    rai::sign_message (prv_a, pub_a, hash (), signature);
}

rai::change_block::change_block (rai::account const & representative_a, rai::block_hash const & previous_a, rai::private_key const & prv_a, rai::public_key const & pub_a) :
hashables (representative_a, previous_a)
{
    rai::sign_message (prv_a, pub_a, hash (), signature);
}

rai::change_block::change_block (bool & error_a, rai::stream & stream_a) :
hashables (error_a, stream_a)
{
    if (!error_a)
    {
        error_a = rai::read (stream_a, signature);
        if (!error_a)
        {
            error_a = rai::read (stream_a, work);
        }
    }
}

rai::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
    if (!error_a)
    {
        try
        {
            auto work_l (tree_a.get <std::string> ("work"));
            auto signature_l (tree_a.get <std::string> ("signature"));
            error_a = rai::from_string_hex (work_l, work);
            if (!error_a)
            {
                error_a = signature.decode_hex (signature_l);
            }
        }
        catch (std::runtime_error const &)
        {
            error_a = true;
        }
    }
}

void rai::change_block::hash (CryptoPP::SHA3 & hash_a) const
{
    hashables.hash (hash_a);
}

uint64_t rai::change_block::block_work () const
{
    return work;
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
    write (stream_a, work);
}

void rai::change_block::serialize_json (std::string & string_a) const
{
    boost::property_tree::ptree tree;
    tree.put ("type", "change");
    std::string representative;
    hashables.representative.encode_hex (representative);
    tree.put ("representative", representative);
    std::string previous;
    hashables.previous.encode_hex (previous);
    tree.put ("previous", previous);
    tree.put ("work", rai::to_string_hex (work));
    std::string signature_l;
    signature.encode_hex (signature_l);
    tree.put ("signature", signature_l);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
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
            if (!result)
            {
                result = read (stream_a, work);
            }
        }
    }
    return result;
}

bool rai::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
    auto result (false);
    try
    {
        assert (tree_a.get <std::string> ("type") == "change");
        auto representative_l (tree_a.get <std::string> ("representative"));
        auto previous_l (tree_a.get <std::string> ("previous"));
        auto work_l (tree_a.get <std::string> ("work"));
        auto signature_l (tree_a.get <std::string> ("signature"));
        result = hashables.representative.decode_hex (representative_l);
        if (!result)
        {
            result = hashables.previous.decode_hex (previous_l);
            if (!result)
            {
                result = rai::from_string_hex (work_l, work);
                if (!result)
                {
                    result = signature.decode_hex (signature_l);
                }
            }
        }
    }
    catch (std::runtime_error const &)
    {
        result = true;
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
    return hashables.representative == other_a.hashables.representative && hashables.previous == other_a.hashables.previous && work == other_a.work && signature == other_a.signature;
}

rai::block_hash rai::change_block::source () const
{
    return 0;
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

bool rai::frontier::operator == (rai::frontier const & other_a) const
{
    return hash == other_a.hash && representative == other_a.representative && balance == other_a.balance && time == other_a.time;
}

bool rai::frontier::operator != (rai::frontier const & other_a) const
{
    return ! (*this == other_a);
}

rai::account_entry * rai::account_entry::operator -> ()
{
    return this;
}

rai::account_entry & rai::account_iterator::operator -> ()
{
    return current;
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

rai::account_iterator::account_iterator (leveldb::DB & db_a, rai::account const & account_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->Seek (leveldb::Slice (account_a.chars.data (), account_a.chars.size ()));
    set_current ();
}

rai::block_entry * rai::block_entry::operator -> ()
{
    return this;
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

rai::block_store_temp_t rai::block_store_temp;

rai::block_store::block_store (leveldb::Status & result, block_store_temp_t const &) :
block_store (result, boost::filesystem::unique_path ())
{
}

rai::block_store::block_store (leveldb::Status & init_a, boost::filesystem::path const & path_a)
{
    leveldb::DB * db;
    boost::system::error_code code;
    boost::filesystem::create_directories (path_a, code);
    if (!code)
    {
        leveldb::Options options;
        options.create_if_missing = true;
        auto status1 (leveldb::DB::Open (options, (path_a / "accounts.ldb").string (), &db));
        if (status1.ok ())
        {
            accounts.reset (db);
            auto status2 (leveldb::DB::Open (options, (path_a / "blocks.ldb").string (), &db));
            if (status2.ok ())
            {
                blocks.reset (db);
                auto status3 (leveldb::DB::Open (options, (path_a / "pending.ldb").string (), &db));
                if (status3.ok ())
                {
                    pending.reset (db);
                    auto status4 (leveldb::DB::Open (options, (path_a / "representation.ldb").string (), &db));
                    if (status4.ok ())
                    {
                        representation.reset (db);
                        auto status6 (leveldb::DB::Open (options, (path_a / "bootstrap.ldb").string (), &db));
                        if (status6.ok ())
                        {
                            bootstrap.reset (db);
                            auto status7 (leveldb::DB::Open (options, (path_a / "checksum.ldb").string (), &db));
                            if (status7.ok ())
                            {
                                checksum.reset (db);
                                checksum_put (0, 0, 0);
                            }
                            else
                            {
                                init_a = status7;
                            }
                        }
                        else
                        {
                            init_a = status6;
                        }
                    }
                    else
                    {
                        init_a = status4;
                    }
                }
                else
                {
                    init_a = status3;
                }
            }
            else
            {
                init_a = status2;
            }
        }
        else
        {
            init_a = status1;
        }
    }
    else
    {
        init_a = leveldb::Status::IOError ("Unable to create directories");
    }
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

bool rai::block_store::latest_get (rai::account const & account_a, rai::frontier & frontier_a)
{
    std::string value;
    auto status (accounts->Get (leveldb::ReadOptions (), leveldb::Slice (account_a.chars.data (), account_a.chars.size ()), &value));
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

void rai::block_store::latest_put (rai::account const & account_a, rai::frontier const & frontier_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        frontier_a.serialize (stream);
    }
    auto status (accounts->Put (leveldb::WriteOptions (), leveldb::Slice (account_a.chars.data (), account_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

void rai::block_store::pending_put (rai::block_hash const & hash_a, rai::receivable const & receivable_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        rai::write (stream, receivable_a.source);
        rai::write (stream, receivable_a.amount);
        rai::write (stream, receivable_a.destination);
    }
    auto status (pending->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
}

void rai::block_store::pending_del (rai::block_hash const & hash_a)
{
    auto status (pending->Delete (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

bool rai::block_store::pending_exists (rai::block_hash const & hash_a)
{
    std::unique_ptr <leveldb::Iterator> iterator (pending->NewIterator (leveldb::ReadOptions {}));
    iterator->Seek (leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()));
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

bool rai::block_store::pending_get (rai::block_hash const & hash_a, rai::receivable & receivable_a)
{
    std::string value;
    auto status (pending->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    bool result;
    if (status.IsNotFound ())
    {
        result = true;
    }
    else
    {
        result = false;
        assert (value.size () == sizeof (receivable_a.source.bytes) + sizeof (receivable_a.amount.bytes) + sizeof (receivable_a.destination.bytes));
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error1 (rai::read (stream, receivable_a.source));
        assert (!error1);
        auto error2 (rai::read (stream, receivable_a.amount));
        assert (!error2);
        auto error3 (rai::read (stream, receivable_a.destination));
        assert (!error3);
    }
    return result;
}

rai::pending_iterator rai::block_store::pending_begin ()
{
    rai::pending_iterator result (*pending);
    return result;
}

rai::pending_iterator rai::block_store::pending_end ()
{
    rai::pending_iterator result (*pending, nullptr);
    return result;
}

void rai::receivable::serialize (rai::stream & stream_a) const
{
    rai::write (stream_a, source.bytes);
    rai::write (stream_a, amount.bytes);
    rai::write (stream_a, destination.bytes);
}

bool rai::receivable::deserialize (rai::stream & stream_a)
{
    auto result (rai::read (stream_a, source.bytes));
    if (!result)
    {
        result = rai::read (stream_a, amount.bytes);
        if (!result)
        {
            result = rai::read (stream_a, destination.bytes);
        }
    }
    return result;
}

bool rai::receivable::operator == (rai::receivable const & other_a) const
{
    return source == other_a.source && amount == other_a.amount && destination == other_a.destination;
}

rai::pending_entry * rai::pending_entry::operator -> ()
{
    return this;
}

rai::pending_entry & rai::pending_iterator::operator -> ()
{
    return current;
}

rai::pending_iterator::pending_iterator (leveldb::DB & db_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

rai::pending_iterator::pending_iterator (leveldb::DB & db_a, std::nullptr_t) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

void rai::pending_iterator::set_current ()
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
        current.second.source.clear ();
        current.second.amount.clear ();
        current.second.destination.clear ();
    }
}

rai::pending_iterator & rai::pending_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}

bool rai::pending_iterator::operator == (rai::pending_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
}

bool rai::pending_iterator::operator != (rai::pending_iterator const & other_a) const
{
    return !(*this == other_a);
}

rai::uint128_t rai::block_store::representation_get (rai::account const & account_a)
{
    std::string value;
    auto status (representation->Get (leveldb::ReadOptions (), leveldb::Slice (account_a.chars.data (), account_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    rai::uint128_t result;
    if (status.ok ())
    {
        rai::uint128_union rep;
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error (rai::read (stream, rep));
        assert (!error);
        result = rep.number ();
    }
    else
    {
        result = 0;
    }
    return result;
}

void rai::block_store::representation_put (rai::account const & account_a, rai::uint128_t const & representation_a)
{
    rai::uint128_union rep (representation_a);
    auto status (representation->Put (leveldb::WriteOptions (), leveldb::Slice (account_a.chars.data (), account_a.chars.size ()), leveldb::Slice (rep.chars.data (), rep.chars.size ())));
    assert (status.ok ());
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

rai::block_iterator rai::block_store::bootstrap_begin ()
{
    rai::block_iterator result (*bootstrap);
    return result;
}

rai::block_iterator rai::block_store::bootstrap_end ()
{
    rai::block_iterator result (*bootstrap, nullptr);
    return result;
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
        auto error (rai::read (stream, hash_a));
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
    // Open blocks have no previous () so we use the account number
    void open_block (rai::open_block const & block_a) override
    {
        auto hash (block_a.source ());
        auto source (store.block_get (hash));
        if (source != nullptr)
		{
			auto send (dynamic_cast <rai::send_block *> (source.get ()));
			if (send != nullptr)
			{
				result = send->hashables.destination;
			}
			else
			{
				result.clear ();
			}
		}
		else
		{
			result.clear ();
		}
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
    rai::account_iterator result (*accounts);
    return result;
}

rai::account_iterator rai::block_store::latest_end ()
{
    rai::account_iterator result (*accounts, nullptr);
    return result;
}

namespace
{
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

// Determine the amount delta resultant from this block
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
    rai::uint128_t result;
};

// Determine the balance as of this block
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
    rai::uint128_t result;
};

// Determine the account for this block
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
        if (block != nullptr)
        {
            assert (dynamic_cast <rai::send_block *> (block.get ()) != nullptr);
            auto send (static_cast <rai::send_block *> (block.get ()));
            result = send->hashables.destination;
        }
        else
        {
            assert (hash_a == rai::genesis_account);
            result = rai::genesis_account;
        }
    }
    rai::block_store & store;
    rai::account result;
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
    assert (false);
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

// Determine the representative for this block
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
    rai::account result;
};

// Rollback this block
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
        rai::receivable receivable;
        while (ledger.store.pending_get (hash, receivable))
        {
            ledger.rollback (ledger.latest (block_a.hashables.destination));
        }
        rai::frontier frontier;
        ledger.store.latest_get (receivable.source, frontier);
        ledger.store.pending_del (hash);
        ledger.change_latest (receivable.source, block_a.hashables.previous, frontier.representative, ledger.balance (block_a.hashables.previous));
        ledger.store.block_del (hash);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_account (ledger.account (hash));
        ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_account, block_a.hashables.previous, representative, ledger.balance (block_a.hashables.previous));
        ledger.store.block_del (hash);
        ledger.store.pending_put (block_a.hashables.source, {ledger.account (block_a.hashables.source), amount, destination_account});
    }
    void open_block (rai::open_block const & block_a) override
    {
        auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_account (ledger.account (hash));
        ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_account, 0, representative, 0);
        ledger.store.block_del (hash);
        ledger.store.pending_put (block_a.hashables.source, {ledger.account (block_a.hashables.source), amount, destination_account});
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

void amount_visitor::compute (rai::block_hash const & block_hash)
{
    auto block (store.block_get (block_hash));
	if (block != nullptr)
	{
		block->visit (*this);
	}
	else
	{
		if (block_hash == rai::genesis_account)
		{
			result = std::numeric_limits <rai::uint128_t>::max ();
		}
		else
		{
			assert (false);
			result = 0;
		}
	}
}

void balance_visitor::compute (rai::block_hash const & block_hash)
{
    auto block (store.block_get (block_hash));
    assert (block != nullptr);
    block->visit (*this);
}

rai::uint128_t rai::ledger::balance (rai::block_hash const & hash_a)
{
    balance_visitor visitor (store);
    visitor.compute (hash_a);
    return visitor.result;
}

rai::uint128_t rai::ledger::account_balance (rai::account const & account_a)
{
    rai::uint128_t result (0);
    rai::frontier frontier;
    auto none (store.latest_get (account_a, frontier));
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

rai::uint128_t rai::ledger::supply ()
{
    return std::numeric_limits <rai::uint128_t>::max ();
}

rai::account rai::ledger::representative (rai::block_hash const & hash_a)
{
    auto result (representative_calculated (hash_a));
    //assert (result == representative_cached (hash_a));
    return result;
}

rai::account rai::ledger::representative_calculated (rai::block_hash const & hash_a)
{
    representative_visitor visitor (store);
    visitor.compute (hash_a);
    return visitor.result;
}

rai::account rai::ledger::representative_cached (rai::block_hash const & hash_a)
{
    assert (false);
}

rai::uint128_t rai::ledger::weight (rai::account const & account_a)
{
    return store.representation_get (account_a);
}

// Rollback blocks until `frontier_a' is the frontier block
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
    // Continue rolling back until this block is the frontier
    } while (frontier.hash != frontier_a);
}

rai::account rai::ledger::account (rai::block_hash const & hash_a)
{
    account_visitor account (store);
    account.compute (hash_a);
    return account.result;
}

rai::uint128_t rai::ledger::amount (rai::block_hash const & hash_a)
{
    amount_visitor amount (store);
    amount.compute (hash_a);
    return amount.result;
}

void rai::ledger::move_representation (rai::account const & source_a, rai::account const & destination_a, rai::uint128_t const & amount_a)
{
    auto source_previous (store.representation_get (source_a));
    assert (source_previous >= amount_a);
    store.representation_put (source_a, source_previous - amount_a);
    auto destination_previous (store.representation_get (destination_a));
    store.representation_put (destination_a, destination_previous + amount_a);
}

rai::block_hash rai::ledger::latest (rai::account const & account_a)
{
    rai::frontier frontier;
    auto latest_error (store.latest_get (account_a, frontier));
    assert (!latest_error);
    return frontier.hash;
}

rai::checksum rai::ledger::checksum (rai::account const & begin_a, rai::account const & end_a)
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

void rai::ledger::change_latest (rai::account const & account_a, rai::block_hash const & hash_a, rai::account const & representative_a, rai::amount const & balance_a)
{
    rai::frontier frontier;
    auto exists (!store.latest_get (account_a, frontier));
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
        store.latest_put (account_a, frontier);
        checksum_update (hash_a);
    }
    else
    {
        store.latest_del (account_a);
    }
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
                result = frontier.hash == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::fork_previous; // Is the previous block the latest (Malicious)
                if (result == rai::process_result::progress)
                {
                    ledger.move_representation (frontier.representative, block_a.hashables.representative, ledger.balance (block_a.hashables.previous));
                    ledger.store.block_put (message, block_a);
                    ledger.change_latest (account, message, block_a.hashables.representative, frontier.balance);
                    ledger.change_observer (block_a, account, block_a.hashables.representative);
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
                    result = frontier.hash == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::fork_previous;
                    if (result == rai::process_result::progress)
                    {
                        ledger.store.block_put (message, block_a);
                        ledger.change_latest (account, message, frontier.representative, block_a.hashables.balance);
                        ledger.store.pending_put (message, {account, frontier.balance.number () - block_a.hashables.balance.number (), block_a.hashables.destination});
                        ledger.send_observer (block_a, account, block_a.hashables.balance);
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
            rai::receivable receivable;
            result = ledger.store.pending_get (block_a.hashables.source, receivable) ? rai::process_result::overreceive : rai::process_result::progress; // Has this source already been received (Malformed)
            if (result == rai::process_result::progress)
            {
                result = rai::validate_message (receivable.destination, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
                if (result == rai::process_result::progress)
                {
                    rai::frontier frontier;
                    result = ledger.store.latest_get (receivable.destination, frontier) ? rai::process_result::gap_previous : rai::process_result::progress;  //Have we seen the previous block? No entries for account at all (Harmless)
                    if (result == rai::process_result::progress)
                    {
                        result = frontier.hash == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
                        if (result == rai::process_result::progress)
                        {
                            auto new_balance (frontier.balance.number () + receivable.amount.number ());
                            rai::frontier source_frontier;
                            auto error (ledger.store.latest_get (receivable.source, source_frontier));
                            assert (!error);
                            ledger.store.pending_del (block_a.hashables.source);
                            ledger.store.block_put (hash, block_a);
                            ledger.change_latest (receivable.destination, hash, frontier.representative, new_balance);
                            ledger.move_representation (source_frontier.representative, frontier.representative, receivable.amount.number ());
                            ledger.receive_observer (block_a, receivable.destination, new_balance);
                        }
                        else
                        {
                            result = ledger.store.block_exists (block_a.hashables.previous) ? rai::process_result::fork_previous : rai::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
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
            rai::receivable receivable;
            result = ledger.store.pending_get (block_a.hashables.source, receivable) ? rai::process_result::fork_source : rai::process_result::progress; // Has this source already been received (Malformed)
            if (result == rai::process_result::progress)
            {
                result = rai::validate_message (receivable.destination, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
                if (result == rai::process_result::progress)
                {
                    rai::frontier frontier;
                    result = ledger.store.latest_get (receivable.destination, frontier) ? rai::process_result::progress : rai::process_result::fork_previous; // Has this account already been opened? (Malicious)
                    if (result == rai::process_result::progress)
                    {
                        rai::frontier source_frontier;
                        auto error (ledger.store.latest_get (receivable.source, source_frontier));
                        assert (!error);
                        ledger.store.pending_del (block_a.hashables.source);
                        ledger.store.block_put (hash, block_a);
                        ledger.change_latest (receivable.destination, hash, block_a.hashables.representative, receivable.amount.number ());
                        ledger.move_representation (source_frontier.representative, block_a.hashables.representative, receivable.amount.number ());
                        ledger.open_observer (block_a, receivable.destination, receivable.amount, block_a.hashables.representative);
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

rai::uint128_t rai::votes::flip_threshold ()
{
    return ledger.supply () / 2;
}

rai::genesis::genesis ()
{
	open.hashables.source = genesis_account;
	open.hashables.representative = genesis_account;
	open.signature.clear ();
}

void rai::genesis::initialize (rai::block_store & store_a) const
{
	assert (store_a.latest_begin () == store_a.latest_end ());
	store_a.block_put (open.hash (), open);
	store_a.latest_put (genesis_account, {open.hash (), open.hashables.representative, std::numeric_limits <rai::uint128_t>::max (), store_a.now ()});
	store_a.representation_put (genesis_account, std::numeric_limits <rai::uint128_t>::max ());
	store_a.checksum_put (0, 0, hash ());
}

rai::block_hash rai::genesis::hash () const
{
    return open.hash ();
}