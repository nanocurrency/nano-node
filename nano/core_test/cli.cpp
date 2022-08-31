#include <nano/lib/cli.hpp>
#include <nano/node/cli.hpp>
#include <nano/secure/utility.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <regex>

using namespace std::chrono_literals;

namespace
{
std::string call_cli_command (boost::program_options::variables_map const & vm);
}

TEST (cli, key_create)
{
	boost::program_options::variables_map vm;
	vm.emplace ("key_create", boost::program_options::variable_value ());
	auto output = call_cli_command (vm);

	// Extract the private, public and account values. The regular expression extracts anything between the semi-colon and new line.
	std::regex regexpr (": (\\w+)");
	std::smatch matches;
	std::vector<std::string> vals;
	std::string::const_iterator search_start (output.cbegin ());
	while (std::regex_search (search_start, output.cend (), matches, regexpr))
	{
		ASSERT_NE (matches[1].str (), "");
		vals.push_back (matches[1].str ());
		search_start = matches.suffix ().first;
	}

	// Get the contents of the private key and check that the public key and account are successfully derived from the private key
	auto private_key_str = vals.front ();
	nano::raw_key private_key;
	private_key.decode_hex (private_key_str);

	auto public_key = nano::pub_key (private_key);
	ASSERT_EQ (vals[1], public_key.to_string ());
	ASSERT_EQ (vals[2], public_key.to_account ());
}

TEST (cli, config_override_parsing)
{
	std::vector<nano::config_key_value_pair> key_value_pairs;
	auto config_overrides = nano::config_overrides (key_value_pairs);
	ASSERT_TRUE (config_overrides.empty ());
	key_value_pairs.push_back ({ "key", "value" });
	config_overrides = nano::config_overrides (key_value_pairs);
	ASSERT_EQ (config_overrides[0], "key=\"value\"");
	key_value_pairs.push_back ({ "node.online_weight_minimum", "40000000000000000000000000000000000000" });
	config_overrides = nano::config_overrides (key_value_pairs);
	ASSERT_EQ (config_overrides[1], "node.online_weight_minimum=\"40000000000000000000000000000000000000\"");

	// Should add this as it contains escaped quotes, and make sure these are not escaped again
	key_value_pairs.push_back ({ "key", "\"value\"" });
	config_overrides = nano::config_overrides (key_value_pairs);
	ASSERT_EQ (config_overrides[2], "key=\"value\"");
	ASSERT_EQ (config_overrides.size (), 3);

	// Try it with arrays, with and without escaped quotes
	key_value_pairs.push_back ({ "node.work_peers", "[127.0.0.1:7000,\"128.0.0.1:50000\"]" });
	config_overrides = nano::config_overrides (key_value_pairs);
	ASSERT_EQ (config_overrides[3], "node.work_peers=[\"127.0.0.1:7000\",\"128.0.0.1:50000\"]");
	ASSERT_EQ (config_overrides.size (), 4);
}

namespace
{
std::string call_cli_command (boost::program_options::variables_map const & vm)
{
	std::stringstream ss;
	nano::test::cout_redirect redirect (ss.rdbuf ());

	// Execute CLI command. This populates the stringstream with a string like: "Private: 123\n Public: 456\n Account: nano_123"
	auto ec = nano::handle_node_options (vm);
	release_assert (!static_cast<bool> (ec));
	return ss.str ();
}
}