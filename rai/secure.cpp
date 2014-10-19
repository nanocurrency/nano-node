#include <rai/secure.hpp>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

CryptoPP::AutoSeededRandomPool rai::random_pool;

void rai::uint256_union::digest_password (std::string const & password_a)
{
	CryptoPP::SHA3 hash (32);
	hash.Update (reinterpret_cast <uint8_t const *> (password_a.c_str ()), password_a.size ());
	hash.Final (bytes.data ());
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
				ledger.rollback (last_winner->hash ());
				ledger.process (*winner_l.first);
				last_winner = std::move (winner_l.first);
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
		auto weight (ledger.weight (i.first));
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

rai::votes::votes (rai::ledger & ledger_a, rai::block const & block_a) :
ledger (ledger_a),
root (ledger.store.root (block_a)),
last_winner (block_a.clone ()),
sequence (0)
{
}

rai::keypair::keypair ()
{
    random_pool.GenerateBlock (prv.bytes.data (), prv.bytes.size ());
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

rai::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

rai::ledger::ledger (bool & init_a, leveldb::Status const & store_init_a, rai::block_store & store_a) :
store (store_a)
{
    if (store_init_a.ok ())
    {
        store.checksum_put (0, 0, 0);
        init_a = false;
    }
    else
    {
        init_a = true;
    }
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

void rai::receive_block::visit (rai::block_visitor & visitor_a) const
{
    visitor_a.receive_block (*this);
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

bool rai::receive_block::validate (rai::public_key const & key, rai::uint256_t const & hash) const
{
    return validate_message (key, hash, signature);
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
	hash_a.Update (source.bytes.data (), sizeof (source.bytes));
	hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
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
    bytes.fill (0);
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

rai::uint256_union::uint256_union (uint64_t value)
{
    *this = rai::uint256_t (value);
}

bool rai::uint256_union::operator != (rai::uint256_union const & other_a) const
{
    return ! (*this == other_a);
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

void rai::uint512_union::clear ()
{
    bytes.fill (0);
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

void rai::serialize_block (rai::stream & stream_a, rai::block const & block_a)
{
    write (stream_a, block_a.type ());
    block_a.serialize (stream_a);
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

rai::send_block::send_block (send_block const & other_a) :
hashables (other_a.hashables),
signature (other_a.signature)
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
    auto result (signature == other_a.signature && hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance);
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

rai::block_hash rai::open_block::source () const
{
    return hashables.source;
}

void rai::change_hashables::hash (CryptoPP::SHA3 & hash_a) const
{
    hash_a.Update (representative.bytes.data (), sizeof (representative.bytes));
    hash_a.Update (previous.bytes.data (), sizeof (previous.bytes));
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

rai::account_iterator::account_iterator (leveldb::DB & db_a, rai::address const & address_a) :
iterator (db_a.NewIterator (leveldb::ReadOptions ()))
{
    iterator->Seek (leveldb::Slice (address_a.chars.data (), address_a.chars.size ()));
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
        auto status1 (leveldb::DB::Open (options, (path_a / "addresses.ldb").string (), &db));
        if (status1.ok ())
        {
            addresses.reset (db);
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
                        auto status5 (leveldb::DB::Open (options, (path_a / "forks.ldb").string (), &db));
                        if (status5.ok ())
                        {
                            forks.reset (db);
                            auto status6 (leveldb::DB::Open (options, (path_a / "bootstrap.ldb").string (), &db));
                            if (status6.ok ())
                            {
                                bootstrap.reset (db);
                                auto status7 (leveldb::DB::Open (options, (path_a / "checksum.ldb").string (), &db));
                                if (status7.ok ())
                                {
                                    checksum.reset (db);
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
                            init_a = status5;
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

rai::uint256_t rai::ledger::supply ()
{
    return std::numeric_limits <rai::uint256_t>::max ();
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

rai::uint256_t rai::votes::flip_threshold ()
{
    return ledger.supply () / 2;
}

namespace {
    std::string rai_test_private_key = "E49C03BB7404C10B388AE56322217306B57F3DCBB3A5F060A2F420AD7AA3F034";
    std::string rai_test_public_key = "1149338F7D0DA66D7ED0DAA4F1F72431831B3D06AFC704F3224D68B317CC41B2"; // U63Kt2zHcikQvirWSSNKZHbfVZsPY68A65zyD1NtQoE5HsWZTf
    std::string rai_live_public_key = "0";
}
rai::keypair rai::test_genesis_key (rai_test_private_key);
rai::address rai::rai_test_address (rai_test_public_key);
rai::address rai::rai_live_address (rai_live_public_key);
rai::address rai::genesis_address (GENESIS_KEY);