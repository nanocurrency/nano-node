#include <rai/node/peers.hpp>

rai::endpoint rai::map_endpoint_to_v6 (rai::endpoint const & endpoint_a)
{
	auto endpoint_l (endpoint_a);
	if (endpoint_l.address ().is_v4 ())
	{
		endpoint_l = rai::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
	}
	return endpoint_l;
}

rai::peer_information::peer_information (rai::endpoint const & endpoint_a, unsigned network_version_a) :
endpoint (endpoint_a),
ip_address (endpoint_a.address ()),
last_contact (std::chrono::steady_clock::now ()),
last_attempt (last_contact),
last_bootstrap_attempt (std::chrono::steady_clock::time_point ()),
last_rep_request (std::chrono::steady_clock::time_point ()),
last_rep_response (std::chrono::steady_clock::time_point ()),
rep_weight (0),
network_version (network_version_a),
node_id ()
{
}

rai::peer_information::peer_information (rai::endpoint const & endpoint_a, std::chrono::steady_clock::time_point const & last_contact_a, std::chrono::steady_clock::time_point const & last_attempt_a) :
endpoint (endpoint_a),
ip_address (endpoint_a.address ()),
last_contact (last_contact_a),
last_attempt (last_attempt_a),
last_bootstrap_attempt (std::chrono::steady_clock::time_point ()),
last_rep_request (std::chrono::steady_clock::time_point ()),
last_rep_response (std::chrono::steady_clock::time_point ()),
rep_weight (0),
network_version (rai::protocol_version),
node_id ()
{
}

rai::peer_container::peer_container (rai::endpoint const & self_a) :
self (self_a),
legacy_peers (0),
peer_observer ([](rai::endpoint const &) {}),
disconnect_observer ([]() {})
{
}

bool rai::peer_container::contacted (rai::endpoint const & endpoint_a, unsigned version_a)
{
	auto endpoint_l (rai::map_endpoint_to_v6 (endpoint_a));
	auto should_handshake (false);
	if (version_a < rai::node_id_version)
	{
		insert (endpoint_l, version_a);
	}
	else if (!known_peer (endpoint_l))
	{
		std::lock_guard<std::mutex> lock (mutex);

		if (peers.get<rai::peer_by_ip_addr> ().count (endpoint_l.address ()) < max_peers_per_ip)
		{
			should_handshake = true;
		}
	}
	return should_handshake;
}

bool rai::peer_container::known_peer (rai::endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (peers.find (endpoint_a));
	return existing != peers.end ();
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::deque<rai::endpoint> rai::peer_container::list_fanout ()
{
	auto peers (random_set (size_sqrt ()));
	std::deque<rai::endpoint> result;
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		result.push_back (*i);
	}
	return result;
}

std::deque<rai::endpoint> rai::peer_container::list ()
{
	std::deque<rai::endpoint> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		result.push_back (i->endpoint);
	}
	std::random_shuffle (result.begin (), result.end ());
	return result;
}

std::map<rai::endpoint, unsigned> rai::peer_container::list_version ()
{
	std::map<rai::endpoint, unsigned> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		result.insert (std::pair<rai::endpoint, unsigned> (i->endpoint, i->network_version));
	}
	return result;
}

std::vector<rai::peer_information> rai::peer_container::list_vector ()
{
	std::vector<peer_information> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		result.push_back (*i);
	}
	std::random_shuffle (result.begin (), result.end ());
	return result;
}

rai::endpoint rai::peer_container::bootstrap_peer ()
{
	rai::endpoint result (boost::asio::ip::address_v6::any (), 0);
	std::lock_guard<std::mutex> lock (mutex);
	;
	for (auto i (peers.get<4> ().begin ()), n (peers.get<4> ().end ()); i != n;)
	{
		if (i->network_version >= protocol_version_reasonable_min)
		{
			result = i->endpoint;
			peers.get<4> ().modify (i, [](rai::peer_information & peer_a) {
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

boost::optional<rai::uint256_union> rai::peer_container::assign_syn_cookie (rai::endpoint const & endpoint)
{
	auto ip_addr (endpoint.address ());
	assert (ip_addr.is_v6 ());
	std::unique_lock<std::mutex> lock (syn_cookie_mutex);
	unsigned & ip_cookies = syn_cookies_per_ip[ip_addr];
	boost::optional<rai::uint256_union> result;
	if (ip_cookies < max_peers_per_ip)
	{
		if (syn_cookies.find (endpoint) == syn_cookies.end ())
		{
			rai::uint256_union query;
			random_pool.GenerateBlock (query.bytes.data (), query.bytes.size ());
			syn_cookie_info info{ query, std::chrono::steady_clock::now () };
			syn_cookies[endpoint] = info;
			++ip_cookies;
			result = query;
		}
	}
	return result;
}

bool rai::peer_container::validate_syn_cookie (rai::endpoint const & endpoint, rai::account node_id, rai::signature sig)
{
	auto ip_addr (endpoint.address ());
	assert (ip_addr.is_v6 ());
	std::unique_lock<std::mutex> lock (syn_cookie_mutex);
	auto result (true);
	auto cookie_it (syn_cookies.find (endpoint));
	if (cookie_it != syn_cookies.end () && !rai::validate_message (node_id, cookie_it->second.cookie, sig))
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

std::unordered_set<rai::endpoint> rai::peer_container::random_set (size_t count_a)
{
	std::unordered_set<rai::endpoint> result;
	result.reserve (count_a);
	std::lock_guard<std::mutex> lock (mutex);
	// Stop trying to fill result with random samples after this many attempts
	auto random_cutoff (count_a * 2);
	auto peers_size (peers.size ());
	// Usually count_a will be much smaller than peers.size()
	// Otherwise make sure we have a cutoff on attempting to randomly fill
	if (!peers.empty ())
	{
		for (auto i (0); i < random_cutoff && result.size () < count_a; ++i)
		{
			auto index (random_pool.GenerateWord32 (0, peers_size - 1));
			result.insert (peers.get<3> ()[index].endpoint);
		}
	}
	// Fill the remainder with most recent contact
	for (auto i (peers.get<1> ().begin ()), n (peers.get<1> ().end ()); i != n && result.size () < count_a; ++i)
	{
		result.insert (i->endpoint);
	}
	return result;
}

void rai::peer_container::random_fill (std::array<rai::endpoint, 8> & target_a)
{
	auto peers (random_set (target_a.size ()));
	assert (peers.size () <= target_a.size ());
	auto endpoint (rai::endpoint (boost::asio::ip::address_v6{}, 0));
	assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		assert (i->address ().is_v6 ());
		assert (j < target_a.end ());
		*j = *i;
	}
}

// Request a list of the top known representatives
std::vector<rai::peer_information> rai::peer_container::representatives (size_t count_a)
{
	std::vector<peer_information> result;
	result.reserve (std::min (count_a, size_t (16)));
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.get<6> ().begin ()), n (peers.get<6> ().end ()); i != n && result.size () < count_a; ++i)
	{
		if (!i->rep_weight.is_zero ())
		{
			result.push_back (*i);
		}
	}
	return result;
}

void rai::peer_container::purge_syn_cookies (std::chrono::steady_clock::time_point const & cutoff)
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

std::vector<rai::peer_information> rai::peer_container::purge_list (std::chrono::steady_clock::time_point const & cutoff)
{
	std::vector<rai::peer_information> result;
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto pivot (peers.get<1> ().lower_bound (cutoff));
		result.assign (pivot, peers.get<1> ().end ());
		for (auto i (peers.get<1> ().begin ()); i != pivot; ++i)
		{
			if (i->network_version < rai::node_id_version)
			{
				if (legacy_peers > 0)
				{
					--legacy_peers;
				}
				else
				{
					assert (false && "More legacy peers removed than added");
				}
			}
		}
		// Remove peers that haven't been heard from past the cutoff
		peers.get<1> ().erase (peers.get<1> ().begin (), pivot);
		for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
		{
			peers.modify (i, [](rai::peer_information & info) { info.last_attempt = std::chrono::steady_clock::now (); });
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

std::vector<rai::endpoint> rai::peer_container::rep_crawl ()
{
	std::vector<rai::endpoint> result;
	// If there is enough observed peers weight, crawl 10 peers. Otherwise - 40
	uint16_t max_count = (total_weight () > online_weight_minimum) ? 10 : 40;
	result.reserve (max_count);
	std::lock_guard<std::mutex> lock (mutex);
	uint16_t count (0);
	for (auto i (peers.get<5> ().begin ()), n (peers.get<5> ().end ()); i != n && count < max_count; ++i, ++count)
	{
		result.push_back (i->endpoint);
	};
	return result;
}

size_t rai::peer_container::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return peers.size ();
}

size_t rai::peer_container::size_sqrt ()
{
	auto result (std::ceil (std::sqrt (size ())));
	return result;
}

rai::uint128_t rai::peer_container::total_weight ()
{
	rai::uint128_t result (0);
	std::unordered_set<rai::account> probable_reps;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (peers.get<6> ().begin ()), n (peers.get<6> ().end ()); i != n; ++i)
	{
		// Calculate if representative isn't recorded for several IP addresses
		if (probable_reps.find (i->probable_rep_account) == probable_reps.end ())
		{
			result = result + i->rep_weight.number ();
			probable_reps.insert (i->probable_rep_account);
		}
	}
	return result;
}

bool rai::peer_container::empty ()
{
	return size () == 0;
}

bool rai::peer_container::not_a_peer (rai::endpoint const & endpoint_a, bool blacklist_loopback)
{
	bool result (false);
	if (endpoint_a.address ().to_v6 ().is_unspecified ())
	{
		result = true;
	}
	else if (rai::reserved_address (endpoint_a, blacklist_loopback))
	{
		result = true;
	}
	else if (endpoint_a == self)
	{
		result = true;
	}
	return result;
}

bool rai::peer_container::rep_response (rai::endpoint const & endpoint_a, rai::account const & rep_account_a, rai::amount const & weight_a)
{
	assert (endpoint_a.address ().is_v6 ());
	auto updated (false);
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (peers.find (endpoint_a));
	if (existing != peers.end ())
	{
		peers.modify (existing, [weight_a, &updated, rep_account_a](rai::peer_information & info) {
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

void rai::peer_container::rep_request (rai::endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (peers.find (endpoint_a));
	if (existing != peers.end ())
	{
		peers.modify (existing, [](rai::peer_information & info) {
			info.last_rep_request = std::chrono::steady_clock::now ();
		});
	}
}

bool rai::peer_container::reachout (rai::endpoint const & endpoint_a)
{
	// Don't contact invalid IPs
	bool error = not_a_peer (endpoint_a, false);
	if (!error)
	{
		auto endpoint_l (rai::map_endpoint_to_v6 (endpoint_a));
		// Don't keepalive to nodes that already sent us something
		error |= known_peer (endpoint_l);
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (attempts.find (endpoint_l));
		error |= existing != attempts.end ();
		attempts.insert ({ endpoint_l, std::chrono::steady_clock::now () });
	}
	return error;
}

bool rai::peer_container::insert (rai::endpoint const & endpoint_a, unsigned version_a)
{
	assert (endpoint_a.address ().is_v6 ());
	auto unknown (false);
	auto is_legacy (version_a < rai::node_id_version);
	auto result (not_a_peer (endpoint_a, false));
	if (!result)
	{
		if (version_a >= rai::protocol_version_min)
		{
			std::lock_guard<std::mutex> lock (mutex);
			auto existing (peers.find (endpoint_a));
			if (existing != peers.end ())
			{
				peers.modify (existing, [](rai::peer_information & info) {
					info.last_contact = std::chrono::steady_clock::now ();
					// Don't update `network_version` here unless you handle the legacy peer caps (both global and per IP)
					// You'd need to ensure that an upgrade from network version 7 to 8 entails a node ID handshake
				});
				result = true;
			}
			else
			{
				unknown = true;
				if (is_legacy)
				{
					if (legacy_peers < max_legacy_peers)
					{
						++legacy_peers;
					}
					else
					{
						result = true;
					}
				}
				if (!result && rai_network != rai_networks::rai_test_network)
				{
					auto peer_it_range (peers.get<rai::peer_by_ip_addr> ().equal_range (endpoint_a.address ()));
					auto i (peer_it_range.first);
					auto n (peer_it_range.second);
					unsigned ip_peers (0);
					unsigned legacy_ip_peers (0);
					while (i != n)
					{
						++ip_peers;
						if (i->network_version < rai::node_id_version)
						{
							++legacy_ip_peers;
						}
						++i;
					}
					if (ip_peers >= max_peers_per_ip || (is_legacy && legacy_ip_peers >= max_legacy_peers_per_ip))
					{
						result = true;
					}
				}
				if (!result)
				{
					peers.insert (rai::peer_information (endpoint_a, version_a));
				}
			}
		}
	}
	if (unknown && !result)
	{
		peer_observer (endpoint_a);
	}
	return result;
}
