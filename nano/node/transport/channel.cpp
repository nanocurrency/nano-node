#include <nano/lib/config.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/network_params.hpp>

nano::transport::channel::channel (nano::node & node_a) :
	channel{ node_a, nano::transport::peer_info{
					 .protocol_version = node_a.network_params.network.protocol_version,
					 } }
{
}

nano::transport::channel::channel (nano::node & node_a, nano::transport::peer_info const & peer_a) :
	node{ node_a },
	peer{ peer_a }
{
}

bool nano::transport::channel::send (nano::messages::message const & message, nano::transport::traffic_type traffic_type, callback_t callback)
{
	bool sent = send_impl (message, traffic_type, std::move (callback));
	node.stats.inc (sent ? nano::stat::type::message : nano::stat::type::message_drop, to_stat_detail (message.type ()), nano::stat::dir::out, /* aggregate all */ true);
	return sent;
}

bool nano::transport::channel::send_buffer (nano::shared_const_buffer const & buffer, nano::messages::message_type type, nano::transport::traffic_type traffic_type, callback_t callback)
{
	bool sent = send_buffer_impl (buffer, traffic_type, std::move (callback));
	node.stats.inc (sent ? nano::stat::type::message : nano::stat::type::message_drop, to_stat_detail (type), nano::stat::dir::out, /* aggregate all */ true);
	return sent;
}

bool nano::transport::channel::send_impl (nano::messages::message const & message, nano::transport::traffic_type traffic_type, callback_t callback)
{
	return send_buffer_impl (message.to_shared_const_buffer (), traffic_type, std::move (callback));
}

bool nano::transport::channel::send_buffer_as_message (nano::shared_const_buffer const & buffer, nano::transport::traffic_type traffic_type, callback_t callback)
{
	nano::shared_buffer const bytes = buffer;
	// Parsed like network input, minus the duplicate filter, which would drop repeats a caller sends on purpose
	auto [message, status] = nano::deserialize_message (*bytes, node.network_params.network, nullptr, &node.block_uniquer, &node.vote_uniquer);
	// A message serialized in this process can only fail on its content, any other failure means serializing and parsing disagree
	debug_assert (message || status == nano::deserialize_message_status::insufficient_work, nano::to_string (status));
	if (!message)
	{
		return false;
	}
	return send_impl (*message, traffic_type, std::move (callback));
}

bool nano::transport::channel::max (nano::transport::traffic_type traffic_type)
{
	// TODO: Compile the saturation check out of node builds, only test suites use it
	if (nano::is_dev_run () && saturation.at (traffic_type))
	{
		return true;
	}
	return max_impl (traffic_type);
}

bool nano::transport::channel::max_impl (nano::transport::traffic_type)
{
	return false;
}

void nano::transport::channel::saturate (nano::transport::traffic_type traffic_type)
{
	debug_assert (nano::is_dev_run ());
	saturation.at (traffic_type) = true;
}

void nano::transport::channel::unsaturate (nano::transport::traffic_type traffic_type)
{
	debug_assert (nano::is_dev_run ());
	saturation.at (traffic_type) = false;
}

bool nano::transport::channel::alive () const
{
	return true;
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

nano::transport::peer_info const & nano::transport::channel::get_peer_info () const noexcept
{
	return peer;
}

nano::account nano::transport::channel::get_node_id () const
{
	return peer.node_id;
}

std::optional<nano::account> nano::transport::channel::get_node_id_optional () const
{
	if (peer.node_id.is_zero ())
	{
		return std::nullopt;
	}
	return peer.node_id;
}

uint8_t nano::transport::channel::get_network_version () const
{
	return peer.protocol_version;
}

nano::node_capabilities_flags nano::transport::channel::get_flags () const
{
	return peer.capabilities;
}

bool nano::transport::channel::has_capability (nano::node_capabilities capability) const
{
	return peer.capabilities.test (capability);
}

bool nano::transport::channel::serves_ledger () const
{
	return !has_capability (nano::node_capabilities::no_ledger);
}

std::chrono::steady_clock::time_point nano::transport::channel::get_last_packet_received () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return last_packet_received;
}

void nano::transport::channel::set_last_packet_received (std::chrono::steady_clock::time_point const time)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	last_packet_received = time;
}

std::chrono::steady_clock::time_point nano::transport::channel::get_last_packet_sent () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return last_packet_sent;
}

void nano::transport::channel::set_last_packet_sent (std::chrono::steady_clock::time_point const time)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	last_packet_sent = time;
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
