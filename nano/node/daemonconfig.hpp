#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/openclconfig.hpp>

namespace nano
{
class daemon_config
{
public:
	daemon_config ();
	nano::error deserialize_json (bool &, nano::jsonconfig &);
	nano::error serialize_json (nano::jsonconfig &);
	/**
	 * Returns true if an upgrade occurred
	 * @param version The version to upgrade to.
	 * @param config Configuration to upgrade.
	 */
	bool upgrade_json (unsigned version, nano::jsonconfig & config);
	nano::node_config node;
	bool opencl_enable;
	nano::opencl_config opencl;
	int json_version () const
	{
		return 3;
	}
};

nano::error read_and_update_daemon_config (boost::filesystem::path const &, nano::daemon_config & config_a);
}
