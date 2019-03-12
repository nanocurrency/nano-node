#include <nano/node/peers.hpp>

#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/transport/udp.hpp>

nano::endpoint nano::map_endpoint_to_v6 (nano::endpoint const & endpoint_a)
{
	auto endpoint_l (endpoint_a);
	if (endpoint_l.address ().is_v4 ())
	{
		endpoint_l = nano::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
	}
	return endpoint_l;
}

nano::peer_information::peer_information (std::shared_ptr<nano::transport::channel_udp> sink_a) :
sink (sink_a)
{
}

boost::asio::ip::address nano::peer_information::ip_address () const
{
	return sink->endpoint.address ();
}

nano::endpoint nano::peer_information::endpoint () const
{
	return sink->endpoint;
}

std::reference_wrapper<nano::transport::channel const> nano::peer_information::sink_ref () const
{
	return *sink;
}

bool nano::peer_information::operator< (nano::peer_information const & peer_information_a) const
{
	return endpoint () < peer_information_a.endpoint ();
}

nano::peer_container::peer_container (nano::node & node_a) :
node (node_a),
peer_observer ([](std::shared_ptr<nano::transport::channel>) {})
{
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::deque<std::shared_ptr<nano::transport::channel_udp>> nano::peer_container::list_fanout ()
{
	auto peers (node.network.udp_channels.random_set (size_sqrt ()));
	std::deque<std::shared_ptr<nano::transport::channel_udp>> result;
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		result.push_back (*i);
	}
	return result;
}

std::vector<nano::peer_information> nano::peer_container::list_vector (size_t count_a)
{
	std::vector<peer_information> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		result.push_back (*i);
	}
	random_pool::shuffle (result.begin (), result.end ());
	if (result.size () > count_a)
	{
		result.resize (count_a, nano::peer_information (nullptr));
	}
	return result;
}

size_t nano::peer_container::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return peers.size ();
}

size_t nano::peer_container::size_sqrt ()
{
	return (static_cast<size_t> (std::ceil (std::sqrt (size ()))));
}

bool nano::peer_container::empty ()
{
	return size () == 0;
}

bool nano::peer_container::insert (nano::endpoint const & endpoint_a, unsigned version_a, bool allow_local_peers, boost::optional<nano::account> node_id_a)
{
	assert (endpoint_a.address ().is_v6 ());
	std::shared_ptr<nano::transport::channel_udp> new_peer;
	auto result (node.network.udp_channels.not_a_peer (endpoint_a, allow_local_peers));
	if (!result)
	{
		std::lock_guard<std::mutex> lock (mutex);
		nano::transport::channel_udp sink (node.network.udp_channels, endpoint_a);
		auto existing (peers.find (std::reference_wrapper<nano::transport::channel const> (sink)));
		if (existing != peers.end ())
		{
			result = true;
		}
		else
		{
			if (!result && !nano::is_test_network)
			{
				result = node.network.udp_channels.max_ip_connections (endpoint_a);
			}
			if (!result)
			{
				new_peer = std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, endpoint_a, version_a);
				peers.insert (nano::peer_information (new_peer));
				node.network.udp_channels.add (new_peer);
			}
		}
	}
	if (new_peer != nullptr)
	{
		peer_observer (new_peer);
	}
	return result;
}

void nano::peer_container::erase (nano::transport::channel const & channel_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	peers.get<sink_ref_tag> ().erase (channel_a);
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (peer_container & peer_container, const std::string & name)
{
	size_t peers_count = 0;
	{
		std::lock_guard<std::mutex> guard (peer_container.mutex);
		peers_count = peer_container.peers.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "peers", peers_count, sizeof (decltype (peer_container.peers)::value_type) }));

	return composite;
}
}
