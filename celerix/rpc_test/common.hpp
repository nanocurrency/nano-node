#pragma once

#include <boost/asio/ip/tcp.hpp>

namespace celerix
{
class node;
class node_config;
class node_flags;
class public_key;
using account = public_key;

namespace store
{
	class component;
}

namespace test
{
	class system;
	std::shared_ptr<celerix::node> add_ipc_enabled_node (celerix::test::system & system, celerix::node_config & node_config, celerix::node_flags const & node_flags);
	std::shared_ptr<celerix::node> add_ipc_enabled_node (celerix::test::system & system, celerix::node_config & node_config);
	std::shared_ptr<celerix::node> add_ipc_enabled_node (celerix::test::system & system);
	void reset_confirmation_height (celerix::store::component & store, celerix::account const & account);
}
}
