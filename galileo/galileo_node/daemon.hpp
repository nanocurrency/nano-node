#include <galileo/node/node.hpp>
#include <galileo/node/rpc.hpp>

namespace galileo_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &);
};
class daemon_config
{
public:
	daemon_config (boost::filesystem::path const &);
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	void serialize_json (boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	bool rpc_enable;
	galileo::rpc_config rpc;
	galileo::node_config node;
	bool opencl_enable;
	galileo::opencl_config opencl;
};
}
