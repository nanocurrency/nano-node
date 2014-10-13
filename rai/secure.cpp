#include <rai/secure.hpp>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

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