#include <celerix/rpc_test/common.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

std::shared_ptr<celerix::node> celerix::test::add_ipc_enabled_node (celerix::test::system & system, celerix::node_config & node_config, celerix::node_flags const & node_flags)
{
	node_config.ipc_config.transport_tcp.enabled = true;
	node_config.ipc_config.transport_tcp.port = system.get_available_port ();
	return system.add_node (node_config, node_flags);
}

std::shared_ptr<celerix::node> celerix::test::add_ipc_enabled_node (celerix::test::system & system, celerix::node_config & node_config)
{
	return add_ipc_enabled_node (system, node_config, celerix::node_flags ());
}

std::shared_ptr<celerix::node> celerix::test::add_ipc_enabled_node (celerix::test::system & system)
{
	celerix::node_config node_config = system.default_config ();
	return add_ipc_enabled_node (system, node_config);
}

void celerix::test::reset_confirmation_height (celerix::store::component & store, celerix::account const & account)
{
	auto transaction = store.tx_begin_write ();
	celerix::confirmation_height_info confirmation_height_info;
	if (!store.confirmation_height.get (transaction, account, confirmation_height_info))
	{
		store.confirmation_height.clear (transaction, account);
	}
}