#include <nano/node/node.hpp>
#include <nano/node/transport/udp.hpp>

nano::transport::channel_udp::channel_udp (nano::node & node_a, nano::endpoint const & endpoint_a) :
node (node_a),
endpoint (endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
}

size_t nano::transport::channel_udp::hash_code () const
{
	std::hash<::nano::endpoint> hash;
	return hash (endpoint);
}

bool nano::transport::channel_udp::operator== (nano::transport::channel const & other_a) const
{
	bool result (false);
	auto other_l (dynamic_cast<nano::transport::channel_udp const *> (&other_a));
	if (other_l != nullptr)
	{
		return *this == *other_l;
	}
	return result;
}

void nano::transport::channel_udp::send_buffer_raw (boost::asio::const_buffer buffer_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	node.network.socket.async_send_to (buffer_a, endpoint, callback_a);
}

std::function<void(boost::system::error_code const &, size_t)> nano::transport::channel_udp::callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	return [ buffer_a, node = std::weak_ptr<nano::node> (node.shared ()), detail_a, callback_a ](boost::system::error_code const & ec, size_t size_a)
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
				if (callback_a)
				{
					callback_a (ec, size_a);
				}
			}
		}
	};
}

std::string nano::transport::channel_udp::to_string () const
{
	return boost::str (boost::format ("UDP: %1%") % endpoint);
}

void nano::transport::udp_channels::add (nano::endpoint const & endpoint_a, std::shared_ptr<nano::transport::channel_udp> channel_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	channels.insert (std::make_pair (endpoint_a, channel_a));
}

void nano::transport::udp_channels::erase (nano::endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	channels.erase (endpoint_a);
}

size_t nano::transport::udp_channels::size () const
{
	std::lock_guard<std::mutex> lock (mutex);
	return channels.size ();
}

std::shared_ptr<nano::transport::channel_udp> nano::transport::udp_channels::channel (nano::endpoint const & endpoint_a) const
{
	std::lock_guard<std::mutex> lock (mutex);
	std::shared_ptr<nano::transport::channel_udp> result;
	auto existing (channels.find (endpoint_a));
	if (existing != channels.end ())
	{
		result = existing->second;
	}
	return result;
}
