#include <nano/lib/tomlconfig.hpp>

#include <gtest/gtest.h>

#include <sstream>
#include <string>

/** Ensure only different values survive a toml diff */
TEST (tomlconfig, diff)
{
	nano::tomlconfig defaults, other;

	// Defaults
	std::stringstream ss;
	ss << R"toml(
	a = false
	b = false
	)toml";

	defaults.read (ss);

	// User file. The rpc section is the same and doesn't need to be emitted
	std::stringstream ss_override;
	ss_override << R"toml(
	a = true
	b = false
	)toml";

	other.read (ss_override);
	other.erase_default_values (defaults);

	ASSERT_TRUE (other.has_key ("a"));
	ASSERT_FALSE (other.has_key ("b"));
}

/** Diff on equal toml files leads to an empty result */
TEST (tomlconfig, diff_equal)
{
	nano::tomlconfig defaults, other;

	std::stringstream ss;
	ss << R"toml(
	[node]
	allow_local_peers = false
	)toml";

	defaults.read (ss);

	std::stringstream ss_override;
	ss_override << R"toml(
	[node]
	allow_local_peers = false
	)toml";

	other.read (ss_override);
	other.erase_default_values (defaults);
	ASSERT_TRUE (other.empty ());
}

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

/** Config settings passed via CLI overrides the config file settings. This is solved
using an override stream. */
TEST (tomlconfig, dot_child_syntax)
{
	std::stringstream ss_override;
	ss_override << R"toml(
		node.a = 1
		node.b = 2
	)toml";

	std::stringstream ss;
	ss << R"toml(
		[node]
		b=5
		c=3
	)toml";

	nano::tomlconfig t;
	t.read (ss_override, ss);

	auto node = t.get_required_child ("node");
	uint16_t a, b, c;
	node.get<uint16_t> ("a", a);
	ASSERT_EQ (a, 1);
	node.get<uint16_t> ("b", b);
	ASSERT_EQ (b, 2);
	node.get<uint16_t> ("c", c);
	ASSERT_EQ (c, 3);
}

TEST (tomlconfig, base_override)
{
	std::stringstream ss_base;
	ss_base << R"toml(
	        node.peering_port=7075
	)toml";

	std::stringstream ss_override;
	ss_override << R"toml(
	        node.peering_port=8075
			node.too_big=70000
	)toml";

	nano::tomlconfig t;
	t.read (ss_override, ss_base);

	// Query optional existent value
	uint16_t port = 0;
	t.get_optional<uint16_t> ("node.peering_port", port);
	ASSERT_EQ (port, 8075);
	ASSERT_FALSE (t.get_error ());

	// Query optional non-existent value, make sure we get default and no errors
	port = 65535;
	t.get_optional<uint16_t> ("node.peering_port_non_existent", port);
	ASSERT_EQ (port, 65535);
	ASSERT_FALSE (t.get_error ());
	t.get_error ().clear ();

	// Query required non-existent value, make sure it errors
	t.get_required<uint16_t> ("node.peering_port_not_existent", port);
	ASSERT_EQ (port, 65535);
	ASSERT_TRUE (t.get_error ());
	ASSERT_EQ (t.get_error (), nano::error_config::missing_value);
	t.get_error ().clear ();

	// Query uint16 that's too big, make sure we have an error
	t.get_required<uint16_t> ("node.too_big", port);
	ASSERT_TRUE (t.get_error ());
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
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
