#include <celerix/lib/threading.hpp>
#include <celerix/node/ipc/ipc_server.hpp>
#include <celerix/rpc/rpc_request_processor.hpp>
#include <celerix/rpc_test/common.hpp>
#include <celerix/rpc_test/rpc_context.hpp>
#include <celerix/rpc_test/test_response.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

celerix::test::rpc_context::rpc_context (std::shared_ptr<celerix::rpc> & rpc_a, std::shared_ptr<celerix::ipc::ipc_server> & ipc_server_a, std::unique_ptr<celerix::ipc_rpc_processor> & ipc_rpc_processor_a, std::unique_ptr<celerix::node_rpc_config> & node_rpc_config_a)
{
	rpc = std::move (rpc_a);
	ipc_server = std::move (ipc_server_a);
	ipc_rpc_processor = std::move (ipc_rpc_processor_a);
	node_rpc_config = std::move (node_rpc_config_a);
}

void celerix::test::wait_response_impl (celerix::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::celerix> const & time, boost::property_tree::ptree & response_json)
{
	test_response response (request, rpc_ctx.rpc->listening_port (), *system.io_ctx);
	ASSERT_TIMELY (time, response.status != 0);
	ASSERT_EQ (200, response.status);
	response_json = response.json;
}

boost::property_tree::ptree celerix::test::wait_response (celerix::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::celerix> const & time)
{
	boost::property_tree::ptree response_json;
	wait_response_impl (system, rpc_ctx, request, time, response_json);
	return response_json;
}

bool celerix::test::check_block_response_count (celerix::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count)
{
	auto response (wait_response (system, rpc_ctx, request));
	auto & blocks = response.get_child ("blocks");
	return size_count == blocks.size ();
}

celerix::test::rpc_context celerix::test::add_rpc (celerix::test::system & system, std::shared_ptr<celerix::node> const & node_a)
{
	auto node_rpc_config (std::make_unique<celerix::node_rpc_config> ());
	auto ipc_server (std::make_shared<celerix::ipc::ipc_server> (*node_a, *node_rpc_config));
	celerix::rpc_config rpc_config (node_a->network_params.network, system.get_available_port (), true);
	const auto ipc_tcp_port = ipc_server->listening_tcp_port ();
	debug_assert (ipc_tcp_port.has_value ());
	auto ipc_rpc_processor (std::make_unique<celerix::ipc_rpc_processor> (*system.io_ctx, rpc_config, ipc_tcp_port.value ()));
	auto rpc (std::make_shared<celerix::rpc> (system.io_ctx, rpc_config, *ipc_rpc_processor));
	rpc->start ();

	return rpc_context{ rpc, ipc_server, ipc_rpc_processor, node_rpc_config };
}
