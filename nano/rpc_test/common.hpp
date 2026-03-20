#pragma once

#include <boost/asio/ip/tcp.hpp>

namespace nano
{
class node;
class node_config;
class node_flags;
class public_key;
class account;

namespace store
{
	class ledger_store;
}

namespace test
{
	class system;
	std::shared_ptr<nano::node> add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config, nano::node_flags const & node_flags);
	std::shared_ptr<nano::node> add_ipc_enabled_node (nano::test::system & system, nano::node_config & node_config);
	std::shared_ptr<nano::node> add_ipc_enabled_node (nano::test::system & system);
	void reset_confirmation_height (nano::store::ledger_store & store, nano::account const & account);
}
}
