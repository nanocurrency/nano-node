#include <nano/boost/asio/post.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/secure/network_params.hpp>

#include <boost/format.hpp>

nano::transport::inproc::channel::channel (nano::node & node, nano::node & destination) :
	transport::channel{ node, nano::transport::peer_info{
							  .node_id = node.get_node_id (),
							  .protocol_version = node.network_params.network.protocol_version,
							  } },
	destination{ destination }, endpoint{ node.network.endpoint () }
{
}

/**
 * Send the buffer to the peer and call the callback function when done. The call never fails.
 * Note that the inbound message visitor will be called before the callback because it is called directly whereas the callback is spawned in the background.
 */
bool nano::transport::inproc::channel::send_impl (nano::messages::message const & message, nano::transport::traffic_type traffic_type, nano::transport::channel::callback_t callback)
{
	auto const bytes = message.to_bytes ();

	// Parsed like network input to the destination, minus the duplicate filter, which would drop repeats a caller sends on purpose
	auto [incoming, status] = nano::deserialize_message (*bytes, destination.network_params.network, nullptr, &destination.block_uniquer, &destination.vote_uniquer);

	// A message serialized in this process can only fail on its content, any other failure means serializing and parsing disagree
	debug_assert (incoming || status == nano::deserialize_message_status::insufficient_work, nano::to_string (status));

	if (incoming)
	{
		// we create a temporary channel for the reply path, in case the receiver of the message wants to reply
		auto remote_channel = std::make_shared<nano::transport::inproc::channel> (destination, node);

		destination.inbound (*incoming, remote_channel);
	}

	if (callback)
	{
		boost::asio::post (node.io_ctx, [callback_l = std::move (callback), buffer_size = bytes->size ()] () {
			callback_l (boost::system::errc::make_error_code (boost::system::errc::success), buffer_size);
		});
	}

	return true;
}

std::string nano::transport::inproc::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}
