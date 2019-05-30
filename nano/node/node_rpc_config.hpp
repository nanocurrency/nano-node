#pragma once

#include <nano/lib/rpcconfig.hpp>

#include <boost/filesystem.hpp>

#include <string>

namespace nano
{
class rpc_child_process_config final
{
public:
	bool enable{ false };
	std::string rpc_path{ get_default_rpc_filepath () };
};

class node_rpc_config final
{
public:
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (bool & upgraded_a, nano::jsonconfig &, boost::filesystem::path const & data_path);
	bool enable_sign_hash{ false };
	uint64_t max_work_generate_difficulty{ 0xffffffffc0000000 };
	nano::rpc_child_process_config child_process;
	static int json_version ()
	{
		return 1;
	}

private:
	void migrate (nano::jsonconfig & json, boost::filesystem::path const & data_path);
};
}
