#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/peer_blocklist.hpp>
#include <nano/test_common/topology.hpp>

nano::test::topology::topology (nano::test::system & system_a) :
	system{ system_a }
{
}

std::shared_ptr<nano::node> nano::test::topology::add_public (nano::node_config const & config, nano::node_flags const & flags, std::optional<nano::keypair> const & rep)
{
	auto node = system.make_disconnected_node (config, flags, rep);
	nodes.push_back (node);
	public_nodes.push_back (node);
	return node;
}

std::shared_ptr<nano::node> nano::test::topology::add_hidden (nano::node_config const & config, nano::node_flags const & flags, std::optional<nano::keypair> const & rep)
{
	auto node = system.make_disconnected_node (config, flags, rep);

	// Two hidden nodes can never peer, whichever of them learns about the other first
	for (auto const & other : hidden_nodes)
	{
		node->peer_blocklist.add (other->get_node_id ());
		other->peer_blocklist.add (node->get_node_id ());
	}

	nodes.push_back (node);
	hidden_nodes.push_back (node);
	return node;
}

std::error_code nano::test::topology::connect (nano::node & from, nano::node & to)
{
	return system.connect (from, to);
}

bool nano::test::topology::connected (nano::node & a, nano::node & b)
{
	return a.network.find_node_id (b.get_node_id ()) != nullptr && b.network.find_node_id (a.get_node_id ()) != nullptr;
}
