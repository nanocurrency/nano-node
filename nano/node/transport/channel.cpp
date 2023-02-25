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

void nano::transport::channel::send (nano::message & message_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a, nano::buffer_drop_policy drop_policy_a, nano::bandwidth_limit_type limiter_type)
{
	auto buffer (message_a.to_shared_const_buffer ());
	auto detail = nano::to_stat_detail (message_a.header.type);
	auto is_droppable_by_limiter = drop_policy_a == nano::buffer_drop_policy::limiter;
	auto should_pass (node.outbound_limiter.should_pass (buffer.size (), limiter_type));
	if (!is_droppable_by_limiter || should_pass)
	{
		send_buffer (buffer, callback_a, drop_policy_a);
		node.stats.inc (nano::stat::type::message, detail, nano::stat::dir::out);
	}
	else
	{
		if (callback_a)
		{
			node.background ([callback_a] () {
				callback_a (boost::system::errc::make_error_code (boost::system::errc::not_supported), 0);
			});
		}

		node.stats.inc (nano::stat::type::drop, detail, nano::stat::dir::out);
		if (node.config.logging.network_packet_logging ())
		{
			node.logger.always_log (boost::str (boost::format ("%1% of size %2% dropped") % nano::to_string (detail) % buffer.size ()));
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