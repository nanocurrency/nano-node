#pragma once

#include <celerix/lib/work.hpp>
#include <celerix/secure/common.hpp>

#include <boost/asio/io_context.hpp>

#include <filesystem>

namespace celerix
{

class node;
class node_flags;

class node_wrapper final
{
public:
	node_wrapper (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, celerix::node_flags const & node_flags_a);
	~node_wrapper ();

	celerix::network_params network_params;
	std::shared_ptr<boost::asio::io_context> io_context;
	celerix::work_pool work;
	std::shared_ptr<celerix::node> node;
};

}