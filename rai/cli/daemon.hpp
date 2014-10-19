#include <rai/core/core.hpp>
#include <boost/property_tree/ptree.hpp>

namespace rai_daemon
{
    class daemon
    {
    public:
        daemon ();
        void run ();
        std::string path;
    };
    class daemon_config
    {
    public:
        daemon_config ();
        bool deserialize (std::istream &);
        void serialize (std::ostream &);
        uint16_t peering_port;
        bool rpc_enable;
        uint16_t rpc_port;
        bool rpc_enable_control;
    };
}