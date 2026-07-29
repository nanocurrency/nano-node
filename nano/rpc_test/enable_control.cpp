#include <nano/lib/rpcconfig.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/rpc_test/common.hpp>
#include <nano/rpc_test/rpc_context.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace nano::test;

namespace
{
// Error returned by the RPC layer when a control action is requested without enable_control
std::string const control_disabled_message{ "RPC control is disabled" };

/*
 * Every action the RPC layer gates behind enable_control.
 * Kept in sync by hand with create_rpc_control_impls() in nano/rpc/rpc_handler.cpp; the tests below fail if the two drift apart.
 */
std::vector<std::string> const control_actions{
	"account_create",
	"account_move",
	"account_remove",
	"account_representative_set",
	"accounts_create",
	"block_create",
	"bootstrap_lazy",
	"bootstrap_priorities",
	"bootstrap_reset",
	"database_txn_tracker",
	"epoch_upgrade",
	"keepalive",
	"ledger",
	"node_id",
	"password_change",
	"password_enter",
	"populate_backlog",
	"receive",
	"receive_minimum",
	"receive_minimum_set",
	"search_pending",
	"search_pending_all",
	"search_receivable",
	"search_receivable_all",
	"send",
	"stats_clear",
	"stop",
	"unchecked_clear",
	"unopened",
	"wallet_add",
	"wallet_add_watch",
	"wallet_change_seed",
	"wallet_create",
	"wallet_destroy",
	"wallet_export",
	"wallet_lock",
	"wallet_representative_set",
	"wallet_republish",
	"wallet_seed",
	"wallet_unlock",
	"wallet_work_get",
	"work_cancel",
	"work_generate",
	"work_get",
	"work_peer_add",
	"work_peers",
	"work_peers_clear",
	"work_set",
};

// Sends the action with no further parameters and returns the error field, or an empty string when the response carries no error
std::string error_of (nano::test::system & system, rpc_context const & rpc_ctx, std::string const & action)
{
	boost::property_tree::ptree request;
	request.put ("action", action);
	auto response = wait_response (system, rpc_ctx, request);
	return response.get<std::string> ("error", "");
}
}

/*
 * Without enable_control every control action is rejected before reaching the node,
 * so the rejection does not depend on the action's own parameter validation
 */
TEST (rpc, enable_control_disabled_rejects_control_actions)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .enable_control = false });

	for (auto const & action : control_actions)
	{
		ASSERT_EQ (control_disabled_message, error_of (system, rpc_ctx, action)) << "action: " << action;
	}
}

/*
 * With enable_control the same actions get past the gate and are handled by the node.
 * The requests carry no parameters, so most fail their own validation; the point is only that the failure is no longer the control gate.
 */
TEST (rpc, enable_control_enabled_admits_control_actions)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .enable_control = true });

	for (auto const & action : control_actions)
	{
		// "stop" shuts the node down and "work_generate" blocks without a hash, so neither can run inside this loop
		if (action == "stop" || action == "work_generate")
		{
			continue;
		}
		ASSERT_NE (control_disabled_message, error_of (system, rpc_ctx, action)) << "action: " << action;
	}
}

/*
 * Read-only actions stay available without enable_control
 */
TEST (rpc, enable_control_disabled_admits_read_actions)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .enable_control = false });

	for (auto const & action : { "block_count", "version", "peers", "representatives", "telemetry", "uptime" })
	{
		ASSERT_NE (control_disabled_message, error_of (system, rpc_ctx, action)) << "action: " << action;
	}
}

/*
 * The two actions gated on a parameter rather than on their name:
 * "stats" is control-only for type=objects, "process" is control-only when force is set
 */
TEST (rpc, enable_control_disabled_rejects_gated_parameters)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .enable_control = false });

	// stats: only type=objects requires control
	{
		boost::property_tree::ptree request;
		request.put ("action", "stats");
		request.put ("type", "objects");
		auto response = wait_response (system, rpc_ctx, request);
		ASSERT_EQ (control_disabled_message, response.get<std::string> ("error", ""));
	}
	{
		boost::property_tree::ptree request;
		request.put ("action", "stats");
		request.put ("type", "counters");
		auto response = wait_response (system, rpc_ctx, request);
		ASSERT_NE (control_disabled_message, response.get<std::string> ("error", ""));
	}

	// process: only force=true requires control
	{
		boost::property_tree::ptree request;
		request.put ("action", "process");
		request.put ("force", "true");
		auto response = wait_response (system, rpc_ctx, request);
		ASSERT_EQ (control_disabled_message, response.get<std::string> ("error", ""));
	}
	{
		boost::property_tree::ptree request;
		request.put ("action", "process");
		request.put ("force", "false");
		auto response = wait_response (system, rpc_ctx, request);
		ASSERT_NE (control_disabled_message, response.get<std::string> ("error", ""));
	}
}

/*
 * Every gated action name must correspond to an action the node actually handles.
 * A name that no longer exists gates nothing and hides the fact that its replacement may be ungated.
 */
TEST (rpc, enable_control_actions_are_known)
{
	nano::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node, { .enable_control = true });

	for (auto const & action : control_actions)
	{
		if (action == "stop" || action == "work_generate")
		{
			continue;
		}
		ASSERT_NE ("Unknown command", error_of (system, rpc_ctx, action)) << "action: " << action;
	}
}
