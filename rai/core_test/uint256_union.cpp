#include <ed25519-donna/ed25519.h>

#include <gtest/gtest.h>

#include <rai/secure.hpp>

TEST (uint128_union, decode_dec)
{
    rai::uint128_union value;
    std::string text ("16");
    ASSERT_FALSE (value.decode_dec (text));
    ASSERT_EQ (16, value.bytes [15]);
}

TEST (unions, identity)
{
	ASSERT_EQ (1, rai::uint128_union (1).number ().convert_to <uint8_t> ());
	ASSERT_EQ (1, rai::uint256_union (1).number ().convert_to <uint8_t> ());
	ASSERT_EQ (1, rai::uint512_union (1).number ().convert_to <uint8_t> ());
}

TEST (uint256_union, key_encryption)
{
    rai::keypair key1;
    rai::raw_key secret_key;
    secret_key.data.bytes.fill (0);
    rai::uint256_union encrypted;
	encrypted.encrypt (key1.prv, secret_key, key1.pub.owords [0]);
    rai::raw_key key4;
	key4.decrypt (encrypted, secret_key, key1.pub.owords [0]);
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
	encrypted1.encrypt (number1, key, key.data.owords [0]);
    rai::uint256_union encrypted2;
	encrypted2.encrypt (number1, key, key.data.owords [0]);
    ASSERT_EQ (encrypted1, encrypted2);
    rai::raw_key number2;
	number2.decrypt (encrypted1, key, key.data.owords [0]);
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
    text [5] = '!';
    rai::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (uint256_union, max_hex)
{
    rai::uint256_union input (std::numeric_limits <rai::uint256_t>::max ());
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
    ASSERT_EQ (16, value.bytes [31]);
}

TEST (uint256_union, max_dec)
{
    rai::uint256_union input (std::numeric_limits <rai::uint256_t>::max ());
    std::string text;
    input.encode_dec (text);
    rai::uint256_union output;
    auto error (output.decode_dec (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_EQ (rai::uint256_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint256_union, parse_error_overflow)
{
    rai::uint256_union input (std::numeric_limits <rai::uint256_t>::max ());
    std::string text;
    input.encode_hex (text);
    text.push_back (0);
    rai::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (uint256_union, big_endian_union_constructor)
{
	boost::multiprecision::uint256_t value1 (1);
	rai::uint256_union bytes1 (value1);
	ASSERT_EQ (1, bytes1.bytes [31]);
	boost::multiprecision::uint512_t value2 (1);
	rai::uint512_union bytes2 (value2);
	ASSERT_EQ (1, bytes2.bytes [63]);
}

TEST (uint256_union, big_endian_union_function)
{
	rai::uint256_union bytes1 ("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
	ASSERT_EQ (0xfe, bytes1.bytes [0x00]);
	ASSERT_EQ (0xdc, bytes1.bytes [0x01]);
	ASSERT_EQ (0xba, bytes1.bytes [0x02]);
	ASSERT_EQ (0x98, bytes1.bytes [0x03]);
	ASSERT_EQ (0x76, bytes1.bytes [0x04]);
	ASSERT_EQ (0x54, bytes1.bytes [0x05]);
	ASSERT_EQ (0x32, bytes1.bytes [0x06]);
	ASSERT_EQ (0x10, bytes1.bytes [0x07]);
	ASSERT_EQ (0xfe, bytes1.bytes [0x08]);
	ASSERT_EQ (0xdc, bytes1.bytes [0x09]);
	ASSERT_EQ (0xba, bytes1.bytes [0x0a]);
	ASSERT_EQ (0x98, bytes1.bytes [0x0b]);
	ASSERT_EQ (0x76, bytes1.bytes [0x0c]);
	ASSERT_EQ (0x54, bytes1.bytes [0x0d]);
	ASSERT_EQ (0x32, bytes1.bytes [0x0e]);
	ASSERT_EQ (0x10, bytes1.bytes [0x0f]);
	ASSERT_EQ (0xfe, bytes1.bytes [0x10]);
	ASSERT_EQ (0xdc, bytes1.bytes [0x11]);
	ASSERT_EQ (0xba, bytes1.bytes [0x12]);
	ASSERT_EQ (0x98, bytes1.bytes [0x13]);
	ASSERT_EQ (0x76, bytes1.bytes [0x14]);
	ASSERT_EQ (0x54, bytes1.bytes [0x15]);
	ASSERT_EQ (0x32, bytes1.bytes [0x16]);
	ASSERT_EQ (0x10, bytes1.bytes [0x17]);
	ASSERT_EQ (0xfe, bytes1.bytes [0x18]);
	ASSERT_EQ (0xdc, bytes1.bytes [0x19]);
	ASSERT_EQ (0xba, bytes1.bytes [0x1a]);
	ASSERT_EQ (0x98, bytes1.bytes [0x1b]);
	ASSERT_EQ (0x76, bytes1.bytes [0x1c]);
	ASSERT_EQ (0x54, bytes1.bytes [0x1d]);
	ASSERT_EQ (0x32, bytes1.bytes [0x1e]);
	ASSERT_EQ (0x10, bytes1.bytes [0x1f]);
	ASSERT_EQ ("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210", bytes1.to_string ());
	ASSERT_EQ (rai::uint256_t ("0xFEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210"), bytes1.number ());
	rai::uint512_union bytes2;
	bytes2.clear ();
	bytes2.bytes [63] = 1;
	ASSERT_EQ (rai::uint512_t (1), bytes2.number ());
}

TEST (uint256_union, transcode_test_key_base58check)
{
    rai::uint256_union value;
    ASSERT_FALSE (value.decode_base58check (rai::test_genesis_key.pub.to_base58check ()));
    ASSERT_EQ (rai::test_genesis_key.pub, value);
}

class json_upgrade_test
{
public:
	bool deserialize_json (bool & upgraded, boost::property_tree::ptree & tree_a)
	{
		auto error (false);
		if (!tree_a.empty ())
		{
			auto text_l (tree_a.get <std::string> ("thing"));
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
	std::string string1 ("{ \"thing\": \"junktest\" }");
	std::stringstream stream1 (string1);
	json_upgrade_test object1;
	auto error1 (rai::fetch_object (object1, stream1));
	ASSERT_FALSE (error1);
	ASSERT_EQ ("changed", object1.text);
	boost::property_tree::ptree tree1;
	stream1.seekg (0);
	boost::property_tree::read_json (stream1, tree1);
	ASSERT_EQ ("changed", tree1.get <std::string> ("thing"));
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
	std::string string4 ("");
	std::stringstream stream4 (string4);
	json_upgrade_test object4;
	auto error4 (rai::fetch_object (object4, stream4));
	ASSERT_FALSE (error4);
	ASSERT_EQ ("created", object4.text);
	boost::property_tree::ptree tree2;
	stream4.seekg (0);
	boost::property_tree::read_json (stream4, tree2);
	ASSERT_EQ ("created", tree2.get <std::string> ("thing"));
}