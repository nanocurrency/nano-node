#include <nano/node/endpoint.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/format.hpp>

nano::transport::channel::channel (nano::node & node_a) :
	node{ node_a }
{
	set_network_version (node_a.network_params.network.protocol_version);
}

bool nano::transport::channel::send (nano::messages::message const & message, nano::transport::traffic_type traffic_type, callback_t callback)
{
	bool sent = send_impl (message, traffic_type, std::move (callback));
	node.stats.inc (sent ? nano::stat::type::message : nano::stat::type::drop, to_stat_detail (message.type ()), nano::stat::dir::out, /* aggregate all */ true);
	return sent;
}

void nano::transport::channel::set_peering_endpoint (nano::endpoint endpoint)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	peering_endpoint = endpoint;
}

nano::endpoint nano::transport::channel::get_peering_endpoint () const
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		if (peering_endpoint)
		{
			return *peering_endpoint;
		}
	}
	return get_remote_endpoint ();
}

void nano::transport::channel::set_last_keepalive (nano::messages::keepalive const & message)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	last_keepalive = message;
}

std::optional<nano::messages::keepalive> nano::transport::channel::pop_last_keepalive ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto result = last_keepalive;
	last_keepalive.reset ();
	return result;
}

std::shared_ptr<nano::node> nano::transport::channel::owner () const
{
	return node.shared ();
}

void nano::transport::channel::operator() (nano::object_stream & obs) const
{
	obs.write ("remote_endpoint", get_remote_endpoint ());
	obs.write ("local_endpoint", get_local_endpoint ());
	obs.write ("peering_endpoint", get_peering_endpoint ());
	obs.write ("node_id", get_node_id ().to_node_id ());
}
