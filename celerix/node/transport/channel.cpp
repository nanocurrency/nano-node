#include <celerix/node/endpoint.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/transport/channel.hpp>
#include <celerix/node/transport/transport.hpp>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/format.hpp>

celerix::transport::channel::channel (celerix::node & node_a) :
	node{ node_a }
{
	set_network_version (node_a.network_params.network.protocol_version);
}

bool celerix::transport::channel::send (celerix::message const & message, celerix::transport::traffic_type traffic_type, callback_t callback)
{
	auto buffer = message.to_shared_const_buffer ();
	bool sent = send_buffer (buffer, traffic_type, std::move (callback));
	node.stats.inc (sent ? celerix::stat::type::message : celerix::stat::type::drop, to_stat_detail (message.type ()), celerix::stat::dir::out, /* aggregate all */ true);
	return sent;
}

void celerix::transport::channel::set_peering_endpoint (celerix::endpoint endpoint)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	peering_endpoint = endpoint;
}

celerix::endpoint celerix::transport::channel::get_peering_endpoint () const
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		if (peering_endpoint)
		{
			return *peering_endpoint;
		}
	}
	return get_remote_endpoint ();
}

std::shared_ptr<celerix::node> celerix::transport::channel::owner () const
{
	return node.shared ();
}

void celerix::transport::channel::operator() (celerix::object_stream & obs) const
{
	obs.write ("remote_endpoint", get_remote_endpoint ());
	obs.write ("local_endpoint", get_local_endpoint ());
	obs.write ("peering_endpoint", get_peering_endpoint ());
	obs.write ("node_id", get_node_id ().to_node_id ());
}
