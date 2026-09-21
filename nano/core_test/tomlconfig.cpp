#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <gtest/gtest.h>

#include <sstream>
#include <string>

TEST (tomlconfig, optional_child)
{
	std::stringstream ss;
	ss << R"toml(
		[child]
		val=1
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	auto c1 = t.get_required_child ("child");
	int val = 0;
	c1.get_required ("val", val);
	ASSERT_EQ (val, 1);
	auto c2 = t.get_optional_child ("child2");
	ASSERT_FALSE (c2);
}

/** A missing required child sets an error and yields an empty table instead of aliasing the parent */
TEST (tomlconfig, required_child_missing)
{
	std::stringstream ss;
	ss << R"toml(
		val=1
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	auto child = t.get_required_child ("child");
	ASSERT_TRUE (t.get_error ());
	ASSERT_EQ (t.get_error (), nano::error_config::missing_value);
	ASSERT_TRUE (child.empty ());
	ASSERT_FALSE (child.has_key ("val"));
}

/** A key holding a plain value cannot be opened as a child table */
TEST (tomlconfig, child_not_table)
{
	std::stringstream ss;
	ss << R"toml(
		child=1
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	ASSERT_FALSE (t.get_optional_child ("child"));
	ASSERT_TRUE (t.get_error ());
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	t.get_error ().clear ();

	auto child = t.get_required_child ("child");
	ASSERT_TRUE (t.get_error ());
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	ASSERT_TRUE (child.empty ());
}

/** The first failure a document produces survives a later structural error */
TEST (tomlconfig, first_error_wins)
{
	std::stringstream ss;
	ss << R"toml(
		num = "not a number"
		child = 1
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	uint16_t num = 0;
	t.get<uint16_t> ("num", num);
	std::string const expected = "num is not an integer between 0 and 65535";
	ASSERT_EQ (t.get_error ().get_message (), expected);

	// Both child lookups fail on `child`, neither may replace the error already recorded
	t.get_required_child ("child");
	ASSERT_EQ (t.get_error ().get_message (), expected);
	ASSERT_FALSE (t.get_optional_child ("child"));
	ASSERT_EQ (t.get_error ().get_message (), expected);
	t.get_required_child ("missing");
	ASSERT_EQ (t.get_error ().get_message (), expected);
}

/** Negative numbers are rejected for unsigned targets instead of wrapping around */
TEST (tomlconfig, negative_unsigned)
{
	std::stringstream ss;
	ss << R"toml(
		a = -1
		b = 5
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	uint64_t a{ 7 }, b{ 0 };
	t.get ("a", a);
	t.get ("b", b);
	ASSERT_EQ (a, 7);
	ASSERT_EQ (b, 5);
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	ASSERT_NE (t.get_error ().get_message ().find ("a is not a 64-bit unsigned integer"), std::string::npos) << t.get_error ().get_message ();
}

/** A table or an array where a scalar is expected is rejected and leaves the target alone */
TEST (tomlconfig, non_scalar_value)
{
	std::stringstream ss;
	ss << R"toml(
		arr = ["a", "b"]
		[tbl]
		x = 1
	)toml";

	nano::tomlconfig t;
	ASSERT_FALSE (t.read (ss)) << t.get_error ().get_message ();

	std::size_t from_array{ 42 };
	t.get<std::size_t> ("arr", from_array);
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	ASSERT_EQ (t.get_error ().get_message (), "arr is not a 64-bit unsigned integer");
	ASSERT_EQ (from_array, 42);
	t.get_error ().clear ();

	std::size_t from_table{ 42 };
	t.get<std::size_t> ("tbl", from_table);
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	ASSERT_EQ (t.get_error ().get_message (), "tbl is not a 64-bit unsigned integer");
	ASSERT_EQ (from_table, 42);
	t.get_error ().clear ();

	// An empty string converts successfully, so a string target needs the value to be present
	std::string text{ "original" };
	t.get<std::string> ("arr", text);
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	ASSERT_EQ (t.get_error ().get_message (), "arr is not a string");
	ASSERT_EQ (text, "original");
	t.get_error ().clear ();

	t.get<std::string> ("tbl", text);
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	ASSERT_EQ (text, "original");
}

/** Type descriptions are derived from the type, so aliases such as size_t get a real one */
TEST (tomlconfig, type_description)
{
	std::stringstream ss;
	ss << R"toml(
		a = "not a number"
		b = 70000
		c = "not an address"
	)toml";

	nano::tomlconfig t;
	t.read (ss);

	std::size_t a{ 0 };
	t.get ("a", a);
	ASSERT_EQ (t.get_error ().get_message (), "a is not a 64-bit unsigned integer");
	t.get_error ().clear ();

	uint16_t b{ 0 };
	t.get ("b", b);
	ASSERT_EQ (t.get_error ().get_message (), "b is not an integer between 0 and 65535");
	t.get_error ().clear ();

	boost::asio::ip::address_v6 c;
	t.get_optional<boost::asio::ip::address_v6> ("c", c, boost::asio::ip::address_v6::loopback ());
	ASSERT_EQ (t.get_error ().get_message (), "c is not an IPv6 address such as ::1 or ::ffff:127.0.0.1");
}

TEST (tomlconfig, put)
{
	nano::tomlconfig config;
	nano::tomlconfig config_node;
	// Overwrite value and add to child node
	config_node.put ("port", "7074");
	config_node.put ("port", "7075");
	config.put_child ("node", config_node);
	uint16_t port;
	config.get_required<uint16_t> ("node.port", port);
	ASSERT_EQ (port, 7075);
	ASSERT_FALSE (config.get_error ());
}

TEST (tomlconfig, array)
{
	nano::tomlconfig config;
	nano::tomlconfig config_node;
	config.put_child ("node", config_node);
	config_node.push<std::string> ("items", "item 1");
	config_node.push<std::string> ("items", "item 2");
	int i = 1;
	config_node.array_entries_required<std::string> ("items", [&i] (std::string item) {
		ASSERT_EQ (item, std::string ("item ") + std::to_string (i));
		i++;
	});
}
