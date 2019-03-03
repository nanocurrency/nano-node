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

void nano::transport::udp_channels::add (std::shared_ptr<nano::transport::channel_udp> channel_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	channels.get<endpoint_tag> ().insert ({ channel_a });
}

void nano::transport::udp_channels::erase (nano::endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	channels.get<endpoint_tag> ().erase (endpoint_a);
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
	auto existing (channels.get<nano::transport::udp_channels::endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<nano::transport::udp_channels::endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<nano::transport::channel_udp>> nano::transport::udp_channels::random_set (size_t count_a) const
{
	std::unordered_set<std::shared_ptr<nano::transport::channel_udp>> result;
	result.reserve (count_a);
	std::lock_guard<std::mutex> lock (mutex);
	// Stop trying to fill result with random samples after this many attempts
	auto random_cutoff (count_a * 2);
	auto peers_size (channels.size ());
	// Usually count_a will be much smaller than peers.size()
	// Otherwise make sure we have a cutoff on attempting to randomly fill
	if (!channels.empty ())
	{
		for (auto i (0); i < random_cutoff && result.size () < count_a; ++i)
		{
			auto index (random_pool.GenerateWord32 (0, static_cast<CryptoPP::word32> (peers_size - 1)));
			result.insert (channels.get<random_access_tag> ()[index].channel);
		}
	}
	return result;
}

void nano::transport::udp_channels::random_fill (std::array<nano::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size ()));
	assert (peers.size () <= target_a.size ());
	auto endpoint (nano::endpoint (boost::asio::ip::address_v6{}, 0));
	assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		assert ((*i)->endpoint.address ().is_v6 ());
		assert (j < target_a.end ());
		*j = (*i)->endpoint;
	}
}
