#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

#include <boost/container_hash/hash.hpp>

#include <thread>
#include <unordered_set>

TEST (numbers, identity)
{
	ASSERT_EQ (1, nano::uint128_union (1).number ().convert_to<uint8_t> ());
	ASSERT_EQ (1, nano::uint256_union (1).number ().convert_to<uint8_t> ());
	ASSERT_EQ (1, nano::uint512_union (1).number ().convert_to<uint8_t> ());
}

namespace
{
template <typename Type>
void check_operator_less_than (Type lhs, Type rhs)
{
	ASSERT_TRUE (lhs < rhs);
	ASSERT_FALSE (rhs < lhs);
	ASSERT_FALSE (lhs < lhs);
	ASSERT_FALSE (rhs < rhs);
}

template <typename Type>
void test_operator_less_than ()
{
	using underlying_t = typename Type::underlying_type;

	// Small
	check_operator_less_than (Type{ 123 }, Type{ 124 });
	check_operator_less_than (Type{ 124 }, Type{ 125 });

	// Medium
	check_operator_less_than (Type{ std::numeric_limits<uint16_t>::max () - 1 }, Type{ std::numeric_limits<uint16_t>::max () + 1 });
	check_operator_less_than (Type{ std::numeric_limits<uint32_t>::max () - 12345678 }, Type{ std::numeric_limits<uint32_t>::max () - 123456 });

	// Large
	check_operator_less_than (Type{ std::numeric_limits<uint64_t>::max () - 555555555555 }, Type{ std::numeric_limits<uint64_t>::max () - 1 });

	// Boundary values
	check_operator_less_than (Type{ std::numeric_limits<underlying_t>::min () }, Type{ std::numeric_limits<underlying_t>::max () });
}

template <typename Type>
void check_operator_greater_than (Type lhs, Type rhs)
{
	ASSERT_TRUE (lhs > rhs);
	ASSERT_FALSE (rhs > lhs);
	ASSERT_FALSE (lhs > lhs);
	ASSERT_FALSE (rhs > rhs);
}

template <typename Type>
void test_operator_greater_than ()
{
	using underlying_t = typename Type::underlying_type;

	// Small
	check_operator_greater_than (Type{ 124 }, Type{ 123 });
	check_operator_greater_than (Type{ 125 }, Type{ 124 });

	// Medium
	check_operator_greater_than (Type{ std::numeric_limits<uint16_t>::max () + 1 }, Type{ std::numeric_limits<uint16_t>::max () - 1 });
	check_operator_greater_than (Type{ std::numeric_limits<uint32_t>::max () - 123456 }, Type{ std::numeric_limits<uint32_t>::max () - 12345678 });

	// Large
	check_operator_greater_than (Type{ std::numeric_limits<uint64_t>::max () - 1 }, Type{ std::numeric_limits<uint64_t>::max () - 555555555555 });

	// Boundary values
	check_operator_greater_than (Type{ std::numeric_limits<underlying_t>::max () }, Type{ std::numeric_limits<underlying_t>::min () });
}

template <typename Type>
void test_comparison ()
{
	test_operator_less_than<Type> ();
	test_operator_greater_than<Type> ();
}
}

TEST (numbers, comparison)
{
	test_comparison<nano::uint128_union> ();
	test_comparison<nano::uint256_union> ();
	test_comparison<nano::uint512_union> ();
	test_comparison<nano::block_hash> ();
	test_comparison<nano::public_key> ();
	test_comparison<nano::hash_or_account> ();
	test_comparison<nano::link> ();
	test_comparison<nano::root> ();
	test_comparison<nano::raw_key> ();
	test_comparison<nano::wallet_id> ();
	test_comparison<nano::qualified_root> ();
}

namespace
{
template <typename Type, template <typename> class Hash>
void test_hashing ()
{
	Hash<Type> hash;
	using underlying_t = typename Type::underlying_type;

	// Basic equality tests
	ASSERT_EQ (hash (Type{}), hash (Type{}));
	ASSERT_EQ (hash (Type{ 123 }), hash (Type{ 123 }));

	// Basic inequality tests
	ASSERT_NE (hash (Type{ 123 }), hash (Type{ 124 }));
	ASSERT_NE (hash (Type{ 0 }), hash (Type{ 1 }));

	// Boundary value tests
	constexpr auto min_val = std::numeric_limits<underlying_t>::min ();
	constexpr auto max_val = std::numeric_limits<underlying_t>::max ();

	// Min/Max tests
	ASSERT_EQ (hash (Type{ min_val }), hash (Type{ min_val }));
	ASSERT_EQ (hash (Type{ max_val }), hash (Type{ max_val }));
	ASSERT_NE (hash (Type{ min_val }), hash (Type{ max_val }));

	// Near boundary tests
	ASSERT_NE (hash (Type{ min_val }), hash (Type{ min_val + 1 }));
	ASSERT_NE (hash (Type{ max_val }), hash (Type{ max_val - 1 }));
	ASSERT_NE (hash (Type{ min_val + 1 }), hash (Type{ max_val }));
	ASSERT_NE (hash (Type{ max_val - 1 }), hash (Type{ min_val }));

	// Common value tests
	std::vector<underlying_t> common_values = {
		0, // Zero
		1, // One
		42, // Common test value
		0xFF, // Byte boundary
		0xFFFF, // Word boundary
		min_val, // Minimum
		max_val, // Maximum
		max_val / 2, // Middle value
		min_val + (max_val / 2) // Offset middle
	};

	// Test all common values against each other
	for (size_t i = 0; i < common_values.size (); ++i)
	{
		for (size_t j = i + 1; j < common_values.size (); ++j)
		{
			if (common_values[i] != common_values[j])
			{
				ASSERT_NE (hash (Type{ common_values[i] }), hash (Type{ common_values[j] }));
			}
			else
			{
				ASSERT_EQ (hash (Type{ common_values[i] }), hash (Type{ common_values[j] }));
			}
		}
	}
}
}

TEST (numbers, hashing)
{
	// Using std::hash
	test_hashing<nano::uint128_union, std::hash> ();
	test_hashing<nano::uint256_union, std::hash> ();
	test_hashing<nano::uint512_union, std::hash> ();
	test_hashing<nano::block_hash, std::hash> ();
	test_hashing<nano::public_key, std::hash> ();
	test_hashing<nano::hash_or_account, std::hash> ();
	test_hashing<nano::link, std::hash> ();
	test_hashing<nano::root, std::hash> ();
	test_hashing<nano::raw_key, std::hash> ();
	test_hashing<nano::wallet_id, std::hash> ();
	test_hashing<nano::qualified_root, std::hash> ();

	// Using boost::hash
	test_hashing<nano::uint128_union, boost::hash> ();
	test_hashing<nano::uint256_union, boost::hash> ();
	test_hashing<nano::uint512_union, boost::hash> ();
	test_hashing<nano::block_hash, boost::hash> ();
	test_hashing<nano::public_key, boost::hash> ();
	test_hashing<nano::hash_or_account, boost::hash> ();
	test_hashing<nano::link, boost::hash> ();
	test_hashing<nano::root, boost::hash> ();
	test_hashing<nano::raw_key, boost::hash> ();
	test_hashing<nano::wallet_id, boost::hash> ();
	test_hashing<nano::qualified_root, boost::hash> ();
}

TEST (uint128_union, decode_dec)
{
	nano::uint128_union value;
	std::string text ("16");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_EQ (16, value.bytes[15]);
}

TEST (uint128_union, decode_dec_negative)
{
	nano::uint128_union value;
	std::string text ("-1");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint128_union, decode_dec_zero)
{
	nano::uint128_union value;
	std::string text ("0");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_TRUE (value.is_zero ());
}

TEST (uint128_union, decode_dec_leading_zero)
{
	nano::uint128_union value;
	std::string text ("010");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint128_union, decode_dec_overflow)
{
	nano::uint128_union value;
	std::string text ("340282366920938463463374607431768211456");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

struct test_punct : std::moneypunct<char>
{
	pattern do_pos_format () const
	{
		return { { value, none, none, none } };
	}
	int do_frac_digits () const
	{
		return 0;
	}
	char_type do_decimal_point () const
	{
		return '+';
	}
	char_type do_thousands_sep () const
	{
		return '-';
	}
	string_type do_grouping () const
	{
		return "\3\4";
	}
};

TEST (uint128_union, balance_format)
{
	ASSERT_EQ ("0", nano::amount (nano::uint128_t ("0")).format_balance (nano::nano_ratio, 0, false));
	ASSERT_EQ ("0", nano::amount (nano::uint128_t ("0")).format_balance (nano::nano_ratio, 2, true));
	ASSERT_EQ ("340,282,366", nano::amount (nano::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")).format_balance (nano::nano_ratio, 0, true));
	ASSERT_EQ ("340,282,366.920938463463374607431768211455", nano::amount (nano::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")).format_balance (nano::nano_ratio, 64, true));
	ASSERT_EQ ("340,282,366,920,938,463,463,374,607,431,768,211,455", nano::amount (nano::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")).format_balance (1, 4, true));
	ASSERT_EQ ("340,282,366", nano::amount (nano::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (nano::nano_ratio, 0, true));
	ASSERT_EQ ("340,282,366.920938463463374607431768211454", nano::amount (nano::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (nano::nano_ratio, 64, true));
	ASSERT_EQ ("340282366920938463463374607431768211454", nano::amount (nano::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (1, 4, false));
	ASSERT_EQ ("170,141,183", nano::amount (nano::uint128_t ("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (nano::nano_ratio, 0, true));
	ASSERT_EQ ("170,141,183.460469231731687303715884105726", nano::amount (nano::uint128_t ("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (nano::nano_ratio, 64, true));
	ASSERT_EQ ("170141183460469231731687303715884105726", nano::amount (nano::uint128_t ("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (1, 4, false));
	ASSERT_EQ ("1", nano::amount (nano::uint128_t ("1000000000000000000000000000000")).format_balance (nano::nano_ratio, 2, true));
	ASSERT_EQ ("1.2", nano::amount (nano::uint128_t ("1200000000000000000000000000000")).format_balance (nano::nano_ratio, 2, true));
	ASSERT_EQ ("1.23", nano::amount (nano::uint128_t ("1230000000000000000000000000000")).format_balance (nano::nano_ratio, 2, true));
	ASSERT_EQ ("1.2", nano::amount (nano::uint128_t ("1230000000000000000000000000000")).format_balance (nano::nano_ratio, 1, true));
	ASSERT_EQ ("1", nano::amount (nano::uint128_t ("1230000000000000000000000000000")).format_balance (nano::nano_ratio, 0, true));
	ASSERT_EQ ("123456789", nano::amount (nano::nano_ratio * 123456789).format_balance (nano::nano_ratio, 2, false));
	ASSERT_EQ ("123,456,789", nano::amount (nano::nano_ratio * 123456789).format_balance (nano::nano_ratio, 2, true));
}

TEST (uint128_union, decode_decimal)
{
	nano::amount amount;
	ASSERT_FALSE (amount.decode_dec ("340282366920938463463374607431768211455", nano::raw_ratio));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max (), amount.number ());
	ASSERT_TRUE (amount.decode_dec ("340282366920938463463374607431768211456", nano::raw_ratio));
	ASSERT_TRUE (amount.decode_dec ("340282366920938463463374607431768211455.1", nano::raw_ratio));
	ASSERT_TRUE (amount.decode_dec ("0.1", nano::raw_ratio));
	ASSERT_FALSE (amount.decode_dec ("1", nano::raw_ratio));
	ASSERT_EQ (1, amount.number ());
	ASSERT_FALSE (amount.decode_dec ("340282366.920938463463374607431768211454", nano::nano_ratio));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - 1, amount.number ());
	ASSERT_TRUE (amount.decode_dec ("340282366.920938463463374607431768211456", nano::nano_ratio));
	ASSERT_TRUE (amount.decode_dec ("340282367", nano::nano_ratio));
	ASSERT_FALSE (amount.decode_dec ("0.000000000000000000000001", nano::nano_ratio));
	ASSERT_EQ (1000000, amount.number ());
	ASSERT_FALSE (amount.decode_dec ("0.000000000000000000000000000001", nano::nano_ratio));
	ASSERT_EQ (1, amount.number ());
	ASSERT_TRUE (amount.decode_dec ("0.0000000000000000000000000000001", nano::nano_ratio));
	ASSERT_TRUE (amount.decode_dec (".1", nano::nano_ratio));
	ASSERT_TRUE (amount.decode_dec ("0.", nano::nano_ratio));
	ASSERT_FALSE (amount.decode_dec ("9.999999999999999999999999999999", nano::nano_ratio));
	ASSERT_EQ (nano::uint128_t ("9999999999999999999999999999999"), amount.number ());
	ASSERT_FALSE (amount.decode_dec ("170141183.460469231731687303715884105727", nano::nano_ratio));
	ASSERT_EQ (nano::uint128_t ("170141183460469231731687303715884105727"), amount.number ());
	ASSERT_FALSE (amount.decode_dec ("1230", nano::Knano_ratio));
	ASSERT_EQ (1230 * nano::Knano_ratio, amount.number ());
}

TEST (uint256_union, key_encryption)
{
	nano::keypair key1;
	nano::raw_key secret_key;
	secret_key.clear ();
	nano::uint256_union encrypted;
	encrypted.encrypt (key1.prv, secret_key, key1.pub.owords[0]);
	nano::raw_key key4;
	key4.decrypt (encrypted, secret_key, key1.pub.owords[0]);
	ASSERT_EQ (key1.prv, key4);
	auto pub (nano::pub_key (key4));
	ASSERT_EQ (key1.pub, pub);
}

TEST (uint256_union, encryption)
{
	nano::raw_key key;
	key.clear ();
	nano::raw_key number1;
	number1 = 1;
	nano::uint256_union encrypted1;
	encrypted1.encrypt (number1, key, key.owords[0]);
	nano::uint256_union encrypted2;
	encrypted2.encrypt (number1, key, key.owords[0]);
	ASSERT_EQ (encrypted1, encrypted2);
	nano::raw_key number2;
	number2.decrypt (encrypted1, key, key.owords[0]);
	ASSERT_EQ (number1, number2);
}

TEST (uint256_union, decode_empty)
{
	std::string text;
	nano::uint256_union val;
	ASSERT_TRUE (val.decode_hex (text));
}

TEST (uint256_union, parse_zero)
{
	nano::uint256_union input (nano::uint256_t (0));
	std::string text;
	input.encode_hex (text);
	nano::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint256_union, parse_zero_short)
{
	std::string text ("0");
	nano::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint256_union, parse_one)
{
	nano::uint256_union input (nano::uint256_t (1));
	std::string text;
	input.encode_hex (text);
	nano::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (1, output.number ());
}

TEST (uint256_union, parse_error_symbol)
{
	nano::uint256_union input (nano::uint256_t (1000));
	std::string text;
	input.encode_hex (text);
	text[5] = '!';
	nano::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, max_hex)
{
	nano::uint256_union input (std::numeric_limits<nano::uint256_t>::max ());
	std::string text;
	input.encode_hex (text);
	nano::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (nano::uint256_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint256_union, decode_dec)
{
	nano::uint256_union value;
	std::string text ("16");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_EQ (16, value.bytes[31]);
}

TEST (uint256_union, max_dec)
{
	nano::uint256_union input (std::numeric_limits<nano::uint256_t>::max ());
	std::string text;
	input.encode_dec (text);
	nano::uint256_union output;
	auto error (output.decode_dec (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (nano::uint256_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint256_union, decode_dec_negative)
{
	nano::uint256_union value;
	std::string text ("-1");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, decode_dec_zero)
{
	nano::uint256_union value;
	std::string text ("0");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_TRUE (value.is_zero ());
}

TEST (uint256_union, decode_dec_leading_zero)
{
	nano::uint256_union value;
	std::string text ("010");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, parse_error_overflow)
{
	nano::uint256_union input (std::numeric_limits<nano::uint256_t>::max ());
	std::string text;
	input.encode_hex (text);
	text.push_back (0);
	nano::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, big_endian_union_constructor)
{
	nano::uint256_t value1 (1);
	nano::uint256_union bytes1 (value1);
	ASSERT_EQ (1, bytes1.bytes[31]);
	nano::uint512_t value2 (1);
	nano::uint512_union bytes2 (value2);
	ASSERT_EQ (1, bytes2.bytes[63]);
}

TEST (uint256_union, big_endian_union_function)
{
	nano::uint256_union bytes1 ("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
	ASSERT_EQ (0xfe, bytes1.bytes[0x00]);
	ASSERT_EQ (0xdc, bytes1.bytes[0x01]);
	ASSERT_EQ (0xba, bytes1.bytes[0x02]);
	ASSERT_EQ (0x98, bytes1.bytes[0x03]);
	ASSERT_EQ (0x76, bytes1.bytes[0x04]);
	ASSERT_EQ (0x54, bytes1.bytes[0x05]);
	ASSERT_EQ (0x32, bytes1.bytes[0x06]);
	ASSERT_EQ (0x10, bytes1.bytes[0x07]);
	ASSERT_EQ (0xfe, bytes1.bytes[0x08]);
	ASSERT_EQ (0xdc, bytes1.bytes[0x09]);
	ASSERT_EQ (0xba, bytes1.bytes[0x0a]);
	ASSERT_EQ (0x98, bytes1.bytes[0x0b]);
	ASSERT_EQ (0x76, bytes1.bytes[0x0c]);
	ASSERT_EQ (0x54, bytes1.bytes[0x0d]);
	ASSERT_EQ (0x32, bytes1.bytes[0x0e]);
	ASSERT_EQ (0x10, bytes1.bytes[0x0f]);
	ASSERT_EQ (0xfe, bytes1.bytes[0x10]);
	ASSERT_EQ (0xdc, bytes1.bytes[0x11]);
	ASSERT_EQ (0xba, bytes1.bytes[0x12]);
	ASSERT_EQ (0x98, bytes1.bytes[0x13]);
	ASSERT_EQ (0x76, bytes1.bytes[0x14]);
	ASSERT_EQ (0x54, bytes1.bytes[0x15]);
	ASSERT_EQ (0x32, bytes1.bytes[0x16]);
	ASSERT_EQ (0x10, bytes1.bytes[0x17]);
	ASSERT_EQ (0xfe, bytes1.bytes[0x18]);
	ASSERT_EQ (0xdc, bytes1.bytes[0x19]);
	ASSERT_EQ (0xba, bytes1.bytes[0x1a]);
	ASSERT_EQ (0x98, bytes1.bytes[0x1b]);
	ASSERT_EQ (0x76, bytes1.bytes[0x1c]);
	ASSERT_EQ (0x54, bytes1.bytes[0x1d]);
	ASSERT_EQ (0x32, bytes1.bytes[0x1e]);
	ASSERT_EQ (0x10, bytes1.bytes[0x1f]);
	ASSERT_EQ ("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210", bytes1.to_string ());
	ASSERT_EQ (nano::uint256_t ("0xFEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210"), bytes1.number ());
	nano::uint512_union bytes2;
	bytes2.clear ();
	bytes2.bytes[63] = 1;
	ASSERT_EQ (nano::uint512_t (1), bytes2.number ());
}

TEST (uint256_union, decode_nano_variant)
{
	nano::account key;
	ASSERT_FALSE (key.decode_account ("xrb_1111111111111111111111111111111111111111111111111111hifc8npp"));
	ASSERT_FALSE (key.decode_account ("nano_1111111111111111111111111111111111111111111111111111hifc8npp"));
}

/**
 * It used to be the case that when the address was wrong only in the checksum part
 * then the decode_account would return error and it would also write the address with
 * fixed checksum into 'key', which is not desirable.
 */
TEST (uint256_union, key_is_not_updated_on_checksum_error)
{
	nano::account key;
	ASSERT_EQ (key, 0);
	bool result = key.decode_account ("nano_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtd1");
	ASSERT_EQ (key, 0);
	ASSERT_TRUE (result);
}

TEST (uint256_union, account_transcode)
{
	nano::account value;
	auto text (nano::dev::genesis_key.pub.to_account ());
	ASSERT_FALSE (value.decode_account (text));
	ASSERT_EQ (nano::dev::genesis_key.pub, value);

	/*
	 * Handle different offsets for the underscore separator
	 * for "xrb_" prefixed and "nano_" prefixed accounts
	 */
	unsigned offset = (text.front () == 'x') ? 3 : 4;
	ASSERT_EQ ('_', text[offset]);
	text[offset] = '-';
	nano::account value2;
	ASSERT_FALSE (value2.decode_account (text));
	ASSERT_EQ (value, value2);
}

TEST (uint256_union, account_encode_lex)
{
	nano::account min ("0000000000000000000000000000000000000000000000000000000000000000");
	nano::account max ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
	auto min_text (min.to_account ());
	auto max_text (max.to_account ());

	/*
	 * Handle different lengths for "xrb_" prefixed and "nano_" prefixed accounts
	 */
	unsigned length = (min_text.front () == 'x') ? 64 : 65;
	ASSERT_EQ (length, min_text.size ());
	ASSERT_EQ (length, max_text.size ());

	auto previous (min_text);
	for (auto i (1); i != 1000; ++i)
	{
		nano::account number (min.number () + i);
		auto text (number.to_account ());
		nano::account output;
		output.decode_account (text);
		ASSERT_EQ (number, output);
		ASSERT_GT (text, previous);
		previous = text;
	}
	for (auto i (1); i != 1000; ++i)
	{
		nano::keypair key;
		auto text (key.pub.to_account ());
		nano::account output;
		output.decode_account (text);
		ASSERT_EQ (key.pub, output);
	}
}

TEST (uint256_union, bounds)
{
	nano::account key;
	std::string bad1 (64, '\x000');
	bad1[0] = 'x';
	bad1[1] = 'r';
	bad1[2] = 'b';
	bad1[3] = '-';
	ASSERT_TRUE (key.decode_account (bad1));
	std::string bad2 (64, '\x0ff');
	bad2[0] = 'x';
	bad2[1] = 'r';
	bad2[2] = 'b';
	bad2[3] = '-';
	ASSERT_TRUE (key.decode_account (bad2));
}

TEST (uint64_t, parse)
{
	uint64_t value0 (1);
	ASSERT_FALSE (nano::from_string_hex ("0", value0));
	ASSERT_EQ (0, value0);
	uint64_t value1 (1);
	ASSERT_FALSE (nano::from_string_hex ("ffffffffffffffff", value1));
	ASSERT_EQ (0xffffffffffffffffULL, value1);
	uint64_t value2 (1);
	ASSERT_TRUE (nano::from_string_hex ("g", value2));
	uint64_t value3 (1);
	ASSERT_TRUE (nano::from_string_hex ("ffffffffffffffff0", value3));
	uint64_t value4 (1);
	ASSERT_TRUE (nano::from_string_hex ("", value4));
}

TEST (uint256_union, hash)
{
	ASSERT_EQ (4, nano::uint256_union{}.qwords.size ());
	std::hash<nano::uint256_union> h{};
	for (size_t i (0), n (nano::uint256_union{}.bytes.size ()); i < n; ++i)
	{
		nano::uint256_union x1{ 0 };
		nano::uint256_union x2{ 0 };
		x2.bytes[i] = 1;
		ASSERT_NE (h (x1), h (x2));
	}
}

TEST (uint512_union, hash)
{
	ASSERT_EQ (2, nano::uint512_union{}.uint256s.size ());
	std::hash<nano::uint512_union> h{};
	for (size_t i (0), n (nano::uint512_union{}.bytes.size ()); i < n; ++i)
	{
		nano::uint512_union x1{ 0 };
		nano::uint512_union x2{ 0 };
		x2.bytes[i] = 1;
		ASSERT_NE (h (x1), h (x2));
	}
	for (auto part (0); part < nano::uint512_union{}.uint256s.size (); ++part)
	{
		for (size_t i (0), n (nano::uint512_union{}.uint256s[part].bytes.size ()); i < n; ++i)
		{
			nano::uint512_union x1{ 0 };
			nano::uint512_union x2{ 0 };
			x2.uint256s[part].bytes[i] = 1;
			ASSERT_NE (h (x1), h (x2));
		}
	}
}