#include <nano/lib/cli.hpp>
#include <nano/lib/files.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/inactive_node.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/wallet.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <regex>

using namespace std::chrono_literals;

namespace
{
std::string call_cli_command (boost::program_options::variables_map const & vm);

/** What a CLI command returned and printed to stdout */
struct cli_result
{
	std::error_code error;
	std::string output;
};

/** Runs a CLI command that is allowed to fail */
cli_result run_cli_command (boost::program_options::variables_map const & vm);
/** Command line options given once each, with an empty value for an option that takes none */
boost::program_options::variables_map cli_options (std::vector<std::pair<std::string, std::string>> const & options);
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

/** Overrides are read from the named command line option and are empty when it is absent */
TEST (cli, config_overrides_from_options)
{
	boost::program_options::options_description description;
	// clang-format off
	description.add_options ()
		("config", boost::program_options::value<std::vector<nano::config_key_value_pair>> ()->multitoken ())
		("rpcconfig", boost::program_options::value<std::vector<nano::config_key_value_pair>> ()->multitoken ());
	// clang-format on

	char const * argv[] = { "nano_node", "--config", "node.peering_port=7075", "--config", "node.work_peers=[a:1,b:2]" };
	boost::program_options::variables_map vm;
	boost::program_options::store (boost::program_options::parse_command_line (5, argv, description), vm);
	boost::program_options::notify (vm);

	auto overrides = nano::config_overrides (vm);
	ASSERT_EQ (overrides, (std::vector<std::string>{ "node.peering_port=\"7075\"", "node.work_peers=[\"a:1\",\"b:2\"]" }));
	ASSERT_TRUE (nano::config_overrides (vm, "rpcconfig").empty ());
}

TEST (cli, enable_rpc_flag)
{
	boost::program_options::options_description description;
	nano::add_node_flag_options (description);

	char const * argv[] = { "nano_node", "--enable_rpc" };
	boost::program_options::variables_map vm;
	boost::program_options::store (boost::program_options::parse_command_line (2, argv, description), vm);
	boost::program_options::notify (vm);

	nano::node_flags flags;
	nano::update_flags (flags, vm);

	ASSERT_TRUE (flags.enable_rpc);
}

/*
 * key_expand prints the public key and account of a private key written as exactly 64 hex digits.
 * Any other text is refused, as it would expand a different key than the one meant.
 */
TEST (cli, key_expand)
{
	nano::keypair const key;

	// A full private key expands to its public key and account
	auto const expanded = run_cli_command (cli_options ({ { "key_expand", "" }, { "key", key.prv.to_string () } }));
	ASSERT_FALSE (expanded.error);
	ASSERT_EQ ("Private: " + key.prv.to_string () + "\nPublic: " + key.pub.to_string () + "\nAccount: " + key.pub.to_account () + "\n", expanded.output);

	// A key missing its last digit, or with a 0x prefix in place of its first two digits, is refused without printing a key
	for (auto const & text : { key.prv.to_string ().substr (0, 63), "0x" + key.prv.to_string ().substr (2) })
	{
		auto const refused = run_cli_command (cli_options ({ { "key_expand", "" }, { "key", text } }));
		ASSERT_EQ (std::error_code (nano::error_cli::invalid_arguments), refused.error) << text;
		ASSERT_TRUE (refused.output.empty ()) << text;
	}
}

/*
 * account_get prints the account of a public key written as exactly 64 hex digits, and refuses any other text.
 */
TEST (cli, account_get)
{
	nano::keypair const key;

	// A full public key names its account
	auto const found = run_cli_command (cli_options ({ { "account_get", "" }, { "key", key.pub.to_string () } }));
	ASSERT_FALSE (found.error);
	ASSERT_EQ ("Account: " + key.pub.to_account () + "\n", found.output);

	// A key missing its last digit, or with a 0x prefix in place of its first two digits, is refused without printing an account
	for (auto const & text : { key.pub.to_string ().substr (0, 63), "0x" + key.pub.to_string ().substr (2) })
	{
		auto const refused = run_cli_command (cli_options ({ { "account_get", "" }, { "key", text } }));
		ASSERT_EQ (std::error_code (nano::error_cli::invalid_arguments), refused.error) << text;
		ASSERT_TRUE (refused.output.empty ()) << text;
	}
}

/*
 * The wallet commands refuse a seed or key other than exactly 64 hex digits, given with --seed or the legacy --key alike.
 * A seed missing a digit would otherwise decode as a different seed, replacing the wallet's seed or restoring the wrong one.
 */
TEST (cli, wallet_malformed_seed_or_key)
{
	// A data directory with a wallet holding an account of its seed
	auto const path = nano::unique_path ().string ();
	auto const id = nano::random_wallet_id ();
	nano::raw_key seed;
	nano::account account;
	{
		auto flags = nano::inactive_node_flag_defaults ();
		flags.read_only = false;
		nano::inactive_node node (path, flags);
		auto wallet = node.node->wallets.create (id);
		ASSERT_NE (nullptr, wallet);
		account = wallet->deterministic_insert ().value ();
		seed = wallet->get_seed ().value ();
	}

	// Every command and option taking a seed or key refuses one missing its last digit
	auto const text = nano::keypair ().prv.to_string ().substr (0, 63);
	std::vector<std::vector<std::pair<std::string, std::string>>> const commands{
		{ { "wallet_create", "" }, { "seed", text } },
		{ { "wallet_create", "" }, { "key", text } },
		{ { "wallet_change_seed", "" }, { "wallet", id.to_string () }, { "seed", text } },
		{ { "wallet_change_seed", "" }, { "wallet", id.to_string () }, { "key", text } },
		{ { "wallet_add_adhoc", "" }, { "wallet", id.to_string () }, { "key", text } },
	};
	for (auto const & command : commands)
	{
		auto options = command;
		options.emplace_back ("data_path", path);
		auto const refused = run_cli_command (cli_options (options));
		ASSERT_EQ (std::error_code (nano::error_cli::invalid_arguments), refused.error) << command.front ().first << " --" << command.back ().first;
	}

	// The wallet keeps its seed and its only account, and no wallet was created
	nano::inactive_node node (path, nano::inactive_node_flag_defaults ());
	ASSERT_EQ (1, node.node->wallets.wallet_count ());
	auto wallet = node.node->wallets.open (id);
	ASSERT_NE (nullptr, wallet);
	ASSERT_FALSE (wallet->enter_password (""));
	ASSERT_EQ (seed, wallet->get_seed ().value ());
	auto const accounts = wallet->accounts ();
	ASSERT_EQ (1, accounts.size ());
	ASSERT_EQ (account, accounts.front ());
}

/*
 * The wallet commands take a seed or key written as exactly 64 hex digits, given with --seed or the legacy --key.
 */
TEST (cli, wallet_seed_and_key)
{
	// An initialized data directory, which the wallet commands open read only
	auto const path = nano::unique_path ().string ();
	{
		auto flags = nano::inactive_node_flag_defaults ();
		flags.read_only = false;
		nano::inactive_node node (path, flags);
	}
	// Prints the seed of a wallet, followed by its keys
	auto decrypt = [&path] (nano::wallet_id const & id) {
		return run_cli_command (cli_options ({ { "wallet_decrypt_unsafe", "" }, { "wallet", id.to_string () }, { "data_path", path } })).output;
	};

	// wallet_create restores a new wallet from the seed and prints its id
	auto const seed = nano::keypair ().prv;
	auto const created = run_cli_command (cli_options ({ { "wallet_create", "" }, { "seed", seed.to_string () }, { "data_path", path } }));
	ASSERT_FALSE (created.error);
	auto const id = nano::try_parse_wallet_id (created.output.substr (0, created.output.find ('\n')));
	ASSERT_TRUE (id);
	ASSERT_TRUE (decrypt (*id).starts_with ("Seed: " + seed.to_string () + "\n"));

	// wallet_change_seed replaces the seed with one given through --key
	auto const new_seed = nano::keypair ().prv;
	ASSERT_FALSE (run_cli_command (cli_options ({ { "wallet_change_seed", "" }, { "wallet", id->to_string () }, { "key", new_seed.to_string () }, { "data_path", path } })).error);
	ASSERT_TRUE (decrypt (*id).starts_with ("Seed: " + new_seed.to_string () + "\n"));

	// wallet_add_adhoc adds the private key
	nano::keypair const adhoc;
	ASSERT_FALSE (run_cli_command (cli_options ({ { "wallet_add_adhoc", "" }, { "wallet", id->to_string () }, { "key", adhoc.prv.to_string () }, { "data_path", path } })).error);
	ASSERT_NE (std::string::npos, decrypt (*id).find ("Pub: " + adhoc.pub.to_account () + " Prv: " + adhoc.prv.to_string () + "\n"));
}

/*
 * Node flags that no longer have any effect still parse, so command lines written for older releases keep working.
 */
TEST (cli, deprecated_node_flags)
{
	boost::program_options::options_description description;
	nano::add_node_flag_options (description);

	// Each flag is a known option rather than a parse error
	for (auto const flag : { "--disable_lazy_bootstrap", "--disable_legacy_bootstrap", "--disable_wallet_bootstrap", "--disable_bootstrap_bulk_pull_server", "--disable_bootstrap_bulk_push_client" })
	{
		char const * argv[] = { "nano_node", flag };
		boost::program_options::variables_map vm;
		ASSERT_NO_THROW (boost::program_options::store (boost::program_options::parse_command_line (2, argv, description), vm)) << flag;
	}
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

cli_result run_cli_command (boost::program_options::variables_map const & vm)
{
	std::stringstream ss;
	nano::test::cout_redirect redirect (ss.rdbuf ());
	auto error = nano::handle_node_options (vm);
	return { error, ss.str () };
}

boost::program_options::variables_map cli_options (std::vector<std::pair<std::string, std::string>> const & options)
{
	boost::program_options::variables_map vm;
	for (auto const & option : options)
	{
		if (option.second.empty ())
		{
			vm.emplace (option.first, boost::program_options::variable_value ());
		}
		else
		{
			vm.emplace (option.first, boost::program_options::variable_value (option.second, false));
		}
	}
	return vm;
}
}
