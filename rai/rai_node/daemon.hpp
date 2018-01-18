#include <rai/node/node.hpp>
#include <rai/node/rpc.hpp>

namespace rai_daemon
{
class daemon
{
public:
	/**
	 * Start node in daemon mode
	 * @throws rai::config_error Throwm if the config file cannot be parsed
	 */
	void run (boost::filesystem::path const &);
};
class daemon_config
{
public:
	daemon_config (boost::filesystem::path const &);
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	void serialize_json (boost::property_tree::ptree &);
	void upgrade_json (unsigned, boost::property_tree::ptree &);
	bool rpc_enable;
	rai::rpc_config rpc;
	rai::node_config node;
	bool opencl_enable;
	rai::opencl_config opencl;
};
}
