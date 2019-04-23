#pragma once

#include <boost/filesystem.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <string>

namespace nano
{
class node_rpc_config final
{
public:
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (bool & upgraded_a, nano::jsonconfig &, boost::filesystem::path const & data_path);
	bool enable_sign_hash{ false };
	uint64_t max_work_generate_difficulty{ 0xffffffffc0000000 };
	std::string rpc_path{ get_default_rpc_filepath () };
	bool rpc_in_process{ true };
	static int json_version ()
	{
		return 1;
	}

private:
	void migrate (nano::jsonconfig & json, boost::filesystem::path const & data_path);
};
}
