#pragma once

#include <nano/node/node_wrapper.hpp>

#include <boost/program_options/variables_map.hpp>

#include <filesystem>

namespace nano
{

class node;
class node_flags;

class inactive_node final
{
public:
	inactive_node (std::filesystem::path const & path_a, nano::node_flags const & node_flags_a);
	inactive_node (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, nano::node_flags const & node_flags_a);

	nano::node_wrapper node_wrapper;
	std::shared_ptr<nano::node> node;
};

std::unique_ptr<nano::inactive_node> default_inactive_node (std::filesystem::path const &, boost::program_options::variables_map const &);

} // namespace nano