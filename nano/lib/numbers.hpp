#pragma once

#include <crypto/cryptopp/osrng.h>

#include <boost/multiprecision/cpp_int.hpp>

namespace nano
{
using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// SI dividers
nano::uint128_t const Gxrb_ratio = nano::uint128_t ("1000000000000000000000000000000000"); // 10^33
nano::uint128_t const Mxrb_ratio = nano::uint128_t ("1000000000000000000000000000000"); // 10^30
nano::uint128_t const kxrb_ratio = nano::uint128_t ("1000000000000000000000000000"); // 10^27
nano::uint128_t const xrb_ratio = nano::uint128_t ("1000000000000000000000000"); // 10^24
nano::uint128_t const raw_ratio = nano::uint128_t ("1"); // 10^0

union uint128_union final
{
public:
	uint128_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint128_union (std::string const &);
	uint128_union (uint64_t);
	uint128_union (nano::uint128_t const &);
	bool operator== (nano::uint128_union const &) const;
	bool operator!= (nano::uint128_union const &) const;
	bool operator< (nano::uint128_union const &) const;
	bool operator> (nano::uint128_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &, bool = false);
	bool decode_dec (std::string const &, nano::uint128_t);
	std::string format_balance (nano::uint128_t scale, int precision, bool group_digits);
	std::string format_balance (nano::uint128_t scale, int precision, bool group_digits, const std::locale & locale);
	nano::uint128_t number () const;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	std::string to_string_dec () const;
	std::array<uint8_t, 16> bytes;
	std::array<char, 16> chars;
	std::array<uint32_t, 4> dwords;
	std::array<uint64_t, 2> qwords;
};
// Balances are 128 bit.
using amount = uint128_union;
class raw_key;
union uint256_union final
{
	uint256_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint256_union (std::string const &);
	uint256_union (uint64_t);
	uint256_union (nano::uint256_t const &);
	void encrypt (nano::raw_key const &, nano::raw_key const &, uint128_union const &);
	uint256_union & operator^= (nano::uint256_union const &);
	uint256_union operator^ (nano::uint256_union const &) const;
	bool operator== (nano::uint256_union const &) const;
	bool operator!= (nano::uint256_union const &) const;
	bool operator< (nano::uint256_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);
	void encode_account (std::string &) const;
	std::string to_account () const;
	bool decode_account (std::string const &);
	std::array<uint8_t, 32> bytes;
	std::array<char, 32> chars;
	std::array<uint32_t, 8> dwords;
	std::array<uint64_t, 4> qwords;
	std::array<uint128_union, 2> owords;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	nano::uint256_t number () const;
};
// All keys and hashes are 256 bit.
using block_hash = uint256_union;
using account = uint256_union;
using public_key = uint256_union;
using private_key = uint256_union;
using secret_key = uint256_union;
class raw_key final
{
public:
	~raw_key ();
	void decrypt (nano::uint256_union const &, nano::raw_key const &, uint128_union const &);
	bool operator== (nano::raw_key const &) const;
	bool operator!= (nano::raw_key const &) const;
	nano::uint256_union data;
};
union uint512_union final
{
	uint512_union () = default;
	uint512_union (nano::uint256_union const &, nano::uint256_union const &);
	uint512_union (nano::uint512_t const &);
	bool operator== (nano::uint512_union const &) const;
	bool operator!= (nano::uint512_union const &) const;
	nano::uint512_union & operator^= (nano::uint512_union const &);
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	std::array<uint8_t, 64> bytes;
	std::array<uint32_t, 16> dwords;
	std::array<uint64_t, 8> qwords;
	std::array<uint256_union, 2> uint256s;
	void clear ();
	bool is_zero () const;
	nano::uint512_t number () const;
	std::string to_string () const;
};
using signature = uint512_union;
using qualified_root = uint512_union;

nano::uint512_union sign_message (nano::raw_key const &, nano::public_key const &, nano::uint256_union const &);
bool validate_message (nano::public_key const &, nano::uint256_union const &, nano::uint512_union const &);
bool validate_message_batch (const unsigned char **, size_t *, const unsigned char **, const unsigned char **, size_t, int *);
void deterministic_key (nano::uint256_union const &, uint32_t, nano::uint256_union &);
nano::public_key pub_key (nano::private_key const &);

/* Conversion methods */
std::string to_string_hex (uint64_t const);
bool from_string_hex (std::string const &, uint64_t &);

/**
 * Convert a double to string in fixed format
 * @param precision_a (optional) use a specific precision (default is the maximum)
 */
std::string to_string (double const, int const precision_a = std::numeric_limits<double>::digits10);

namespace difficulty
{
	uint64_t from_multiplier (double const, uint64_t const);
	double to_multiplier (uint64_t const, uint64_t const);
}
}

namespace std
{
template <>
struct hash<::nano::uint256_union>
{
	size_t operator() (::nano::uint256_union const & data_a) const
	{
		return *reinterpret_cast<size_t const *> (data_a.bytes.data ());
	}
};
template <>
struct hash<::nano::uint256_t>
{
	size_t operator() (::nano::uint256_t const & number_a) const
	{
		return number_a.convert_to<size_t> ();
	}
};
template <>
struct hash<::nano::uint512_union>
{
	size_t operator() (::nano::uint512_union const & data_a) const
	{
		return *reinterpret_cast<size_t const *> (data_a.bytes.data ());
	}
};
}
