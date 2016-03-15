#pragma once

#include <array>
#include <condition_variable>
#include <type_traits>

#include <blake2/blake2.h>

#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cryptopp/osrng.h>

#include <liblmdb/lmdb.h>

#include <rai/config.hpp>

namespace rai
{
extern CryptoPP::AutoSeededRandomPool random_pool;
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf <uint8_t>;
using bufferstream = boost::iostreams::stream_buffer <boost::iostreams::basic_array_source <uint8_t>>;
using vectorstream = boost::iostreams::stream_buffer <boost::iostreams::back_insert_device <std::vector <uint8_t>>>;
// OS-specific way of finding a path to a home directory.
boost::filesystem::path working_path ();
// Get a unique path within the home directory, used for testing
boost::filesystem::path unique_path ();
// Lower priority of calling work generating thread
void work_thread_reprioritize ();
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool read (rai::stream & stream_a, T & value)
{
	static_assert (std::is_pod <T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast <uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
template <typename T>
void write (rai::stream & stream_a, T const & value)
{
	static_assert (std::is_pod <T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast <uint8_t const *> (&value), sizeof (value)));
	assert (amount_written == sizeof (value));
}
// C++ stream are absolutely horrible so I need this helper function to do the most basic operation of creating a file if it doesn't exist or truntacing it.
void open_or_create (std::fstream &, std::string const &);
// Reads a json object from the stream and if was changed, write the object back to the stream
template <typename T>
bool fetch_object (T & object, std::iostream & stream_a)
{
	assert (stream_a.tellg () == std::streampos (0) || stream_a.tellg () == std::streampos (-1));
	assert (stream_a.tellp () == std::streampos (0) || stream_a.tellp () == std::streampos (-1));
	bool error (false);
    boost::property_tree::ptree tree;
	try
	{
		boost::property_tree::read_json (stream_a, tree);
	}
	catch (std::runtime_error const &)
	{
		auto pos (stream_a.tellg ());
		if (pos != std::streampos(0))
		{
			error = true;
		}
	}
	if (!error)
	{
		auto updated (false);
		error = object.deserialize_json (updated, tree);
		if (!error && updated)
		{
			stream_a.seekp (0);
			try
			{
				boost::property_tree::write_json(stream_a, tree);
			}
			catch (std::runtime_error const &)
			{
				error = true;
			}
		}
	}
	return error;
}
std::string to_string_hex (uint64_t);
bool from_string_hex (std::string const &, uint64_t &);

using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// SI dividers
rai::uint128_t const Grai_ratio = rai::uint128_t ("1000000000000000000000000000000000"); // 10^33
rai::uint128_t const Mrai_ratio = rai::uint128_t ("1000000000000000000000000000000"); // 10^30
rai::uint128_t const krai_ratio = rai::uint128_t ("1000000000000000000000000000"); // 10^27
rai::uint128_t const  rai_ratio = rai::uint128_t ("1000000000000000000000000"); // 10^24
rai::uint128_t const mrai_ratio = rai::uint128_t ("1000000000000000000000"); // 10^21
rai::uint128_t const urai_ratio = rai::uint128_t ("1000000000000000000"); // 10^18
class mdb_env
{
public:
	mdb_env (bool &, boost::filesystem::path const &);
	~mdb_env ();
	operator MDB_env * () const;
	void add_transaction ();
	void remove_transaction ();
	MDB_env * environment;
	std::mutex lock;
	std::condition_variable open_notify;
	unsigned open_transactions;
	unsigned transaction_iteration;
	std::condition_variable resize_notify;
	bool resizing;
};
class mdb_val
{
public:
	mdb_val (size_t, void *);
	operator MDB_val * () const;
	operator MDB_val const & () const;
	MDB_val value;
};
class transaction
{
public:
	transaction (rai::mdb_env &, MDB_txn *, bool);
	~transaction ();
	operator MDB_txn * () const;
	MDB_txn * handle;
	rai::mdb_env & environment;
};
union uint128_union
{
public:
	uint128_union () = default;
	uint128_union (std::string const &);
	uint128_union (uint64_t);
	uint128_union (rai::uint128_union const &) = default;
	uint128_union (rai::uint128_t const &);
	bool operator == (rai::uint128_union const &) const;
	bool operator != (rai::uint128_union const &) const;
	bool operator < (rai::uint128_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);
	rai::uint128_t number () const;
	void clear ();
	bool is_zero () const;
	rai::mdb_val val () const;
	std::string to_string () const;
	std::string to_string_dec () const;
	std::array <uint8_t, 16> bytes;
	std::array <char, 16> chars;
	std::array <uint32_t, 4> dwords;
	std::array <uint64_t, 2> qwords;
};
// Balances are 128 bit.
using amount = uint128_union;
class raw_key;
union uint256_union
{
	uint256_union () = default;
	uint256_union (std::string const &);
	uint256_union (uint64_t);
	uint256_union (rai::uint256_t const &);
	uint256_union (MDB_val const &);
	void encrypt (rai::raw_key const &, rai::raw_key const &, uint128_union const &);
	uint256_union & operator ^= (rai::uint256_union const &);
	uint256_union operator ^ (rai::uint256_union const &) const;
	bool operator == (rai::uint256_union const &) const;
	bool operator != (rai::uint256_union const &) const;
	bool operator < (rai::uint256_union const &) const;
	rai::mdb_val val () const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);
	void encode_account (std::string &) const;
	std::string to_account () const;
	std::string to_account_split () const;
	bool decode_account_v1 (std::string const &);
	bool decode_account (std::string const &);
	std::array <uint8_t, 32> bytes;
	std::array <char, 32> chars;
	std::array <uint32_t, 8> dwords;
	std::array <uint64_t, 4> qwords;
	std::array <uint128_union, 2> owords;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	rai::uint256_t number () const;
};
// All keys and hashes are 256 bit.
using block_hash = uint256_union;
using account = uint256_union;
using public_key = uint256_union;
using private_key = uint256_union;
using secret_key = uint256_union;
using checksum = uint256_union;
class raw_key
{
public:
	raw_key () = default;
	~raw_key ();
	void decrypt (rai::uint256_union const &, rai::raw_key const &, uint128_union const &);
	raw_key (rai::raw_key const &) = delete;
	raw_key (rai::raw_key const &&) = delete;
	rai::raw_key & operator = (rai::raw_key const &) = delete;
	bool operator == (rai::raw_key const &) const;
	bool operator != (rai::raw_key const &) const;
	rai::uint256_union data;
};
union uint512_union
{
	uint512_union () = default;
	uint512_union (rai::uint512_t const &);
	bool operator == (rai::uint512_union const &) const;
	bool operator != (rai::uint512_union const &) const;
	rai::uint512_union & operator ^= (rai::uint512_union const &);
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	std::array <uint8_t, 64> bytes;
	std::array <uint32_t, 16> dwords;
	std::array <uint64_t, 8> qwords;
	std::array <uint256_union, 2> uint256s;
	void clear ();
	boost::multiprecision::uint512_t number () const;
};
// Only signatures are 512 bit.
using signature = uint512_union;
rai::uint512_union sign_message (rai::raw_key const &, rai::public_key const &, rai::uint256_union const &);
bool validate_message (rai::public_key const &, rai::uint256_union const &, rai::uint512_union const &);
}
namespace std
{
template <>
struct hash <rai::uint256_union>
{
	size_t operator () (rai::uint256_union const & data_a) const
	{
		return *reinterpret_cast <size_t const *> (data_a.bytes.data ());
	}
};
template <>
struct hash <rai::uint256_t>
{
	size_t operator () (rai::uint256_t const & number_a) const
	{
		return number_a.convert_to <size_t> ();
	}
};
}