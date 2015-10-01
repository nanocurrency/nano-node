#include <rai/node/node.hpp>
#include <rai/node/rpc.hpp>

namespace rai_daemon
{
    class daemon
    {
    public:
        void run ();
    };
    class daemon_config
    {
    public:
        daemon_config ();
        daemon_config (bool &, std::istream &);
        void serialize (std::ostream &);
		bool rpc_enable;
		rai::rpc_config rpc;
		rai::node_config node;
    };
}