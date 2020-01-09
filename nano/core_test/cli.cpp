#include <nano/core_test/testutil.hpp>
#include <nano/node/cli.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <regex>

#include <crypto/ed25519-donna/ed25519.h>

using namespace std::chrono_literals;

TEST (cli, key_create)
{
	boost::program_options::variables_map vm;
	auto data_path = nano::unique_path ();
	vm.emplace ("key_create", boost::program_options::variable_value ());

	std::stringstream ss;
	nano::cout_redirect redirect (ss.rdbuf ());

	// Execute CLI command. This populates the stringstream with a string like: "Private: 123\n Public: 456\n Account: nano_123"
	auto ec = nano::handle_node_options (vm);
	ASSERT_FALSE (ec);

	// Extract the private, public and account values. The regular expression extracts anything between the semi-colon and new line.
	std::regex regexpr (": (\\w+)");
	std::smatch matches;
	auto output = ss.str ();
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
	nano::private_key private_key;
	private_key.decode_hex (private_key_str);

	nano::public_key public_key;
	ed25519_publickey (private_key.bytes.data (), public_key.bytes.data ());
	ASSERT_EQ (vals[1], public_key.to_string ());
	ASSERT_EQ (vals[2], public_key.to_account ());
}
