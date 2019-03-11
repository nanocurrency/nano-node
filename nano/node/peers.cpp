#include <nano/node/peers.hpp>

#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/transport/udp.hpp>

std::chrono::seconds constexpr nano::peer_container::period;
std::chrono::seconds constexpr nano::peer_container::cutoff;

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
sink (sink_a),
last_contact (std::chrono::steady_clock::now ())
{
}

nano::peer_information::peer_information (std::shared_ptr<nano::transport::channel_udp> sink_a, std::chrono::steady_clock::time_point const & last_contact_a) :
sink (sink_a),
last_contact (last_contact_a)
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
peer_observer ([](std::shared_ptr<nano::transport::channel>) {}),
disconnect_observer ([]() {})
{
}

void nano::peer_container::contacted (nano::endpoint const & endpoint_a)
{
	auto endpoint_l (nano::map_endpoint_to_v6 (endpoint_a));
	nano::transport::channel_udp sink (node.network.udp_channels, endpoint_l);
	auto channel (node.network.udp_channels.channel (endpoint_a));
	if (channel != nullptr)
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (peers.find (std::reference_wrapper<nano::transport::channel const> (*channel)));
		if (existing != peers.end ())
		{
			peers.modify (existing, [](nano::peer_information & info) {
				info.last_contact = std::chrono::steady_clock::now ();
			});
		}
	}
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

boost::optional<nano::uint256_union> nano::peer_container::assign_syn_cookie (nano::endpoint const & endpoint)
{
	auto ip_addr (endpoint.address ());
	assert (ip_addr.is_v6 ());
	std::unique_lock<std::mutex> lock (syn_cookie_mutex);
	unsigned & ip_cookies = syn_cookies_per_ip[ip_addr];
	boost::optional<nano::uint256_union> result;
	if (ip_cookies < nano::transport::udp_channels::max_peers_per_ip)
	{
		if (syn_cookies.find (endpoint) == syn_cookies.end ())
		{
			nano::uint256_union query;
			random_pool::generate_block (query.bytes.data (), query.bytes.size ());
			syn_cookie_info info{ query, std::chrono::steady_clock::now () };
			syn_cookies[endpoint] = info;
			++ip_cookies;
			result = query;
		}
	}
	return result;
}

bool nano::peer_container::validate_syn_cookie (nano::endpoint const & endpoint, nano::account node_id, nano::signature sig)
{
	auto ip_addr (endpoint.address ());
	assert (ip_addr.is_v6 ());
	std::unique_lock<std::mutex> lock (syn_cookie_mutex);
	auto result (true);
	auto cookie_it (syn_cookies.find (endpoint));
	if (cookie_it != syn_cookies.end () && !nano::validate_message (node_id, cookie_it->second.cookie, sig))
	{
		result = false;
		syn_cookies.erase (cookie_it);
		unsigned & ip_cookies = syn_cookies_per_ip[ip_addr];
		if (ip_cookies > 0)
		{
			--ip_cookies;
		}
		else
		{
			assert (false && "More SYN cookies deleted than created for IP");
		}
	}
	return result;
}

void nano::peer_container::purge_syn_cookies (std::chrono::steady_clock::time_point const & cutoff)
{
	std::lock_guard<std::mutex> lock (syn_cookie_mutex);
	auto it (syn_cookies.begin ());
	while (it != syn_cookies.end ())
	{
		auto info (it->second);
		if (info.created_at < cutoff)
		{
			unsigned & per_ip = syn_cookies_per_ip[it->first.address ()];
			if (per_ip > 0)
			{
				--per_ip;
			}
			else
			{
				assert (false && "More SYN cookies deleted than created for IP");
			}
			it = syn_cookies.erase (it);
		}
		else
		{
			++it;
		}
	}
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

bool nano::peer_container::reachout (nano::endpoint const & endpoint_a, bool allow_local_peers)
{
	// Don't contact invalid IPs
	bool error = node.network.udp_channels.not_a_peer (endpoint_a, allow_local_peers);
	if (!error)
	{
		auto endpoint_l (nano::map_endpoint_to_v6 (endpoint_a));
		// Don't keepalive to nodes that already sent us something
		nano::transport::channel_udp sink (node.network.udp_channels, endpoint_l);
		error |= node.network.udp_channels.channel (endpoint_l) != nullptr;
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (attempts.find (endpoint_l));
		error |= existing != attempts.end ();
		attempts.insert ({ endpoint_l, std::chrono::steady_clock::now () });
	}
	return error;
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

void nano::peer_container::purge_list (std::chrono::steady_clock::time_point const & cutoff_a)
{
	bool disconnected;
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto disconnect_cutoff (peers.get<last_contact_tag> ().lower_bound (cutoff_a));
		// Remove peers that haven't been heard from past the cutoff
		for (auto i (peers.get<last_contact_tag> ().begin ()); i != disconnect_cutoff; ++i)
		{
			node.network.udp_channels.erase (i->endpoint ());
		}
		peers.get<last_contact_tag> ().erase (peers.get<last_contact_tag> ().begin (), disconnect_cutoff);

		// Remove keepalive attempt tracking for attempts older than cutoff
		auto attempts_cutoff (attempts.get<1> ().lower_bound (cutoff_a));
		attempts.get<1> ().erase (attempts.get<1> ().begin (), attempts_cutoff);
		disconnected = peers.empty ();
	}
	if (disconnected)
	{
		disconnect_observer ();
	}
}

void nano::peer_container::ongoing_keepalive ()
{
	purge_list (std::chrono::steady_clock::now () - cutoff);
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto keepalive_cutoff (peers.get<last_contact_tag> ().lower_bound (std::chrono::steady_clock::now () - period));
		if (keepalive_cutoff == peers.get<last_contact_tag> ().end ())
		{
			disconnect_observer ();
		}
		for (auto i (peers.get<last_contact_tag> ().begin ()); i != keepalive_cutoff; ++i)
		{
			node.network.send_keepalive (*i->sink);
		}
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->peers.ongoing_keepalive ();
		}
	});
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (peer_container & peer_container, const std::string & name)
{
	size_t peers_count = 0;
	size_t attemps_count = 0;
	{
		std::lock_guard<std::mutex> guard (peer_container.mutex);
		peers_count = peer_container.peers.size ();
		attemps_count = peer_container.attempts.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "peers", peers_count, sizeof (decltype (peer_container.peers)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "attempts", attemps_count, sizeof (decltype (peer_container.attempts)::value_type) }));

	size_t syn_cookies_count = 0;
	size_t syn_cookies_per_ip_count = 0;
	{
		std::lock_guard<std::mutex> guard (peer_container.syn_cookie_mutex);
		syn_cookies_count = peer_container.syn_cookies.size ();
		syn_cookies_per_ip_count = peer_container.syn_cookies_per_ip.size ();
	}

	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "syn_cookies", syn_cookies_count, sizeof (decltype (peer_container.syn_cookies)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "syn_cookies_per_ip", syn_cookies_per_ip_count, sizeof (decltype (peer_container.syn_cookies_per_ip)::value_type) }));
	return composite;
}
}
