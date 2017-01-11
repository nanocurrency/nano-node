#include <rai/utility.hpp>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

#include <ed25519-donna/ed25519.h>

#include <liblmdb/lmdb.h>

CryptoPP::AutoSeededRandomPool rai::random_pool;

boost::filesystem::path rai::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	return result;
}

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

rai::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a) :
open_transactions (0),
transaction_iteration (0),
resizing (false)
{
	boost::system::error_code error;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error);
		if (!error)
		{
			auto status1 (mdb_env_create (&environment));
			assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, 128));
			assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, database_size_increment));
			assert (status3 == 0);
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), MDB_NOSUBDIR, 00600));
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

rai::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

rai::mdb_env::operator MDB_env * () const
{
	return environment;
}

void rai::mdb_env::add_transaction ()
{
	std::unique_lock <std::mutex> lock_l (lock);
	while (resizing)
	{
		resize_notify.wait (lock_l);
	}
	if ((transaction_iteration % rai::database_check_interval) == 0)
	{
		MDB_stat stats;
		mdb_env_stat (environment, &stats);
		MDB_envinfo info;
		mdb_env_info (environment, &info);
		size_t load (info.me_last_pgno * stats.ms_psize);
		auto slack (info.me_mapsize - load);
		if (slack < (rai::database_size_increment / 4))
		{
			resizing = true;
			auto done (std::chrono::system_clock::now () + std::chrono::milliseconds (500));
			while (std::chrono::system_clock::now () < done && open_transactions > 0)
			{
				open_notify.wait_for (lock_l, std::chrono::milliseconds (50));
			}
			if (open_transactions == 0)
			{
				auto next_size (((info.me_mapsize / database_size_increment) + 1) * database_size_increment);
				mdb_env_set_mapsize (environment, next_size);
			}
			resizing = false;
			resize_notify.notify_all ();
		}
	}
	++transaction_iteration;
	++open_transactions;
}

void rai::mdb_env::remove_transaction ()
{
	std::lock_guard <std::mutex> lock_l (lock);
	--open_transactions;
	open_notify.notify_all ();
}

rai::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({size_a, data_a})
{
}

rai::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast <MDB_val *> (&value);
};

rai::mdb_val::operator MDB_val const & () const
{
	return value;
}

rai::transaction::transaction (rai::mdb_env & environment_a, MDB_txn * parent_a, bool write) :
environment (environment_a)
{
	environment_a.add_transaction ();
	auto status (mdb_txn_begin (environment_a, parent_a, write ? 0 : MDB_RDONLY, &handle));
	assert (status == 0);
}

rai::transaction::~transaction ()
{
	auto status (mdb_txn_commit (handle));
	environment.remove_transaction ();
	assert (status == 0);
}

rai::transaction::operator MDB_txn * () const
{
	return handle;
}

rai::uint128_union::uint128_union (std::string const & string_a)
{
	decode_hex (string_a);
}

rai::uint128_union::uint128_union (uint64_t value_a)
{
	*this = rai::uint128_t (value_a);
}

rai::uint128_union::uint128_union (rai::uint128_t const & value_a)
{
    rai::uint128_t number_l (value_a);
	for (auto i (bytes.rbegin ()), n (bytes.rend ()); i != n; ++i)
	{
		*i = ((number_l) & 0xff).convert_to <uint8_t> ();
		number_l >>= 8;
	}
}

bool rai::uint128_union::operator == (rai::uint128_union const & other_a) const
{
    return qwords [0] == other_a.qwords [0] && qwords [1] == other_a.qwords [1];
}

bool rai::uint128_union::operator != (rai::uint128_union const & other_a) const
{
	return !(*this == other_a);
}

bool rai::uint128_union::operator < (rai::uint128_union const & other_a) const
{
	return number () < other_a.number ();
}

rai::uint128_t rai::uint128_union::number () const
{
    rai::uint128_t result;
	auto shift (0);
	for (auto i (bytes.begin ()), n (bytes.end ()); i != n; ++i)
	{
		result <<= shift;
		result |= *i;
		shift = 8;
	}
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

rai::mdb_val rai::uint128_union::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast <rai::uint128_union *> (this));
}

std::string rai::uint128_union::to_string () const
{
	std::string result;
	encode_hex (result);
	return result;
}

std::string rai::uint128_union::to_string_dec () const
{
	std::string result;
	encode_dec (result);
	return result;
}

bool rai::uint256_union::operator == (rai::uint256_union const & other_a) const
{
	return bytes == other_a.bytes;
}

// Construct a uint256_union = AES_ENC_CTR (cleartext, key, iv)
void rai::uint256_union::encrypt (rai::raw_key const & cleartext, rai::raw_key const & key, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key.data.bytes.data (), sizeof (key.data.bytes));
    CryptoPP::CTR_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
	enc.ProcessData (bytes.data (), cleartext.data.bytes.data (), sizeof (cleartext.data.bytes));
}

rai::uint256_union::uint256_union (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (bytes) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast <uint8_t const *> (val_a.mv_data), reinterpret_cast <uint8_t const *> (val_a.mv_data) + sizeof (*this), bytes.data ());
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
    rai::uint256_t result;
	auto shift (0);
	for (auto i (bytes.begin ()), n (bytes.end ()); i != n; ++i)
	{
		result <<= shift;
		result |= *i;
		shift = 8;
	}
    return result;
}

rai::mdb_val rai::uint256_union::val () const
{
	return rai::mdb_val (bytes.size (), const_cast <uint8_t *> (bytes.data ()));
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

rai::uint256_union::uint256_union (uint64_t value0)
{
    *this = rai::uint256_t (value0);
}

bool rai::uint256_union::operator != (rai::uint256_union const & other_a) const
{
    return ! (*this == other_a);
}

rai::raw_key::~raw_key ()
{
	data.clear ();
}

bool rai::raw_key::operator == (rai::raw_key const & other_a) const
{
	return data == other_a.data;
}

bool rai::raw_key::operator != (rai::raw_key const & other_a) const
{
	return !(*this == other_a);
}

// This this = AES_DEC_CTR (ciphertext, key, iv)
void rai::raw_key::decrypt (rai::uint256_union const & ciphertext, rai::raw_key const & key_a, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key_a.data.bytes.data (), sizeof (key_a.data.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
	dec.ProcessData (data.bytes.data (), ciphertext.bytes.data (), sizeof (ciphertext.bytes));
}

namespace
{
    char const * base58_reverse ("~012345678~~~~~~~9:;<=>?@~ABCDE~FGHIJKLMNOP~~~~~~QRSTUVWXYZ[~\\]^_`abcdefghi");
    uint8_t base58_decode (char value)
    {
		assert (value >= '0');
		assert (value <= '~');
        auto result (base58_reverse [value - 0x30] - 0x30);
        return result;
    }
    char const * account_lookup ("13456789abcdefghijkmnopqrstuwxyz");
    char const * account_reverse ("~0~1234567~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~89:;<=>?@AB~CDEFGHIJK~LMNO~~~~~");
    char account_encode (uint8_t value)
    {
        assert (value < 32);
        auto result (account_lookup [value]);
        return result;
    }
    uint8_t account_decode (char value)
    {
		assert (value >= '0');
		assert (value <= '~');
        auto result (account_reverse [value - 0x30] - 0x30);
        return result;
    }
}

void rai::uint256_union::encode_account (std::string & destination_a) const
{
    assert (destination_a.empty ());
    destination_a.reserve (64);
    uint64_t check (0);
    blake2b_state hash;
	blake2b_init (&hash, 5);
    blake2b_update (&hash, bytes.data (), bytes.size ());
    blake2b_final (&hash, reinterpret_cast <uint8_t *> (&check), 5);
    rai::uint512_t number_l (number ());
	number_l <<= 40;
    number_l |= rai::uint512_t (check);
    for (auto i (0); i < 60; ++i)
    {
        auto r (number_l.convert_to <uint8_t> () & 0x1f);
        number_l >>= 5;
        destination_a.push_back (account_encode (r));
    }
	destination_a.append ("_brx"); // xrb_
    std::reverse (destination_a.begin (), destination_a.end ());
}

std::string rai::uint256_union::to_account_split () const
{
	auto result (to_account ());
	assert (result.size () == 64);
	result.insert (32, "\n");
	return result;
}

std::string rai::uint256_union::to_account () const
{
	std::string result;
	encode_account (result);
	return result;
}

bool rai::uint256_union::decode_account_v1 (std::string const & source_a)
{
    auto result (source_a.size () != 50);
    if (!result)
    {
        rai::uint512_t number_l;
        for (auto i (source_a.begin ()), j (source_a.end ()); !result && i != j; ++i)
        {
			uint8_t character (*i);
			result = character < 0x30 || character >= 0x80;
			if (!result)
			{
				uint8_t byte (base58_decode (character));
				result = byte == '~';
				if (!result)
				{
					number_l *= 58;
					number_l += byte;
				}
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
                blake2b_state hash;
				blake2b_init (&hash, sizeof (validation));
                blake2b_update (&hash, bytes.data (), sizeof (bytes));
                blake2b_final (&hash, reinterpret_cast <uint8_t *> (&validation), sizeof (validation));
                result = check != validation;
            }
        }
    }
    return result;
}

bool rai::uint256_union::decode_account (std::string const & source_a)
{
    auto result (source_a.size () != 64);
    if (!result)
    {
		if (source_a [0] == 'x' && source_a [1] == 'r' && source_a [2] == 'b' && (source_a [3] == '_' || source_a [3] == '-'))
		{
			rai::uint512_t number_l;
			for (auto i (source_a.begin () + 4), j (source_a.end ()); !result && i != j; ++i)
			{
				uint8_t character (*i);
				result = character < 0x30 || character >= 0x80;
				if (!result)
				{
					uint8_t byte (account_decode (character));
					result = byte == '~';
					if (!result)
					{
						number_l <<= 5;
						number_l += byte;
					}
				}
			}
			if (!result)
			{
				*this = (number_l >> 40).convert_to <rai::uint256_t> ();
				uint64_t check (number_l.convert_to <uint64_t> ());
				check &=  0xffffffffff;
				uint64_t validation (0);
				blake2b_state hash;
				blake2b_init (&hash, 5);
				blake2b_update (&hash, bytes.data (), bytes.size ());
				blake2b_final (&hash, reinterpret_cast <uint8_t *> (&validation), 5);
				result = check != validation;
			}
		}
		else
		{
			result = true;
		}
    }
	else
	{
		result = decode_account_v1 (source_a);
	}
    return result;
}

rai::uint256_union::uint256_union (rai::uint256_t const & number_a)
{
    rai::uint256_t number_l (number_a);
	for (auto i (bytes.rbegin ()), n (bytes.rend ()); i != n; ++i)
	{
		*i = ((number_l) & 0xff).convert_to <uint8_t> ();
		number_l >>= 8;
	}
}

bool rai::uint512_union::operator == (rai::uint512_union const & other_a) const
{
	return bytes == other_a.bytes;
}

rai::uint512_union::uint512_union (rai::uint512_t const & number_a)
{
    rai::uint512_t number_l (number_a);
	for (auto i (bytes.rbegin ()), n (bytes.rend ()); i != n; ++i)
	{
		*i = ((number_l) & 0xff).convert_to <uint8_t> ();
		number_l >>= 8;
	}
}

void rai::uint512_union::clear ()
{
    bytes.fill (0);
}

rai::uint512_t rai::uint512_union::number () const
{
    rai::uint512_t result;
	auto shift (0);
	for (auto i (bytes.begin ()), n (bytes.end ()); i != n; ++i)
	{
		result <<= shift;
		result |= *i;
		shift = 8;
	}
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

extern "C"
{
#include <ed25519-donna/ed25519-hash-custom.h>
void ed25519_randombytes_unsafe (void * out, size_t outlen)
{
    rai::random_pool.GenerateBlock (reinterpret_cast <uint8_t *> (out), outlen);
}
void ed25519_hash_init (ed25519_hash_context * ctx)
{
    ctx->blake2 = new blake2b_state;
	blake2b_init (reinterpret_cast <blake2b_state *> (ctx->blake2), 64);
}

void ed25519_hash_update (ed25519_hash_context * ctx, uint8_t const * in, size_t inlen)
{
    blake2b_update (reinterpret_cast <blake2b_state *> (ctx->blake2), in, inlen);
}

void ed25519_hash_final (ed25519_hash_context * ctx, uint8_t * out)
{
    blake2b_final (reinterpret_cast <blake2b_state *> (ctx->blake2), out, 64);
    delete reinterpret_cast <blake2b_state *> (ctx->blake2);
}

void ed25519_hash (uint8_t * out, uint8_t const * in, size_t inlen)
{
    ed25519_hash_context ctx;
    ed25519_hash_init (&ctx);
    ed25519_hash_update (&ctx, in, inlen);
    ed25519_hash_final (&ctx, out);
}
}

rai::uint512_union rai::sign_message (rai::raw_key const & private_key, rai::public_key const & public_key, rai::uint256_union const & message)
{
	rai::uint512_union result;
    ed25519_sign (message.bytes.data (), sizeof (message.bytes), private_key.data.bytes.data (), public_key.bytes.data (), result.bytes.data ());
	return result;
}

bool rai::validate_message (rai::public_key const & public_key, rai::uint256_union const & message, rai::uint512_union const & signature)
{
    auto result (0 != ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), public_key.bytes.data (), signature.bytes.data ()));
    return result;
}

void rai::open_or_create (std::fstream & stream_a, std::string const & path_a)
{
	stream_a.open (path_a, std::ios_base::in);
	if (stream_a.fail ())
	{
		stream_a.open (path_a, std::ios_base::out);
	}
	stream_a.close ();
	stream_a.open (path_a, std::ios_base::in | std::ios_base::out);
}
