#include <nano/boost/asio/post.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/loopback.hpp>
#include <nano/node/transport/message_deserializer.hpp>

#include <boost/format.hpp>

nano::transport::loopback_channel::loopback_channel (nano::node & node) :
	transport::channel{ node },
	endpoint{ node.network.endpoint () }
{
	set_node_id (node.node_id.pub);
	set_network_version (node.network_params.network.protocol_version);
}

bool nano::transport::loopback_channel::send_impl (nano::messages::message const & message, nano::transport::traffic_type traffic_type, nano::transport::channel::callback_t callback)
{
	node.stats.inc (nano::stat::type::message_loopback, to_stat_detail (message.type ()), nano::stat::dir::in);

	node.inbound (message, shared_from_this ());

	if (callback)
	{
		boost::asio::post (node.io_ctx, [callback_l = std::move (callback)] () {
			callback_l (boost::system::errc::make_error_code (boost::system::errc::success), 0);
		});
	}

	return true;
}

std::string nano::transport::loopback_channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}
