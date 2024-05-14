#include <nano/lib/threading.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/rpc_test/common.hpp>
#include <nano/rpc_test/rpc_context.hpp>
#include <nano/rpc_test/test_response.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

nano::test::rpc_context::rpc_context (std::shared_ptr<nano::rpc> & rpc_a, std::shared_ptr<nano::ipc::ipc_server> & ipc_server_a, std::unique_ptr<nano::ipc_rpc_processor> & ipc_rpc_processor_a, std::unique_ptr<nano::node_rpc_config> & node_rpc_config_a)
{
	rpc = std::move (rpc_a);
	ipc_server = std::move (ipc_server_a);
	ipc_rpc_processor = std::move (ipc_rpc_processor_a);
	node_rpc_config = std::move (node_rpc_config_a);
}

void nano::test::wait_response_impl (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time, boost::property_tree::ptree & response_json)
{
	test_response response (request, rpc_ctx.rpc->listening_port (), *system.io_ctx);
	ASSERT_TIMELY (time, response.status != 0);
	ASSERT_EQ (200, response.status);
	response_json = response.json;
}

boost::property_tree::ptree nano::test::wait_response (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time)
{
	boost::property_tree::ptree response_json;
	wait_response_impl (system, rpc_ctx, request, time, response_json);
	return response_json;
}

bool nano::test::check_block_response_count (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count)
{
	auto response (wait_response (system, rpc_ctx, request));
	auto & blocks = response.get_child ("blocks");
	return size_count == blocks.size ();
}

nano::test::rpc_context nano::test::add_rpc (nano::test::system & system, std::shared_ptr<nano::node> const & node_a)
{
	auto node_rpc_config (std::make_unique<nano::node_rpc_config> ());
	auto ipc_server (std::make_shared<nano::ipc::ipc_server> (*node_a, *node_rpc_config));
	nano::rpc_config rpc_config (node_a->network_params.network, system.get_available_port (), true);
	const auto ipc_tcp_port = ipc_server->listening_tcp_port ();
	debug_assert (ipc_tcp_port.has_value ());
	auto ipc_rpc_processor (std::make_unique<nano::ipc_rpc_processor> (*system.io_ctx, rpc_config, ipc_tcp_port.value ()));
	auto rpc (std::make_shared<nano::rpc> (system.io_ctx, rpc_config, *ipc_rpc_processor));
	rpc->start ();

	return rpc_context{ rpc, ipc_server, ipc_rpc_processor, node_rpc_config };
}
