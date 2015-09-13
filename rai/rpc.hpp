#pragma once

#include <boost/asio.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace rai
{
class node;
class rpc_config
{
public:
	rpc_config ();
	rpc_config (bool);
    void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (boost::property_tree::ptree const &);
	boost::asio::ip::address_v6 address;
	uint16_t port;
	bool enable_control;
};
class rpc
{
public:
    rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, rai::node &, rai::rpc_config const &);
    void start ();
    void stop ();
    void operator () (boost::network::http::server <rai::rpc>::request const &, boost::network::http::server <rai::rpc>::response &);
    void log (const char *) {}
	rai::rpc_config config;
    boost::network::http::server <rai::rpc> server;
    rai::node & node;
    bool on;
};
}