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
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	void serialize_json (boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	bool rpc_enable;
	nano::rpc_config rpc;
	nano::node_config node;
	bool opencl_enable;
	nano::opencl_config opencl;
	static constexpr int json_version = 2;
};
}
