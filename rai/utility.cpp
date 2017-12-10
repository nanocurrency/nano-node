#include <rai/utility.hpp>

#include <rai/interface.h>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <ed25519-donna/ed25519.h>

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
	auto result (value_a.empty ());
	if (!result)
	{
		result = value_a.size () > 16;
		if (!result)
		{
			std::stringstream stream (value_a);
			stream << std::hex << std::noshowbase;
			uint64_t number_l;
			try
			{
				stream >> number_l;
				target_a = number_l;
				if (!stream.eof())
				{
					result = true;
				}
			}
			catch (std::runtime_error &)
			{
				result = true;
			}
		}
	}
    return result;
}

rai::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a)
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
			auto status3 (mdb_env_set_mapsize (environment, 1ULL * 1024 * 1024 * 1024 * 1024)); // 1 Terabyte
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

rai::mdb_val::mdb_val () :
value ({0, nullptr})
{
}

rai::mdb_val::mdb_val (MDB_val const & value_a) :
value (value_a)
{
}

rai::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({size_a, data_a})
{
}

rai::mdb_val::mdb_val (rai::uint128_union const & val_a) :
mdb_val (sizeof (val_a), const_cast <rai::uint128_union *> (&val_a))
{
}

rai::mdb_val::mdb_val (rai::uint256_union const & val_a) :
mdb_val (sizeof (val_a), const_cast <rai::uint256_union *> (&val_a))
{
}

void * rai::mdb_val::data () const
{
	return value.mv_data;
}

size_t rai::mdb_val::size () const
{
	return value.mv_size;
}

rai::uint256_union rai::mdb_val::uint256 () const
{
	rai::uint256_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast <uint8_t const *> (data ()), reinterpret_cast <uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
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
	auto status (mdb_txn_begin (environment_a, parent_a, write ? 0 : MDB_RDONLY, &handle));
	assert (status == 0);
}

rai::transaction::~transaction ()
{
	auto status (mdb_txn_commit (handle));
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

bool rai::uint128_union::operator > (rai::uint128_union const & other_a) const
{
	return number () > other_a.number ();
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
			if (!stream.eof ())
			{
				result = true;
			}
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
