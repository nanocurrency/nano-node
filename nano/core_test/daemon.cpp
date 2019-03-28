#include <gtest/gtest.h>

#include <nano/node/daemonconfig.hpp>

TEST (daemon_config, upgrade_v2_v3)
{
	// Test the IPC/RPC config migration
	nano::jsonconfig daemon;
	daemon.put ("version", 2);
	daemon.put ("rpc_enable", true);

	nano::jsonconfig rpc;
	rpc.put ("address", "::1");
	rpc.put ("port", 11111);
	rpc.put ("version", 1);
	daemon.put_child ("rpc", rpc);

	nano::jsonconfig node;
	nano::logging logging1;
	nano::jsonconfig logging_l;
	logging1.serialize_json (logging_l);
	node.put_child ("logging", logging_l);
	nano::jsonconfig preconfigured_peers_l;
	node.put_child ("preconfigured_peers", preconfigured_peers_l);
	nano::jsonconfig preconfigured_representatives_l;
	node.put_child ("preconfigured_representatives", preconfigured_representatives_l);
	nano::jsonconfig work_peers_l;
	node.put_child ("work_peers", work_peers_l);
	node.put ("version", 16);

	nano::jsonconfig ipc;
	nano::jsonconfig tcp;
	tcp.put ("enable", false);
	tcp.put ("port", 666);
	ipc.put_child ("tcp", tcp);
	node.put_child ("ipc", ipc);
	daemon.put_child ("node", node);

	bool updated = false;
	auto data_path = nano::unique_path ();
	boost::filesystem::create_directory (data_path);
	nano::daemon_config daemon_config (data_path);
	daemon_config.deserialize_json (updated, daemon);
	ASSERT_TRUE (updated);

	ASSERT_GE (daemon.get_required_child ("node").get<unsigned> ("version"), 3u);
	ASSERT_TRUE (daemon.get_required_child ("node").get_optional_child ("ipc")->get_optional_child ("tcp")->get<bool> ("enable"));

	// Check that the rpc config file is created
	auto rpc_path = nano::get_rpc_config_path (data_path);
	nano::rpc_config rpc_config;
	nano::jsonconfig json;
	updated = false;
	ASSERT_FALSE (json.read_and_update (rpc_config, rpc_path));
	ASSERT_FALSE (updated);

	ASSERT_EQ (rpc_config.port, 11111);
	ASSERT_EQ (rpc_config.ipc_port, 666);
}
