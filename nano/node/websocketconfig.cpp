#include <nano/lib/jsonconfig.hpp>
#include <nano/node/websocketconfig.hpp>

nano::error nano::websocket::config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("enable", enabled);
	json.put ("address", address.to_string ());
	json.put ("port", port);
	return json.get_error ();
}

nano::error nano::websocket::config::deserialize_json (nano::jsonconfig & json)
{
	json.get<bool> ("enable", enabled);
	json.get_required<boost::asio::ip::address_v6> ("address", address);
	json.get<uint16_t> ("port", port);
	return json.get_error ();
}
