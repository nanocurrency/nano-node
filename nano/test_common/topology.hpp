#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/test_common/system.hpp>

#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace nano::test
{
/**
 * Nodes with controlled reachability on top of nano::test::system.
 * Public nodes accept connections from anyone, hidden nodes can only reach public nodes, like nodes behind NAT.
 * Hidden nodes blocklist each other's node ids, so keepalive discovery never lets two of them peer.
 * Nodes are started disconnected, the test wires the links it wants with connect ().
 */
class topology final
{
public:
	explicit topology (nano::test::system &);

	// Start a node that accepts connections from every other node
	std::shared_ptr<nano::node> add_public (nano::node_config const &, nano::node_flags const & = {}, std::optional<nano::keypair> const & rep = std::nullopt);
	// Start a node that can only reach public nodes
	std::shared_ptr<nano::node> add_hidden (nano::node_config const &, nano::node_flags const & = {}, std::optional<nano::keypair> const & rep = std::nullopt);

	// Connect one node to another and wait until both hold the channel
	std::error_code connect (nano::node & from, nano::node & to);

	// Whether the two nodes hold a channel to each other
	static bool connected (nano::node & a, nano::node & b);

public:
	std::vector<std::shared_ptr<nano::node>> nodes; // Every node in creation order
	std::vector<std::shared_ptr<nano::node>> public_nodes;
	std::vector<std::shared_ptr<nano::node>> hidden_nodes;

private:
	nano::test::system & system;
};
}
