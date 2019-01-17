#include <nano/lib/errors.hpp>
#include <nano/node/node.hpp>
#include <nano/node/rpc.hpp>

namespace nano_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &, nano::node_flags const & flags);
};
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
	bool rpc_enable;
	nano::rpc_config rpc;
	nano::node_config node;
	bool opencl_enable;
	nano::opencl_config opencl;
	int json_version () const
	{
		return 2;
	}
};
}
