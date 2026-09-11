#include <nano/lib/threading.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/rpc/rpc_host.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/rpc_test/common.hpp>
#include <nano/rpc_test/rpc_context.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/test_response.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

nano::test::rpc_context::~rpc_context ()
{
	if (host)
	{
		host->stop ();
	}
}

void nano::test::wait_response_impl (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time, boost::property_tree::ptree & response_json)
{
	auto response = test_response::send (request, rpc_ctx.host->listening_port (), *system.io_ctx);
	ASSERT_TIMELY (time, response->status != 0);
	ASSERT_EQ (200, response->status);
	response_json = response->json;
}

boost::property_tree::ptree nano::test::wait_response (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time)
{
	boost::property_tree::ptree response_json;
	wait_response_impl (system, rpc_ctx, request, time, response_json);
	return response_json;
}

void nano::test::wait_responses_impl (nano::test::system & system, rpc_context const & rpc_ctx, std::vector<boost::property_tree::ptree> & requests, std::chrono::duration<double, std::nano> const & time, std::vector<boost::property_tree::ptree> & responses)
{
	// Start every request before waiting so they are in flight simultaneously
	std::vector<std::shared_ptr<test_response>> in_flight;
	in_flight.reserve (requests.size ());
	for (auto & request : requests)
	{
		in_flight.push_back (test_response::send (request, rpc_ctx.host->listening_port (), *system.io_ctx));
	}
	ASSERT_TIMELY (time, std::all_of (in_flight.begin (), in_flight.end (), [] (auto const & response) { return response->status != 0; }));
	for (auto const & response : in_flight)
	{
		ASSERT_EQ (200, response->status);
		responses.push_back (response->json);
	}
}

std::vector<boost::property_tree::ptree> nano::test::wait_responses (nano::test::system & system, rpc_context const & rpc_ctx, std::vector<boost::property_tree::ptree> & requests, std::chrono::duration<double, std::nano> const & time)
{
	std::vector<boost::property_tree::ptree> responses;
	wait_responses_impl (system, rpc_ctx, requests, time, responses);
	return responses;
}

bool nano::test::check_block_response_count (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count)
{
	auto response (wait_response (system, rpc_ctx, request));
	auto & blocks = response.get_child ("blocks");
	return size_count == blocks.size ();
}

nano::test::rpc_context nano::test::add_rpc (nano::test::system & system, std::shared_ptr<nano::node> const & node_a, rpc_options const & options)
{
	rpc_context ctx;
	ctx.node_rpc_config = std::make_unique<nano::node_rpc_config> ();
	ctx.ipc_server = std::make_shared<nano::ipc::ipc_server> (*node_a, *ctx.node_rpc_config, options.stop_callback);

	nano::rpc_config rpc_config (node_a->network_params.network, system.get_available_port (), options.enable_control);
	rpc_config.rpc_process.io_threads = 2;
	if (options.num_ipc_connections)
	{
		rpc_config.rpc_process.num_ipc_connections = *options.num_ipc_connections;
	}

	auto const ipc_tcp_port = ctx.ipc_server->listening_tcp_port ();
	debug_assert (ipc_tcp_port.has_value ());
	ctx.host = std::make_unique<nano::rpc_host> (rpc_config);
	ctx.host->start (std::make_unique<nano::ipc_rpc_processor> (ctx.host->io_context (), rpc_config, ipc_tcp_port.value (), options.stop_callback));
	return ctx;
}
