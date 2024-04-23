#pragma once

#include <nano/lib/work.hpp>
#include <nano/secure/common.hpp>

#include <boost/asio/io_context.hpp>

#include <filesystem>

namespace nano
{

class node;
class node_flags;

class node_wrapper final
{
public:
	node_wrapper (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, nano::node_flags const & node_flags_a);
	~node_wrapper ();

	nano::network_params network_params;
	std::shared_ptr<boost::asio::io_context> io_context;
	nano::work_pool work;
	std::shared_ptr<nano::node> node;
};

}