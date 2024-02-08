#include <nano/node/common.hpp>
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

void nano::transport::channel::send (nano::message & message_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a, nano::transport::buffer_drop_policy drop_policy_a, nano::transport::traffic_type traffic_type)
{
	auto buffer = message_a.to_shared_const_buffer ();

	bool is_droppable_by_limiter = (drop_policy_a == nano::transport::buffer_drop_policy::limiter);
	bool should_pass = node.outbound_limiter.should_pass (buffer.size (), to_bandwidth_limit_type (traffic_type));
	bool pass = !is_droppable_by_limiter || should_pass;

	node.stats.inc (pass ? nano::stat::type::message : nano::stat::type::drop, to_stat_detail (message_a.type ()), nano::stat::dir::out);
	node.logger.trace (nano::log::type::channel_sent, to_log_detail (message_a.type ()),
	nano::log::arg{ "message", message_a },
	nano::log::arg{ "channel", *this },
	nano::log::arg{ "dropped", !pass });

	if (pass)
	{
		send_buffer (buffer, callback_a, drop_policy_a, traffic_type);
	}
	else
	{
		if (callback_a)
		{
			node.background ([callback_a] () {
				callback_a (boost::system::errc::make_error_code (boost::system::errc::not_supported), 0);
			});
		}
	}
}

void nano::transport::channel::set_peering_endpoint (nano::endpoint endpoint)
{
	nano::lock_guard<nano::mutex> lock{ channel_mutex };
	peering_endpoint = endpoint;
}

nano::endpoint nano::transport::channel::get_peering_endpoint () const
{
	nano::unique_lock<nano::mutex> lock{ channel_mutex };
	if (peering_endpoint)
	{
		return *peering_endpoint;
	}
	else
	{
		lock.unlock ();
		return get_endpoint ();
	}
}

void nano::transport::channel::operator() (nano::object_stream & obs) const
{
	obs.write ("endpoint", get_endpoint ());
	obs.write ("peering_endpoint", get_peering_endpoint ());
	obs.write ("node_id", get_node_id ());
}
