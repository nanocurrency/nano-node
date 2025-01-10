#include <celerix/boost/asio/ip/address_v6.hpp>
#include <celerix/lib/tomlconfig.hpp>
#include <celerix/node/websocketconfig.hpp>

celerix::websocket::config::config (celerix::network_constants & network_constants) :
	network_constants{ network_constants },
	port{ network_constants.default_websocket_port },
	address{ boost::asio::ip::address_v6::loopback ().to_string () }
{
}

celerix::error celerix::websocket::config::serialize_toml (celerix::tomlconfig & toml) const
{
	toml.put ("enable", enabled, "Enable or disable WebSocket server.\ntype:bool");
	toml.put ("address", address, "WebSocket server bind address.\ntype:string,ip");
	toml.put ("port", port, "WebSocket server listening port.\ntype:uint16");
	return toml.get_error ();
}

celerix::error celerix::websocket::config::deserialize_toml (celerix::tomlconfig & toml)
{
	toml.get<bool> ("enable", enabled);
	boost::asio::ip::address_v6 address_l;
	toml.get_optional<boost::asio::ip::address_v6> ("address", address_l, boost::asio::ip::address_v6::loopback ());
	address = address_l.to_string ();
	toml.get<uint16_t> ("port", port);
	return toml.get_error ();
}
