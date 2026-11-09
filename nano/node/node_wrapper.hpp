#pragma once

#include <nano/lib/work.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/network_params.hpp>

#include <filesystem>
#include <memory>

namespace nano
{

class data_dir_lock;
class node;
class node_flags;

class node_wrapper final
{
public:
	node_wrapper (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, nano::node_flags const & node_flags_a);
	~node_wrapper ();

	nano::network_params network_params;
	nano::work_pool work;

	// Held while the node may write, so a running node and a writing command never share the data directory
	std::unique_ptr<nano::data_dir_lock> lock;

	std::shared_ptr<nano::node> node;
};

}
