#include <gtest/gtest.h>

#include <banano/common.hpp>
#include <banano/lib/interface.h>

#include <ed25519-donna/ed25519.h>

TEST (uint128_union, decode_dec)
{
	rai::uint128_union value;
	std::string text ("16");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_EQ (16, value.bytes[15]);
}

TEST (uint128_union, decode_dec_negative)
{
	rai::uint128_union value;
	std::string text ("-1");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint128_union, decode_dec_zero)
{
	rai::uint128_union value;
	std::string text ("0");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_TRUE (value.is_zero ());
}

TEST (uint128_union, decode_dec_leading_zero)
{
	rai::uint128_union value;
	std::string text ("010");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint128_union, decode_dec_overflow)
{
	rai::uint128_union value;
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
	ASSERT_EQ ("0", rai::amount (rai::uint128_t ("0")).format_balance (rai::BAN_ratio, 0, false));
	ASSERT_EQ ("0", rai::amount (rai::uint128_t ("0")).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("340,282,366", rai::amount (rai::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")).format_balance (rai::BAN_ratio, 0, true));
	ASSERT_EQ ("340,282,366.920938463463374607431768211455", rai::amount (rai::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")).format_balance (rai::BAN_ratio, 64, true));
	ASSERT_EQ ("340,282,366,920,938,463,463,374,607,431,768,211,455", rai::amount (rai::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")).format_balance (1, 4, true));
	ASSERT_EQ ("340,282,366", rai::amount (rai::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (rai::BAN_ratio, 0, true));
	ASSERT_EQ ("340,282,366.920938463463374607431768211454", rai::amount (rai::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (rai::BAN_ratio, 64, true));
	ASSERT_EQ ("340282366920938463463374607431768211454", rai::amount (rai::uint128_t ("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (1, 4, false));
	ASSERT_EQ ("170,141,183", rai::amount (rai::uint128_t ("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (rai::BAN_ratio, 0, true));
	ASSERT_EQ ("170,141,183.460469231731687303715884105726", rai::amount (rai::uint128_t ("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (rai::BAN_ratio, 64, true));
	ASSERT_EQ ("170141183460469231731687303715884105726", rai::amount (rai::uint128_t ("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE")).format_balance (1, 4, false));
	ASSERT_EQ ("1", rai::amount (rai::uint128_t ("1000000000000000000000000000000")).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("1.2", rai::amount (rai::uint128_t ("1200000000000000000000000000000")).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("1.23", rai::amount (rai::uint128_t ("1230000000000000000000000000000")).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("1.2", rai::amount (rai::uint128_t ("1230000000000000000000000000000")).format_balance (rai::BAN_ratio, 1, true));
	ASSERT_EQ ("1", rai::amount (rai::uint128_t ("1230000000000000000000000000000")).format_balance (rai::BAN_ratio, 0, true));
	ASSERT_EQ ("< 0.01", rai::amount (rai::RAW_ratio * 10).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("< 0.1", rai::amount (rai::RAW_ratio * 10).format_balance (rai::BAN_ratio, 1, true));
	ASSERT_EQ ("< 1", rai::amount (rai::RAW_ratio * 10).format_balance (rai::BAN_ratio, 0, true));
	ASSERT_EQ ("< 0.01", rai::amount (rai::RAW_ratio * 9999).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("0.01", rai::amount (rai::RAW_ratio * 10000).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("123456789", rai::amount (rai::BAN_ratio * 123456789).format_balance (rai::BAN_ratio, 2, false));
	ASSERT_EQ ("123,456,789", rai::amount (rai::BAN_ratio * 123456789).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("123,456,789.12", rai::amount (rai::BAN_ratio * 123456789 + rai::banoshi_ratio * 123).format_balance (rai::BAN_ratio, 2, true));
	ASSERT_EQ ("12-3456-789+123", rai::amount (rai::BAN_ratio * 123456789 + rai::banoshi_ratio * 123).format_balance (rai::BAN_ratio, 4, true, std::locale (std::cout.getloc (), new test_punct)));
}

TEST (unions, identity)
{
	ASSERT_EQ (1, rai::uint128_union (1).number ().convert_to<uint8_t> ());
	ASSERT_EQ (1, rai::uint256_union (1).number ().convert_to<uint8_t> ());
	ASSERT_EQ (1, rai::uint512_union (1).number ().convert_to<uint8_t> ());
}

TEST (uint256_union, key_encryption)
{
	rai::keypair key1;
	rai::raw_key secret_key;
	secret_key.data.bytes.fill (0);
	rai::uint256_union encrypted;
	encrypted.encrypt (key1.prv, secret_key, key1.pub.owords[0]);
	rai::raw_key key4;
	key4.decrypt (encrypted, secret_key, key1.pub.owords[0]);
	ASSERT_EQ (key1.prv, key4);
	rai::public_key pub;
	ed25519_publickey (key4.data.bytes.data (), pub.bytes.data ());
	ASSERT_EQ (key1.pub, pub);
}

TEST (uint256_union, encryption)
{
	rai::raw_key key;
	key.data.clear ();
	rai::raw_key number1;
	number1.data = 1;
	rai::uint256_union encrypted1;
	encrypted1.encrypt (number1, key, key.data.owords[0]);
	rai::uint256_union encrypted2;
	encrypted2.encrypt (number1, key, key.data.owords[0]);
	ASSERT_EQ (encrypted1, encrypted2);
	rai::raw_key number2;
	number2.decrypt (encrypted1, key, key.data.owords[0]);
	ASSERT_EQ (number1, number2);
}

TEST (uint256_union, decode_empty)
{
	std::string text;
	rai::uint256_union val;
	ASSERT_TRUE (val.decode_hex (text));
}

TEST (uint256_union, parse_zero)
{
	rai::uint256_union input (rai::uint256_t (0));
	std::string text;
	input.encode_hex (text);
	rai::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint256_union, parse_zero_short)
{
	std::string text ("0");
	rai::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint256_union, parse_one)
{
	rai::uint256_union input (rai::uint256_t (1));
	std::string text;
	input.encode_hex (text);
	rai::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (1, output.number ());
}

TEST (uint256_union, parse_error_symbol)
{
	rai::uint256_union input (rai::uint256_t (1000));
	std::string text;
	input.encode_hex (text);
	text[5] = '!';
	rai::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, max_hex)
{
	rai::uint256_union input (std::numeric_limits<rai::uint256_t>::max ());
	std::string text;
	input.encode_hex (text);
	rai::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (rai::uint256_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint256_union, decode_dec)
{
	rai::uint256_union value;
	std::string text ("16");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_EQ (16, value.bytes[31]);
}

TEST (uint256_union, max_dec)
{
	rai::uint256_union input (std::numeric_limits<rai::uint256_t>::max ());
	std::string text;
	input.encode_dec (text);
	rai::uint256_union output;
	auto error (output.decode_dec (text));
	ASSERT_FALSE (error);
	ASSERT_EQ (input, output);
	ASSERT_EQ (rai::uint256_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint256_union, decode_dec_negative)
{
	rai::uint256_union value;
	std::string text ("-1");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, decode_dec_zero)
{
	rai::uint256_union value;
	std::string text ("0");
	ASSERT_FALSE (value.decode_dec (text));
	ASSERT_TRUE (value.is_zero ());
}

TEST (uint256_union, decode_dec_leading_zero)
{
	rai::uint256_union value;
	std::string text ("010");
	auto error (value.decode_dec (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, parse_error_overflow)
{
	rai::uint256_union input (std::numeric_limits<rai::uint256_t>::max ());
	std::string text;
	input.encode_hex (text);
	text.push_back (0);
	rai::uint256_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (uint256_union, big_endian_union_constructor)
{
	rai::uint256_t value1 (1);
	rai::uint256_union bytes1 (value1);
	ASSERT_EQ (1, bytes1.bytes[31]);
	rai::uint512_t value2 (1);
	rai::uint512_union bytes2 (value2);
	ASSERT_EQ (1, bytes2.bytes[63]);
}

TEST (uint256_union, big_endian_union_function)
{
	rai::uint256_union bytes1 ("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
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
	ASSERT_EQ (rai::uint256_t ("0xFEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210"), bytes1.number ());
	rai::uint512_union bytes2;
	bytes2.clear ();
	bytes2.bytes[63] = 1;
	ASSERT_EQ (rai::uint512_t (1), bytes2.number ());
}

TEST (uint256_union, decode_account_v1)
{
	rai::uint256_union key;
	ASSERT_FALSE (key.decode_account ("TR6ZJ4pdp6HC76xMRpVDny5x2s8AEbrhFue3NKVxYYdmKuTEib"));
	ASSERT_EQ (rai::banano_test_account, key);
}

TEST (uint256_union, decode_account_variations)
{
	for (int i = 0; i < 100; i++)
	{
		rai::raw_key key;
		ban_generate_random (key.data.bytes.data ());
		rai::uint256_union pub;
		ban_key_account (key.data.bytes.data (), pub.bytes.data ());

		char account[65] = { 0 };
		ban_uint256_to_address (pub.bytes.data (), account);

		// Replace first digit after ban_ with '0'..'9', make sure only one of them is valid
		int errors = 0;
		for (int variation = 0; variation < 10; variation++)
		{
			account[4] = static_cast<char> (variation + 48);
			errors += ban_valid_address (account);
		}

		ASSERT_EQ (errors, 9);
	}
}

TEST (uint256_union, account_transcode)
{
	rai::uint256_union value;
	auto text (rai::test_genesis_key.pub.to_account ());
	ASSERT_FALSE (value.decode_account (text));
	ASSERT_EQ (rai::test_genesis_key.pub, value);
	ASSERT_EQ ('_', text[3]);
	text[3] = '-';
	rai::uint256_union value2;
	ASSERT_FALSE (value2.decode_account (text));
	ASSERT_EQ (value, value2);
}

TEST (uint256_union, account_encode_lex)
{
	rai::uint256_union min ("0000000000000000000000000000000000000000000000000000000000000000");
	rai::uint256_union max ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
	auto min_text (min.to_account ());
	ASSERT_EQ (64, min_text.size ());
	auto max_text (max.to_account ());
	ASSERT_EQ (64, max_text.size ());
	auto previous (min_text);
	for (auto i (1); i != 1000; ++i)
	{
		rai::uint256_union number (min.number () + i);
		auto text (number.to_account ());
		rai::uint256_union output;
		output.decode_account (text);
		ASSERT_EQ (number, output);
		ASSERT_GT (text, previous);
		previous = text;
	}
	for (auto i (1); i != 1000; ++i)
	{
		rai::keypair key;
		auto text (key.pub.to_account ());
		rai::uint256_union output;
		output.decode_account (text);
		ASSERT_EQ (key.pub, output);
	}
}

TEST (uint256_union, bounds)
{
	rai::uint256_union key;
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

class json_upgrade_test
{
public:
	bool deserialize_json (bool & upgraded, boost::property_tree::ptree & tree_a)
	{
		auto error (false);
		if (!tree_a.empty ())
		{
			auto text_l (tree_a.get<std::string> ("thing"));
			if (text_l == "junktest")
			{
				upgraded = true;
				text_l = "changed";
				tree_a.put ("thing", text_l);
			}
			if (text_l == "error")
			{
				error = true;
			}
			text = text_l;
		}
		else
		{
			upgraded = true;
			text = "created";
			tree_a.put ("thing", text);
		}
		return error;
	}
	std::string text;
};

TEST (json, fetch_object)
{
	auto path1 (rai::unique_path ());
	std::fstream stream1;
	rai::open_or_create (stream1, path1.string ());
	stream1 << "{ \"thing\": \"junktest\" }";
	stream1.close ();
	rai::open_or_create (stream1, path1.string ());
	json_upgrade_test object1;
	auto error1 (rai::fetch_object (object1, path1, stream1));
	ASSERT_FALSE (error1);
	ASSERT_EQ ("changed", object1.text);
	boost::property_tree::ptree tree1;
	stream1.close ();
	rai::open_or_create (stream1, path1.string ());
	boost::property_tree::read_json (stream1, tree1);
	ASSERT_EQ ("changed", tree1.get<std::string> ("thing"));
	std::string string2 ("{ \"thing\": \"junktest2\" }");
	std::stringstream stream2 (string2);
	json_upgrade_test object2;
	auto error2 (rai::fetch_object (object2, stream2));
	ASSERT_FALSE (error2);
	ASSERT_EQ ("junktest2", object2.text);
	ASSERT_EQ ("{ \"thing\": \"junktest2\" }", string2);
	std::string string3 ("{ \"thing\": \"error\" }");
	std::stringstream stream3 (string3);
	json_upgrade_test object3;
	auto error3 (rai::fetch_object (object3, stream3));
	ASSERT_TRUE (error3);
	auto path2 (rai::unique_path ());
	std::fstream stream4;
	rai::open_or_create (stream4, path2.string ());
	json_upgrade_test object4;
	auto error4 (rai::fetch_object (object4, path2, stream4));
	ASSERT_FALSE (error4);
	ASSERT_EQ ("created", object4.text);
	boost::property_tree::ptree tree2;
	stream4.close ();
	rai::open_or_create (stream4, path2.string ());
	boost::property_tree::read_json (stream4, tree2);
	ASSERT_EQ ("created", tree2.get<std::string> ("thing"));
}

TEST (json, DISABLED_fetch_write_fail)
{
	std::string string4 ("");
	std::stringstream stream4 (string4, std::ios_base::in);
	json_upgrade_test object4;
	auto error4 (rai::fetch_object (object4, stream4));
	ASSERT_TRUE (error4);
}

TEST (uint64_t, parse)
{
	uint64_t value0 (1);
	ASSERT_FALSE (rai::from_string_hex ("0", value0));
	ASSERT_EQ (0, value0);
	uint64_t value1 (1);
	ASSERT_FALSE (rai::from_string_hex ("ffffffffffffffff", value1));
	ASSERT_EQ (0xffffffffffffffffULL, value1);
	uint64_t value2 (1);
	ASSERT_TRUE (rai::from_string_hex ("g", value2));
	uint64_t value3 (1);
	ASSERT_TRUE (rai::from_string_hex ("ffffffffffffffff0", value3));
	uint64_t value4 (1);
	ASSERT_TRUE (rai::from_string_hex ("", value4));
}
