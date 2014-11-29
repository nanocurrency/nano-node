#include <gtest/gtest.h>

#include <rai/secure.hpp>

TEST (uint128_union, decode_dec)
{
    rai::uint128_union value;
    std::string text ("16");
    ASSERT_FALSE (value.decode_dec (text));
    ASSERT_EQ (16, value.qwords [0]);
}

TEST (uint256_union, key_encryption)
{
    rai::keypair key1;
    rai::uint256_union secret_key;
    secret_key.bytes.fill (0);
    rai::uint256_union encrypted (key1.prv, secret_key, key1.pub.owords [0]);
    rai::private_key key4 (encrypted.prv (secret_key, key1.pub.owords [0]));
    ASSERT_EQ (key1.prv, key4);
    rai::public_key pub;
    ed25519_publickey (key4.bytes.data (), pub.bytes.data ());
    ASSERT_EQ (key1.pub, pub);
}

TEST (uint256_union, encryption)
{
    rai::uint256_union key;
    rai::uint256_union number1 (1);
    rai::uint256_union encrypted1 (number1, key, key.owords [0]);
    rai::uint256_union encrypted2 (number1, key, key.owords [0]);
    ASSERT_EQ (encrypted1, encrypted2);
    auto number2 (encrypted1.prv (key, key.owords [0]));
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
    ASSERT_EQ (16, value.qwords [0]);
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
	ASSERT_EQ (1, bytes1.bytes [0]);
	boost::multiprecision::uint512_t value2 (1);
	rai::uint512_union bytes2 (value2);
	ASSERT_EQ (1, bytes2.bytes [0]);
}

TEST (uint256_union, big_endian_union_function)
{
	rai::uint256_union bytes1;
	bytes1.clear ();
	bytes1.bytes [0] = 1;
	ASSERT_EQ (rai::uint256_t (1), bytes1.number ());
	rai::uint512_union bytes2;
	bytes2.clear ();
	bytes2.bytes [0] = 1;
	ASSERT_EQ (rai::uint512_t (1), bytes2.number ());
}

TEST (uint256_union, transcode_test_key_base58check)
{
    std::string string;
    rai::test_genesis_key.pub.encode_base58check (string);
    rai::uint256_union value;
    ASSERT_FALSE (value.decode_base58check (string));
    ASSERT_EQ (rai::test_genesis_key.pub, value);
}