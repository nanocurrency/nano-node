#include <rai/node.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/asio/ip/address_v6.hpp>

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
        daemon_config (bool &, std::istream &);
        void serialize (std::ostream &);
        std::vector <std::string> bootstrap_peers;
        uint16_t peering_port;
        bool rpc_enable;
        boost::asio::ip::address_v6 rpc_address;
        uint16_t rpc_port;
        bool rpc_enable_control;
		rai::logging logging;
    };
}