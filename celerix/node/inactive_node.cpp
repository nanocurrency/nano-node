#include <celerix/node/active_elections.hpp>
#include <celerix/node/inactive_node.hpp>
#include <celerix/node/node.hpp>

celerix::inactive_node::inactive_node (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, celerix::node_flags const & node_flags_a) :
	node_wrapper (path_a, config_path_a, node_flags_a),
	node (node_wrapper.node)
{
	node_wrapper.node->active.stop ();
}

celerix::inactive_node::inactive_node (std::filesystem::path const & path_a, celerix::node_flags const & node_flags_a) :
	inactive_node (path_a, path_a, node_flags_a)
{
}

celerix::node_flags const & celerix::inactive_node_flag_defaults ()
{
	static celerix::node_flags node_flags;
	node_flags.inactive_node = true;
	node_flags.read_only = true;
	node_flags.generate_cache.reps = false;
	node_flags.generate_cache.cemented_count = false;
	node_flags.generate_cache.unchecked_count = false;
	node_flags.generate_cache.account_count = false;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_tcp_realtime = true;
	return node_flags;
}
