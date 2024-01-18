#include <nano/rpc_test/common.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

std::shared_ptr<nano::node> nano::test::add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config, nano::node_flags const & node_flags)
{
	node_config.ipc_config.transport_tcp.enabled = true;
	node_config.ipc_config.transport_tcp.port = system.get_available_port ();
	return system.add_node (node_config, node_flags);
}

std::shared_ptr<nano::node> nano::test::add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config)
{
	return add_ipc_enabled_node (system, node_config, nano::node_flags ());
}

std::shared_ptr<nano::node> nano::test::add_ipc_enabled_node (nano::test::system & system)
{
	nano::node_config node_config = system.default_config ();
	return add_ipc_enabled_node (system, node_config);
}

void nano::test::reset_confirmation_height (nano::store::component & store, nano::account const & account)
{
	auto transaction = store.tx_begin_write ();
	nano::confirmation_height_info confirmation_height_info;
	if (!store.confirmation_height.get (transaction, account, confirmation_height_info))
	{
		store.confirmation_height.clear (transaction, account);
	}
}