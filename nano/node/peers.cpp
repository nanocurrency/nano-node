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

nano::peer_information::peer_information (std::shared_ptr<nano::transport::channel_udp> sink_a, unsigned network_version_a, boost::optional<nano::account> node_id_a) :
sink (sink_a),
last_contact (std::chrono::steady_clock::now ()),
last_attempt (last_contact),
network_version (network_version_a),
node_id (node_id_a)
{
}

nano::peer_information::peer_information (std::shared_ptr<nano::transport::channel_udp> sink_a, std::chrono::steady_clock::time_point const & last_contact_a, std::chrono::steady_clock::time_point const & last_attempt_a) :
sink (sink_a),
last_contact (last_contact_a),
last_attempt (last_attempt_a)
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

bool nano::peer_container::contacted (nano::endpoint const & endpoint_a, unsigned version_a)
{
	auto endpoint_l (nano::map_endpoint_to_v6 (endpoint_a));
	auto should_handshake (false);
	nano::transport::channel_udp sink (node, endpoint_l);
	if (version_a < nano::node_id_version)
	{
		insert (endpoint_l, version_a);
	}
	else if (node.network.udp_channels.channel (endpoint_a) == nullptr)
	{
		std::lock_guard<std::mutex> lock (mutex);

		if (peers.get<nano::peer_container::id_address_tag> ().count (endpoint_l.address ()) < max_peers_per_ip)
		{
			should_handshake = true;
		}
	}
	else
	{
		std::lock_guard<std::mutex> lock (mutex);
		nano::transport::channel_udp sink (node, endpoint_a);
		auto existing (peers.find (std::reference_wrapper<nano::transport::channel const> (sink)));
		if (existing != peers.end ())
		{
			peers.modify (existing, [](nano::peer_information & info) {
				info.last_contact = std::chrono::steady_clock::now ();
			});
		}
	}
	return should_handshake;
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
	random_pool.Shuffle (result.begin (), result.end ());
	while (result.size () > count_a)
	{
		result.pop_back ();
	}
	return result;
}

nano::endpoint nano::peer_container::bootstrap_peer ()
{
	nano::endpoint result (boost::asio::ip::address_v6::any (), 0);
	std::lock_guard<std::mutex> lock (mutex);
	;
	for (auto i (peers.get<nano::peer_container::last_bootstrap_attempt_tag> ().begin ()), n (peers.get<nano::peer_container::last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->network_version >= protocol_version_reasonable_min)
		{
			result = i->endpoint ();
			peers.get<nano::peer_container::last_bootstrap_attempt_tag> ().modify (i, [](nano::peer_information & peer_a) {
				peer_a.last_bootstrap_attempt = std::chrono::steady_clock::now ();
			});
			i = n;
		}
		else
		{
			++i;
		}
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
	if (ip_cookies < max_peers_per_ip)
	{
		if (syn_cookies.find (endpoint) == syn_cookies.end ())
		{
			nano::uint256_union query;
			random_pool.GenerateBlock (query.bytes.data (), query.bytes.size ());
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

// Request a list of the top known representatives
std::vector<nano::peer_information> nano::peer_container::representatives (size_t count_a)
{
	std::vector<peer_information> result;
	result.reserve (std::min (count_a, size_t (16)));
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.get<nano::peer_container::rep_weight_tag> ().begin ()), n (peers.get<nano::peer_container::rep_weight_tag> ().end ()); i != n && result.size () < count_a; ++i)
	{
		if (!i->rep_weight.is_zero ())
		{
			result.push_back (*i);
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

std::vector<nano::peer_information> nano::peer_container::purge_list (std::chrono::steady_clock::time_point const & cutoff)
{
	std::vector<nano::peer_information> result;
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto pivot (peers.get<nano::peer_container::last_contact_tag> ().lower_bound (cutoff));
		result.assign (pivot, peers.get<nano::peer_container::last_contact_tag> ().end ());
		// Remove peers that haven't been heard from past the cutoff
		for (auto i (peers.get<nano::peer_container::last_contact_tag> ().begin ()); i != pivot; ++i)
		{
			node.network.udp_channels.erase (i->endpoint ());
		}
		peers.get<nano::peer_container::last_contact_tag> ().erase (peers.get<nano::peer_container::last_contact_tag> ().begin (), pivot);
		for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
		{
			peers.modify (i, [](nano::peer_information & info) { info.last_attempt = std::chrono::steady_clock::now (); });
		}

		// Remove keepalive attempt tracking for attempts older than cutoff
		auto attempts_pivot (attempts.get<1> ().lower_bound (cutoff));
		attempts.get<1> ().erase (attempts.get<1> ().begin (), attempts_pivot);
	}
	if (result.empty ())
	{
		disconnect_observer ();
	}
	return result;
}

std::vector<std::shared_ptr<nano::transport::channel>> nano::peer_container::rep_crawl ()
{
	std::vector<std::shared_ptr<nano::transport::channel>> result;
	// If there is enough observed peers weight, crawl 10 peers. Otherwise - 40
	uint16_t max_count = (total_weight () > online_weight_minimum) ? 10 : 40;
	result.reserve (max_count);
	std::lock_guard<std::mutex> lock (mutex);
	uint16_t count (0);
	for (auto i (peers.get<nano::peer_container::last_rep_request_tag> ().begin ()), n (peers.get<nano::peer_container::last_rep_request_tag> ().end ()); i != n && count < max_count; ++i, ++count)
	{
		result.push_back (i->sink);
	};
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

std::vector<nano::peer_information> nano::peer_container::list_probable_rep_weights ()
{
	std::vector<nano::peer_information> result;
	std::unordered_set<nano::account> probable_reps;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.get<nano::peer_container::rep_weight_tag> ().begin ()), n (peers.get<nano::peer_container::rep_weight_tag> ().end ()); i != n; ++i)
	{
		// Calculate if representative isn't recorded for several IP addresses
		if (probable_reps.find (i->probable_rep_account) == probable_reps.end ())
		{
			if (!i->rep_weight.number ().is_zero ())
			{
				result.push_back (*i);
			}
			probable_reps.insert (i->probable_rep_account);
		}
	}
	return result;
}

nano::uint128_t nano::peer_container::total_weight ()
{
	nano::uint128_t result (0);
	for (auto & entry : list_probable_rep_weights ())
	{
		result = result + entry.rep_weight.number ();
	}
	return result;
}

bool nano::peer_container::empty ()
{
	return size () == 0;
}

bool nano::peer_container::not_a_peer (nano::endpoint const & endpoint_a)
{
	bool result (false);
	if (endpoint_a.address ().to_v6 ().is_unspecified ())
	{
		result = true;
	}
	else if (nano::reserved_address (endpoint_a))
	{
		result = true;
	}
	else if (endpoint_a == node.network.endpoint ())
	{
		result = true;
	}
	return result;
}

bool nano::peer_container::rep_response (nano::transport::channel const & sink_a, nano::account const & rep_account_a, nano::amount const & weight_a)
{
	auto updated (false);
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (peers.find (std::reference_wrapper<nano::transport::channel const> (sink_a)));
	if (existing != peers.end ())
	{
		peers.modify (existing, [weight_a, &updated, rep_account_a](nano::peer_information & info) {
			info.last_rep_response = std::chrono::steady_clock::now ();
			if (info.rep_weight < weight_a)
			{
				updated = true;
				info.rep_weight = weight_a;
				info.probable_rep_account = rep_account_a;
			}
		});
	}
	return updated;
}

void nano::peer_container::rep_request (nano::transport::channel const & sink_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (peers.find (std::reference_wrapper<nano::transport::channel const> (sink_a)));
	if (existing != peers.end ())
	{
		peers.modify (existing, [](nano::peer_information & info) {
			info.last_rep_request = std::chrono::steady_clock::now ();
		});
	}
}

bool nano::peer_container::reachout (nano::endpoint const & endpoint_a)
{
	// Don't contact invalid IPs
	bool error = not_a_peer (endpoint_a);
	if (!error)
	{
		auto endpoint_l (nano::map_endpoint_to_v6 (endpoint_a));
		// Don't keepalive to nodes that already sent us something
		nano::transport::channel_udp sink (node, endpoint_l);
		error |= node.network.udp_channels.channel (endpoint_l) != nullptr;
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (attempts.find (endpoint_l));
		error |= existing != attempts.end ();
		attempts.insert ({ endpoint_l, std::chrono::steady_clock::now () });
	}
	return error;
}

bool nano::peer_container::insert (nano::endpoint const & endpoint_a, unsigned version_a, bool preconfigured_a, boost::optional<nano::account> node_id_a)
{
	assert (endpoint_a.address ().is_v6 ());
	std::shared_ptr<nano::transport::channel_udp> new_peer;
	auto result (!preconfigured_a && not_a_peer (endpoint_a));
	if (!result)
	{
		if (version_a >= nano::protocol_version_min)
		{
			std::lock_guard<std::mutex> lock (mutex);
			nano::transport::channel_udp sink (node, endpoint_a);
			auto existing (peers.find (std::reference_wrapper<nano::transport::channel const> (sink)));
			if (existing != peers.end ())
			{
				peers.modify (existing, [node_id_a](nano::peer_information & info) {
					info.last_contact = std::chrono::steady_clock::now ();
					if (node_id_a.is_initialized ())
					{
						info.node_id = node_id_a;
					}
				});
				result = true;
			}
			else
			{
				if (!result && !nano::is_test_network)
				{
					auto ip_peers (peers.get<nano::peer_container::id_address_tag> ().count (endpoint_a.address ()));
					if (ip_peers >= max_peers_per_ip)
					{
						result = true;
					}
				}
				if (!result)
				{
					new_peer = std::make_shared<nano::transport::channel_udp> (node, endpoint_a);
					peers.insert (nano::peer_information (new_peer, version_a, node_id_a));
					node.network.udp_channels.add (new_peer);
				}
			}
		}
	}
	if (new_peer != nullptr)
	{
		peer_observer (new_peer);
	}
	return result;
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
