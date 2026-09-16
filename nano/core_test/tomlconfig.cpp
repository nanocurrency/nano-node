#include <nano/lib/config.hpp>
#include <nano/lib/config_template.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

/** A failing getter does not stop the ones after it and every failure ends up in the message */
TEST (tomlconfig, errors_accumulate)
{
	std::stringstream ss;
	ss << R"toml(
		a = 70000
		b = "text"
		c = 7
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	uint16_t a{ 0 }, b{ 0 }, c{ 0 };
	t.get<uint16_t> ("a", a);
	t.get<uint16_t> ("b", b);
	t.get<uint16_t> ("c", c);
	ASSERT_EQ (c, 7);
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
	auto message = t.get_error ().get_message ();
	ASSERT_NE (message.find ("a is not"), std::string::npos) << message;
	ASSERT_NE (message.find ("b is not"), std::string::npos) << message;
}

/** Entries no getter asked for are listed; a wholly unread table is listed once, a partially read one by entry */
TEST (tomlconfig, unknown_keys)
{
	std::stringstream ss;
	ss << R"toml(
		read = 1
		unread = 2
		[partial]
		read = 3
		unread = 4
		[partial.child]
		unread = 5
		[whole]
		a = 6
		b = 7
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	int value = 0;
	t.get ("read", value);
	t.get_required_child ("partial").get ("read", value);

	auto unknown = t.unknown_keys ();
	std::sort (unknown.begin (), unknown.end ());
	ASSERT_EQ (unknown, (std::vector<std::string>{ "partial.child", "partial.unread", "unread", "whole" }));

	// Raw access to the table may inspect anything, so everything counts as read from then on
	t.get_tree ();
	ASSERT_TRUE (t.unknown_keys ().empty ());
}

/** has_key alone does not count as reading, dotted getters mark only the leaf */
TEST (tomlconfig, unknown_keys_dotted)
{
	std::stringstream ss;
	ss << R"toml(
		[node]
		a = 1
		b = 2
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	ASSERT_TRUE (t.has_key ("node"));
	ASSERT_EQ (t.unknown_keys (), (std::vector<std::string>{ "node" }));

	int value = 0;
	t.get ("node.a", value);
	ASSERT_EQ (t.unknown_keys (), (std::vector<std::string>{ "node.b" }));
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

/** Unknown keys do not fail the load; they are left for the caller to report */
TEST (config_file, unknown_keys)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	{
		std::ofstream file{ path / "config-test.toml" };
		file << "[node]\nport = 1\n[node.typo]\nenable = true\n";
	}

	nano::tomlconfig toml;
	ASSERT_FALSE (nano::read_config_file (toml, "config-test.toml", path, { "node.other=2" }));
	uint16_t port{ 0 };
	toml.get_required<uint16_t> ("node.port", port);
	ASSERT_EQ (port, 1);

	auto unknown = toml.unknown_keys ();
	std::sort (unknown.begin (), unknown.end ());
	ASSERT_EQ (unknown, (std::vector<std::string>{ "node.other", "node.typo" }));
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

/** Each value is preceded by its documentation and tables are introduced by a header of their own */
TEST (config_template, annotates_values)
{
	nano::tomlconfig doc;
	nano::tomlconfig child;
	child.put ("io_threads", 4u, "Threads the process uses.\ntype:uint32");
	doc.put ("port", 7076u, "Listening port");
	doc.put_child ("process", child);

	auto rendered = nano::render_config_template (doc, /* comment_values */ false);
	ASSERT_NE (rendered.find ("\t# Listening port\n\tport = 7076\n"), std::string::npos) << rendered;
	ASSERT_NE (rendered.find ("\n[process]\n"), std::string::npos) << rendered;
	ASSERT_NE (rendered.find ("\t# Threads the process uses.\n\t# type:uint32\n\tio_threads = 4\n"), std::string::npos) << rendered;
}

/** With every value commented out the template parses into a document holding only empty tables */
TEST (config_template, commented_is_empty)
{
	nano::tomlconfig doc;
	nano::tomlconfig child;
	child.put ("io_threads", 4u);
	doc.put ("port", 7076u, "Listening port");
	doc.put_child ("process", child);

	auto rendered = nano::render_config_template (doc, /* comment_values */ true);
	ASSERT_NE (rendered.find ("\t# port = 7076\n"), std::string::npos) << rendered;

	std::stringstream ss{ rendered };
	nano::tomlconfig parsed;
	ASSERT_FALSE (parsed.read (ss)) << parsed.get_error ().get_message ();
	ASSERT_FALSE (parsed.has_key ("port"));
	ASSERT_TRUE (parsed.get_required_child ("process").empty ());
}

/** An update comments out values that still match the defaults and leaves every other value active */
TEST (config_template, update)
{
	nano::tomlconfig defaults;
	nano::tomlconfig defaults_child;
	defaults_child.put ("threshold", 10u);
	defaults.put ("io_threads", 4u);
	defaults.put_child ("bootstrap", defaults_child);

	nano::tomlconfig current;
	nano::tomlconfig current_child;
	current_child.put ("threshold", 33333u);
	current.put ("io_threads", 4u);
	// The defaults write no peering port at all
	current.put ("peering_port", 7075u);
	current.put_child ("bootstrap", current_child);

	auto updated = nano::render_config_update (current, defaults);
	ASSERT_NE (updated.find ("\t# io_threads = 4\n"), std::string::npos) << updated;
	ASSERT_NE (updated.find ("\tpeering_port = 7075\n"), std::string::npos) << updated;
	ASSERT_NE (updated.find ("\tthreshold = 33333\n"), std::string::npos) << updated;
	ASSERT_EQ (updated.find ("\tio_threads = "), std::string::npos) << updated;
}
