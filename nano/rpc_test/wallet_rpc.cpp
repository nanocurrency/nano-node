#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/wallet.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/rpc_test/common.hpp>
#include <nano/rpc_test/rpc_context.hpp>
#include <nano/secure/common.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <unordered_set>

using namespace nano::test;
using namespace std::chrono_literals;

/*
 * Concurrency tests for wallet RPC handlers. Batches of requests are processed simultaneously,
 * so under TSAN these tests double as data race detectors for the wallet container.
 */

namespace
{
// Enough RPC → IPC connections for a whole batch of requests to be in flight at once
constexpr unsigned ipc_connections{ 8 };

boost::property_tree::ptree wallet_create_request ()
{
	boost::property_tree::ptree request;
	request.put ("action", "wallet_create");
	return request;
}

boost::property_tree::ptree wallet_destroy_request (nano::wallet_id const & wallet)
{
	boost::property_tree::ptree request;
	request.put ("action", "wallet_destroy");
	request.put ("wallet", wallet.to_string ());
	return request;
}

boost::property_tree::ptree wallet_export_request (nano::wallet_id const & wallet)
{
	boost::property_tree::ptree request;
	request.put ("action", "wallet_export");
	request.put ("wallet", wallet.to_string ());
	return request;
}

boost::property_tree::ptree account_list_request (nano::wallet_id const & wallet)
{
	boost::property_tree::ptree request;
	request.put ("action", "account_list");
	request.put ("wallet", wallet.to_string ());
	return request;
}

boost::property_tree::ptree account_move_request (nano::wallet_id const & wallet, nano::wallet_id const & source, nano::account const & account)
{
	boost::property_tree::ptree request;
	request.put ("action", "account_move");
	request.put ("wallet", wallet.to_string ());
	request.put ("source", source.to_string ());
	boost::property_tree::ptree accounts;
	boost::property_tree::ptree entry;
	entry.put ("", account.to_account ());
	accounts.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", accounts);
	return request;
}

boost::property_tree::ptree account_create_request (nano::wallet_id const & wallet)
{
	boost::property_tree::ptree request;
	request.put ("action", "account_create");
	request.put ("wallet", wallet.to_string ());
	request.put ("work", "false");
	return request;
}

boost::property_tree::ptree wallet_lock_request (nano::wallet_id const & wallet)
{
	boost::property_tree::ptree request;
	request.put ("action", "wallet_lock");
	request.put ("wallet", wallet.to_string ());
	return request;
}

boost::property_tree::ptree password_enter_request (nano::wallet_id const & wallet, std::string const & password)
{
	boost::property_tree::ptree request;
	request.put ("action", "password_enter");
	request.put ("wallet", wallet.to_string ());
	request.put ("password", password);
	return request;
}

boost::property_tree::ptree wallet_change_seed_request (nano::wallet_id const & wallet, nano::raw_key const & seed)
{
	boost::property_tree::ptree request;
	request.put ("action", "wallet_change_seed");
	request.put ("wallet", wallet.to_string ());
	request.put ("seed", seed.to_string ());
	return request;
}
}

// Concurrent wallet_create requests must each create a distinct wallet
TEST (rpc, wallet_concurrent_create)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .num_ipc_connections = ipc_connections });
	auto const initial_count = node->wallets.wallet_count ();

	std::vector<boost::property_tree::ptree> requests (8, wallet_create_request ());
	auto responses = wait_responses (system, rpc_ctx, requests);
	ASSERT_EQ (requests.size (), responses.size ());

	std::unordered_set<nano::wallet_id> ids;
	for (auto const & response : responses)
	{
		auto wallet_text (response.get_optional<std::string> ("wallet"));
		ASSERT_TRUE (wallet_text) << "unexpected response: " << response.get<std::string> ("error", "<none>");
		nano::wallet_id id;
		ASSERT_FALSE (id.decode_hex (*wallet_text));
		ASSERT_NE (nullptr, node->wallets.open (id));
		ids.insert (id);
	}
	ASSERT_EQ (requests.size (), ids.size ());
	ASSERT_EQ (initial_count + requests.size (), node->wallets.wallet_count ());
}

// Concurrent wallet_destroy requests for distinct wallets must all succeed
TEST (rpc, wallet_concurrent_destroy)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .num_ipc_connections = ipc_connections });
	auto const initial_count = node->wallets.wallet_count ();

	std::vector<boost::property_tree::ptree> requests;
	for (auto i = 0; i < 8; ++i)
	{
		auto id (nano::random_wallet_id ());
		ASSERT_NE (nullptr, node->wallets.create (id));
		requests.push_back (wallet_destroy_request (id));
	}
	auto responses = wait_responses (system, rpc_ctx, requests);
	ASSERT_EQ (requests.size (), responses.size ());

	for (auto const & response : responses)
	{
		ASSERT_EQ ("1", response.get<std::string> ("destroyed", ""));
	}
	ASSERT_EQ (initial_count, node->wallets.wallet_count ());
}

// Concurrent wallet_destroy requests for the same wallet: exactly one wins, the rest report wallet not found
TEST (rpc, wallet_concurrent_destroy_same)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .num_ipc_connections = ipc_connections });
	auto const initial_count = node->wallets.wallet_count ();

	// Repeated rounds give the destroy requests more chances to interleave
	for (auto round = 0; round < 10; ++round)
	{
		auto id (nano::random_wallet_id ());
		ASSERT_NE (nullptr, node->wallets.create (id));

		std::vector<boost::property_tree::ptree> requests (8, wallet_destroy_request (id));
		auto responses = wait_responses (system, rpc_ctx, requests);
		ASSERT_EQ (requests.size (), responses.size ());

		std::size_t destroyed{ 0 };
		for (auto const & response : responses)
		{
			if (response.get<std::string> ("destroyed", "") == "1")
			{
				++destroyed;
			}
			else
			{
				ASSERT_EQ ("Wallet not found", response.get<std::string> ("error", ""));
			}
		}
		ASSERT_EQ (1, destroyed);
		ASSERT_EQ (nullptr, node->wallets.open (id));
	}
	ASSERT_EQ (initial_count, node->wallets.wallet_count ());
}

// Hammers wallet creation, destruction and reads concurrently; the wallet set must stay consistent throughout
TEST (rpc, wallet_concurrent_create_destroy_read)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .num_ipc_connections = ipc_connections });

	// Stable wallet targeted by the read requests while other wallets churn
	auto stable_id (nano::random_wallet_id ());
	auto stable_wallet = node->wallets.create (stable_id);
	ASSERT_NE (nullptr, stable_wallet);
	ASSERT_TRUE (stable_wallet->insert_adhoc (nano::dev::genesis_key.prv, false));

	auto const initial_count = node->wallets.wallet_count ();
	auto const rounds = 5;

	std::vector<nano::wallet_id> previous_round;
	for (auto round = 0; round < rounds; ++round)
	{
		// A fresh wallet whose only account is moved into the stable wallet mid-batch
		nano::keypair key;
		auto source_id (nano::random_wallet_id ());
		auto source_wallet = node->wallets.create (source_id);
		ASSERT_NE (nullptr, source_wallet);
		ASSERT_TRUE (source_wallet->insert_adhoc (key.prv, false));

		std::vector<boost::property_tree::ptree> requests;
		requests.push_back (wallet_create_request ());
		requests.push_back (wallet_create_request ());
		// Destroy every wallet that the previous round left behind
		for (auto const & id : previous_round)
		{
			requests.push_back (wallet_destroy_request (id));
		}
		requests.push_back (account_move_request (stable_id, source_id, key.pub));
		requests.push_back (wallet_export_request (stable_id));
		requests.push_back (account_list_request (stable_id));
		requests.push_back (wallet_export_request (nano::random_wallet_id ()));

		auto responses = wait_responses (system, rpc_ctx, requests, 10s);
		ASSERT_EQ (requests.size (), responses.size ());

		std::vector<nano::wallet_id> created;
		for (std::size_t i = 0; i < 2; ++i)
		{
			auto wallet_text (responses[i].get_optional<std::string> ("wallet"));
			ASSERT_TRUE (wallet_text) << "unexpected response: " << responses[i].get<std::string> ("error", "<none>");
			nano::wallet_id id;
			ASSERT_FALSE (id.decode_hex (*wallet_text));
			created.push_back (id);
		}
		for (std::size_t i = 0; i < previous_round.size (); ++i)
		{
			ASSERT_EQ ("1", responses[2 + i].get<std::string> ("destroyed", ""));
		}
		auto const moved_index = 2 + previous_round.size ();
		ASSERT_EQ ("1", responses[moved_index].get<std::string> ("moved", ""));
		ASSERT_TRUE (stable_wallet->exists (key.pub));
		ASSERT_FALSE (responses[moved_index + 1].get<std::string> ("json", "").empty ());
		auto accounts_node = responses[moved_index + 2].get_child_optional ("accounts");
		ASSERT_TRUE (accounts_node);
		bool genesis_present{ false };
		for (auto const & entry : *accounts_node)
		{
			genesis_present |= entry.second.get<std::string> ("") == nano::dev::genesis_key.pub.to_account ();
		}
		ASSERT_TRUE (genesis_present);
		ASSERT_EQ ("Wallet not found", responses[moved_index + 3].get<std::string> ("error", ""));

		// The drained source wallet is destroyed together with the created wallets next round
		created.push_back (source_id);
		previous_round = created;
	}

	// Destroy the wallets left over from the final round
	for (auto const & id : previous_round)
	{
		ASSERT_TRUE (node->wallets.destroy (id));
	}
	ASSERT_EQ (initial_count, node->wallets.wallet_count ());
	// The stable wallet holds the genesis account plus one moved account per round
	ASSERT_EQ (1 + rounds, stable_wallet->accounts ().size ());
}

// Hammers account creation and seed changes concurrently with wallet locking and unlocking.
// Each key operation must either complete or report a locked wallet; the node must never crash
// on a lock that lands between a handler's password check and its use of the wallet key.
TEST (rpc, wallet_concurrent_lock_create)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .num_ipc_connections = ipc_connections });

	auto wallet_id (nano::random_wallet_id ());
	auto wallet = node->wallets.create (wallet_id);
	ASSERT_NE (nullptr, wallet);

	auto const rounds = 5;
	for (auto round = 0; round < rounds; ++round)
	{
		nano::raw_key seed;
		nano::random_pool::generate_block (seed.bytes.data (), seed.bytes.size ());

		std::vector<boost::property_tree::ptree> requests;
		requests.push_back (account_create_request (wallet_id));
		requests.push_back (wallet_lock_request (wallet_id));
		requests.push_back (account_create_request (wallet_id));
		requests.push_back (wallet_change_seed_request (wallet_id, seed));
		requests.push_back (password_enter_request (wallet_id, ""));
		requests.push_back (account_create_request (wallet_id));

		auto responses = wait_responses (system, rpc_ctx, requests, 10s);
		ASSERT_EQ (requests.size (), responses.size ());

		// Key operations either succeed or report a locked wallet, never a torn result or another error
		auto const locked_message = std::error_code (nano::error_common::wallet_locked).message ();
		for (auto const index : { 0, 2, 5 })
		{
			auto account_text (responses[index].get_optional<std::string> ("account"));
			auto error_text (responses[index].get_optional<std::string> ("error"));
			ASSERT_TRUE (account_text.has_value () != error_text.has_value ());
			if (error_text)
			{
				ASSERT_EQ (locked_message, *error_text);
			}
		}
		ASSERT_EQ ("1", responses[1].get<std::string> ("locked", ""));
		auto seed_success (responses[3].get_optional<std::string> ("success"));
		auto seed_error (responses[3].get_optional<std::string> ("error"));
		ASSERT_TRUE (seed_success.has_value () != seed_error.has_value ());
		if (seed_error)
		{
			ASSERT_EQ (locked_message, *seed_error);
		}
		ASSERT_EQ ("1", responses[4].get<std::string> ("valid", ""));

		// The wallet must be functional again once unlocked
		ASSERT_FALSE (wallet->enter_password (""));
		ASSERT_TRUE (wallet->deterministic_insert (false));
	}
}
