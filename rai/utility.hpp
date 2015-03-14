#pragma once

#include <array>
#include <type_traits>

#include <blake2/blake2.h>

#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <cryptopp/osrng.h>

#include <liblmdb/lmdb.h>

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
std::string to_string_hex (uint64_t);
bool from_string_hex (std::string const &, uint64_t &);

using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// Scaling factor for what is displayed to users compared to what the underlying balance is.
// 10^20 was chosen so that the largest balance amount scaled down will fit in a 64bit integer
// Base 10 reduction was chosen so scaling is intuitive to users.  If we change it in the future, it's easier to explain a base-10 balance representation change than base-16.
rai::uint128_t const scale_64bit_base10 = rai::uint128_t ("100000000000000000000");
uint64_t scale_down (rai::uint128_t const &);
rai::uint128_t scale_up (uint64_t);
class mdb_env
{
public:
	mdb_env (bool &, boost::filesystem::path const &);
	~mdb_env ();
	operator MDB_env * () const;
	MDB_env * environment;
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
	transaction (std::nullptr_t);
	transaction (MDB_env *, MDB_txn *, bool);
	~transaction ();
	operator MDB_txn * () const;
	MDB_txn * handle;
};
union uint128_union
{
public:
	uint128_union () = default;
	uint128_union (uint64_t);
	uint128_union (rai::uint128_union const &) = default;
	uint128_union (rai::uint128_t const &);
	bool operator == (rai::uint128_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);
	rai::uint128_t number () const;
	void clear ();
	bool is_zero () const;
	rai::mdb_val val () const;
	std::array <uint8_t, 16> bytes;
	std::array <char, 16> chars;
	std::array <uint32_t, 4> dwords;
	std::array <uint64_t, 2> qwords;
};
// Balances are 128 bit.
using amount = uint128_union;
union uint256_union
{
	uint256_union () = default;
	uint256_union (std::string const &);
	uint256_union (uint64_t, uint64_t = 0, uint64_t = 0, uint64_t = 0);
	uint256_union (rai::uint256_t const &);
	uint256_union (rai::uint256_union const &, rai::uint256_union const &, uint128_union const &);
	uint256_union (MDB_val const &);
	uint256_union prv (uint256_union const &, uint128_union const &) const;
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
	void encode_base58check (std::string &) const;
	std::string to_base58check () const;
	bool decode_base58check (std::string const &);
	std::array <uint8_t, 32> bytes;
	std::array <char, 32> chars;
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
rai::uint512_union sign_message (rai::private_key const &, rai::public_key const &, rai::uint256_union const &);
bool validate_message (rai::public_key const &, rai::uint256_union const &, rai::uint512_union const &);
}