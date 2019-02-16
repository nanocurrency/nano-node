#include <nano/node/node.hpp>
#include <nano/node/transport/udp.hpp>

nano::message_sink_udp::message_sink_udp (nano::node & node_a, nano::endpoint const & endpoint_a) :
node (node_a),
endpoint (endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
}

size_t nano::message_sink_udp::hash_code () const
{
	std::hash<::nano::endpoint> hash;
	return hash (endpoint);
}

bool nano::message_sink_udp::operator== (nano::message_sink const & other_a) const
{
	bool result (false);
	auto other_l (dynamic_cast<nano::message_sink_udp const *> (&other_a));
	if (other_l != nullptr)
	{
		return *this == *other_l;
	}
	return result;
}

void nano::message_sink_udp::send_buffer_raw (uint8_t const * data_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a) const
{
	node.network.socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint, callback_a);
}

std::function<void(boost::system::error_code const &, size_t)> nano::message_sink_udp::callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a) const
{
	return [ buffer_a, node = std::weak_ptr<nano::node> (node.shared ()), detail_a ](boost::system::error_code const & ec, size_t size_a)
	{
		if (auto node_l = node.lock ())
		{
			if (ec == boost::system::errc::host_unreachable)
			{
				node_l->stats.inc (nano::stat::type::error, nano::stat::detail::unreachable_host, nano::stat::dir::out);
			}
			if (!ec)
			{
				node_l->stats.add (nano::stat::type::traffic, nano::stat::dir::out, size_a);
				node_l->stats.inc (nano::stat::type::message, detail_a, nano::stat::dir::out);
			}
		}
	};
}

std::string nano::message_sink_udp::to_string () const
{
	return boost::str (boost::format ("UDP: %1%") % endpoint);
}
