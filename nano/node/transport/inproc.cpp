#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/transport/message_deserializer.hpp>

#include <boost/format.hpp>

nano::transport::inproc::channel::channel (nano::node & node, nano::node & destination) :
	transport::channel{ node.shared () },
	destination{ destination },
	endpoint{ node.network.endpoint () }
{
	set_node_id (node.node_id.pub);
	set_network_version (node.network_params.network.protocol_version);
}

/**
 * Send the buffer to the peer and call the callback function when done. The call never fails.
 * Note that the inbound message visitor will be called before the callback because it is called directly whereas the callback is spawned in the background.
 */
void nano::transport::inproc::channel::send_buffer (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a, nano::transport::buffer_drop_policy drop_policy_a, nano::transport::traffic_type traffic_type)
{
	std::size_t offset{ 0 };
	auto const buffer_read_fn = [&offset, buffer_v = buffer_a.to_bytes ()] (std::shared_ptr<std::vector<uint8_t>> const & data_a, std::size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a) {
		debug_assert (buffer_v.size () >= (offset + size_a));
		data_a->resize (size_a);
		auto const copy_start = buffer_v.begin () + offset;
		std::copy (copy_start, copy_start + size_a, data_a->data ());
		offset += size_a;
		callback_a (boost::system::errc::make_error_code (boost::system::errc::success), size_a);
	};

	auto const message_deserializer = std::make_shared<nano::transport::message_deserializer> (node.network_params.network, node.network.filter, node.block_uniquer, node.vote_uniquer, buffer_read_fn);
	message_deserializer->read (
	[this] (boost::system::error_code ec_a, std::unique_ptr<nano::message> message_a) {
		if (ec_a || !message_a)
		{
			return;
		}

		// we create a temporary channel for the reply path, in case the receiver of the message wants to reply
		auto remote_channel = std::make_shared<nano::transport::inproc::channel> (destination, node);

		// process message
		{
			node.stats.inc (nano::stat::type::message, to_stat_detail (message_a->type ()), nano::stat::dir::in);
			destination.network.inbound (*message_a, remote_channel);
		}
	});

	if (callback_a)
	{
		node.background ([callback_l = std::move (callback_a), buffer_size = buffer_a.size ()] () {
			callback_l (boost::system::errc::make_error_code (boost::system::errc::success), buffer_size);
		});
	}
}

std::string nano::transport::inproc::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}
