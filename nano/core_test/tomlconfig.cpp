#include <nano/lib/config.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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

/** Overrides with dotted keys land in the addressed table and keep its other keys */
TEST (tomlconfig, override_dotted_keys)
{
	std::stringstream ss;
	ss << R"toml(
		[node]
		b=5
		c=3
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	ASSERT_FALSE (t.apply_overrides ({ "node.a = 1", "node.b = 2" }));

	auto node = t.get_required_child ("node");
	uint16_t a, b, c;
	node.get<uint16_t> ("a", a);
	ASSERT_EQ (a, 1);
	node.get<uint16_t> ("b", b);
	ASSERT_EQ (b, 2);
	node.get<uint16_t> ("c", c);
	ASSERT_EQ (c, 3);
}

/** An override replaces the file's value and later overrides win over earlier ones */
TEST (tomlconfig, override_precedence)
{
	std::stringstream ss;
	ss << R"toml(
		node.peering_port=7075
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	ASSERT_FALSE (t.apply_overrides ({ "node.peering_port=8075", "node.peering_port=9075" }));

	uint16_t port = 0;
	t.get_optional<uint16_t> ("node.peering_port", port);
	ASSERT_EQ (port, 9075);
	ASSERT_FALSE (t.get_error ());
}

/** Overrides create missing tables and arrays replace the whole array */
TEST (tomlconfig, override_creates_tables)
{
	nano::tomlconfig t;
	std::stringstream ss;
	ss << R"toml(
		[node]
		items = ["old"]
	)toml";
	t.read (ss);
	ASSERT_FALSE (t.apply_overrides ({ "node.child.value=1", "node.items=[\"a\",\"b\"]" }));

	uint16_t value = 0;
	t.get_required<uint16_t> ("node.child.value", value);
	ASSERT_EQ (value, 1);

	std::vector<std::string> items;
	t.get_required_child ("node").array_entries_required<std::string> ("items", [&items] (std::string item) {
		items.push_back (item);
	});
	ASSERT_EQ (items, (std::vector<std::string>{ "a", "b" }));
}

/** A malformed override is reported with the offending entry and leaves the document untouched */
TEST (tomlconfig, override_invalid)
{
	std::stringstream ss;
	ss << R"toml(
		[node]
		a=1
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	ASSERT_TRUE (t.apply_overrides ({ "node.b=2", "node.foo" }));
	ASSERT_NE (t.get_error ().get_message ().find ("node.foo"), std::string::npos);

	// The valid override before the malformed one was applied, the document itself is intact
	t.get_error ().clear ();
	uint16_t a = 0, b = 0;
	t.get_required<uint16_t> ("node.a", a);
	t.get_required<uint16_t> ("node.b", b);
	ASSERT_EQ (a, 1);
	ASSERT_EQ (b, 2);
}

/** Type errors on overridden values are reported like any other value */
TEST (tomlconfig, override_type_error)
{
	nano::tomlconfig t;
	ASSERT_FALSE (t.apply_overrides ({ "node.too_big=70000" }));

	uint16_t port = 65535;
	t.get_optional<uint16_t> ("node.missing", port);
	ASSERT_EQ (port, 65535);
	ASSERT_FALSE (t.get_error ());

	t.get_required<uint16_t> ("node.missing", port);
	ASSERT_EQ (t.get_error (), nano::error_config::missing_value);
	t.get_error ().clear ();

	t.get_required<uint16_t> ("node.too_big", port);
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

/** Reading a path that does not exist reports an error and does not create the file */
TEST (tomlconfig, read_missing_file)
{
	auto path = nano::unique_path () / "missing.toml";
	nano::tomlconfig toml;
	ASSERT_TRUE (toml.read (path));
	ASSERT_FALSE (std::filesystem::exists (path));
}

/** Writing creates the file and replaces any previous content */
TEST (tomlconfig, write_replaces_content)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	auto file = path / "config.toml";

	nano::tomlconfig longer;
	longer.put ("a_long_key_name", "a long value that takes up space");
	longer.put ("another_key", "more");
	longer.write (file);

	nano::tomlconfig shorter;
	shorter.put ("a", "b");
	shorter.write (file);

	nano::tomlconfig read;
	ASSERT_FALSE (read.read (file));
	ASSERT_TRUE (read.has_key ("a"));
	ASSERT_FALSE (read.has_key ("another_key"));
}

/** A missing config file yields an empty document and is not created */
TEST (config_file, missing_file)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);

	nano::tomlconfig toml;
	ASSERT_FALSE (nano::read_config_file (toml, "config-test.toml", path));
	ASSERT_TRUE (toml.empty ());
	ASSERT_FALSE (std::filesystem::exists (path / "config-test.toml"));
}

/** Overrides apply even when there is no config file */
TEST (config_file, overrides_without_file)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);

	nano::tomlconfig toml;
	ASSERT_FALSE (nano::read_config_file (toml, "config-test.toml", path, { "node.port=7075" }));
	uint16_t port{ 0 };
	toml.get_required<uint16_t> ("node.port", port);
	ASSERT_EQ (port, 7075);
}

/** Overrides take precedence over the file and untouched keys keep the file's values */
TEST (config_file, overrides_over_file)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	{
		std::ofstream file{ path / "config-test.toml" };
		file << "[node]\nport = 1\nthreads = 2\n";
	}

	nano::tomlconfig toml;
	ASSERT_FALSE (nano::read_config_file (toml, "config-test.toml", path, { "node.port=3" }));
	uint16_t port{ 0 }, threads{ 0 };
	toml.get_required<uint16_t> ("node.port", port);
	toml.get_required<uint16_t> ("node.threads", threads);
	ASSERT_EQ (port, 3);
	ASSERT_EQ (threads, 2);
}

/** A file with invalid syntax is reported with its own line number, regardless of any overrides */
TEST (config_file, invalid_file)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	{
		std::ofstream file{ path / "config-test.toml" };
		file << "[node]\nport = 1\nthreads = \n";
	}

	nano::tomlconfig toml;
	auto error = nano::read_config_file (toml, "config-test.toml", path, { "node.a=1", "node.b=2" });
	ASSERT_TRUE (error);
	ASSERT_NE (error.get_message ().find ("line 3"), std::string::npos) << error.get_message ();
}
