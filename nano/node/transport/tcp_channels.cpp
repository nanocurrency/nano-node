#include <celerix/node/node.hpp>
#include <celerix/node/transport/tcp_channels.hpp>

/*
 * tcp_channels
 */

celerix::transport::tcp_channels::tcp_channels (celerix::node & node) :
	node{ node }
{
}

celerix::transport::tcp_channels::~tcp_channels ()
{
	debug_assert (channels.empty ());
}

void celerix::transport::tcp_channels::start ()
{
}

void celerix::transport::tcp_channels::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	close ();
}

void celerix::transport::tcp_channels::close ()
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	for (auto const & entry : channels)
	{
		entry.socket->close ();
		entry.server->stop ();
		entry.channel->close ();
	}

	channels.clear ();
}

bool celerix::transport::tcp_channels::check (const celerix::tcp_endpoint & endpoint, const celerix::account & node_id) const
{
	debug_assert (!mutex.try_lock ());

	if (stopped)
	{
		return false; // Reject
	}

	if (node.network.not_a_peer (celerix::transport::map_tcp_to_endpoint (endpoint), node.config.allow_local_peers))
	{
		node.stats.inc (celerix::stat::type::tcp_channels_rejected, celerix::stat::detail::not_a_peer);
		node.logger.debug (celerix::log::type::tcp_channels, "Rejected invalid endpoint channel: {}", fmt::streamed (endpoint));

		return false; // Reject
	}

	bool has_duplicate = std::any_of (channels.begin (), channels.end (), [&endpoint, &node_id] (auto const & channel) {
		if (celerix::transport::is_same_ip (channel.endpoint ().address (), endpoint.address ()))
		{
			// Only counsider channels with the same node id as duplicates if they come from the same IP
			if (channel.node_id () == node_id)
			{
				return true;
			}
		}
		return false;
	});

	if (has_duplicate)
	{
		node.stats.inc (celerix::stat::type::tcp_channels_rejected, celerix::stat::detail::channel_duplicate);
		node.logger.debug (celerix::log::type::tcp_channels, "Rejected duplicate channel: {} ({})", fmt::streamed (endpoint), node_id.to_node_id ());

		return false; // Reject
	}

	return true; // OK
}

// This should be the only place in node where channels are created
std::shared_ptr<celerix::transport::tcp_channel> celerix::transport::tcp_channels::create (const std::shared_ptr<celerix::transport::tcp_socket> & socket, const std::shared_ptr<celerix::transport::tcp_server> & server, const celerix::account & node_id)
{
	auto const endpoint = socket->remote_endpoint ();
	debug_assert (endpoint.address ().is_v6 ());

	celerix::unique_lock<celerix::mutex> lock{ mutex };

	if (stopped)
	{
		return nullptr;
	}

	if (!check (endpoint, node_id))
	{
		node.stats.inc (celerix::stat::type::tcp_channels, celerix::stat::detail::channel_rejected);
		node.logger.debug (celerix::log::type::tcp_channels, "Rejected channel: {} ({})", fmt::streamed (endpoint), node_id.to_node_id ());
		// Rejection reason should be logged earlier

		return nullptr;
	}

	node.stats.inc (celerix::stat::type::tcp_channels, celerix::stat::detail::channel_accepted);
	node.logger.debug (celerix::log::type::tcp_channels, "Accepted channel: {} ({}) ({})",
	fmt::streamed (socket->remote_endpoint ()),
	to_string (socket->endpoint_type ()),
	node_id.to_node_id ());

	auto channel = std::make_shared<celerix::transport::tcp_channel> (node, socket);
	channel->set_node_id (node_id);

	attempts.get<endpoint_tag> ().erase (endpoint);

	auto [_, inserted] = channels.get<endpoint_tag> ().emplace (channel, socket, server);
	debug_assert (inserted);

	lock.unlock ();

	node.observers.channel_connected.notify (channel);

	return channel;
}

void celerix::transport::tcp_channels::erase (celerix::tcp_endpoint const & endpoint_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	channels.get<endpoint_tag> ().erase (endpoint_a);
}

std::size_t celerix::transport::tcp_channels::size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return channels.size ();
}

std::shared_ptr<celerix::transport::tcp_channel> celerix::transport::tcp_channels::find_channel (celerix::tcp_endpoint const & endpoint_a) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	std::shared_ptr<celerix::transport::tcp_channel> result;
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<celerix::transport::channel>> celerix::transport::tcp_channels::random_set (std::size_t count_a, uint8_t min_version) const
{
	std::unordered_set<std::shared_ptr<celerix::transport::channel>> result;
	result.reserve (count_a);
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	// Stop trying to fill result with random samples after this many attempts
	auto random_cutoff (count_a * 2);
	// Usually count_a will be much smaller than peers.size()
	// Otherwise make sure we have a cutoff on attempting to randomly fill
	if (!channels.empty ())
	{
		for (auto i (0); i < random_cutoff && result.size () < count_a; ++i)
		{
			auto index = rng.random (channels.size ());
			auto channel = channels.get<random_access_tag> ()[index].channel;
			if (!channel->alive ())
			{
				continue;
			}

			if (channel->get_network_version () >= min_version)
			{
				result.insert (channel);
			}
		}
	}
	return result;
}

void celerix::transport::tcp_channels::random_fill (std::array<celerix::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size ()));
	debug_assert (peers.size () <= target_a.size ());
	auto endpoint (celerix::endpoint (boost::asio::ip::address_v6{}, 0));
	debug_assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		debug_assert ((*i)->get_remote_endpoint ().address ().is_v6 ());
		debug_assert (j < target_a.end ());
		*j = (*i)->get_remote_endpoint ();
	}
}

std::shared_ptr<celerix::transport::tcp_channel> celerix::transport::tcp_channels::find_node_id (celerix::account const & node_id_a)
{
	std::shared_ptr<celerix::transport::tcp_channel> result;
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto existing (channels.get<node_id_tag> ().find (node_id_a));
	if (existing != channels.get<node_id_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

celerix::tcp_endpoint celerix::transport::tcp_channels::bootstrap_peer ()
{
	celerix::tcp_endpoint result (boost::asio::ip::address_v6::any (), 0);
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= node.network_params.network.protocol_version_min)
		{
			result = celerix::transport::map_endpoint_to_tcp (i->channel->get_peering_endpoint ());
			channels.get<last_bootstrap_attempt_tag> ().modify (i, [] (channel_entry & wrapper_a) {
				wrapper_a.channel->set_last_bootstrap_attempt (std::chrono::steady_clock::now ());
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

bool celerix::transport::tcp_channels::max_ip_connections (celerix::tcp_endpoint const & endpoint_a)
{
	if (node.flags.disable_max_peers_per_ip)
	{
		return false;
	}
	bool result{ false };
	auto const address (celerix::transport::ipv4_address_or_ipv6_subnet (endpoint_a.address ()));
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	result = channels.get<ip_address_tag> ().count (address) >= node.config.network.max_peers_per_ip;
	if (!result)
	{
		result = attempts.get<ip_address_tag> ().count (address) >= node.config.network.max_peers_per_ip;
	}
	if (result)
	{
		node.stats.inc (celerix::stat::type::tcp, celerix::stat::detail::max_per_ip, celerix::stat::dir::out);
	}
	return result;
}

bool celerix::transport::tcp_channels::max_subnetwork_connections (celerix::tcp_endpoint const & endpoint_a)
{
	if (node.flags.disable_max_peers_per_subnetwork)
	{
		return false;
	}
	bool result{ false };
	auto const subnet (celerix::transport::map_address_to_subnetwork (endpoint_a.address ()));
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	result = channels.get<subnetwork_tag> ().count (subnet) >= node.config.network.max_peers_per_subnetwork;
	if (!result)
	{
		result = attempts.get<subnetwork_tag> ().count (subnet) >= node.config.network.max_peers_per_subnetwork;
	}
	if (result)
	{
		node.stats.inc (celerix::stat::type::tcp, celerix::stat::detail::max_per_subnetwork, celerix::stat::dir::out);
	}
	return result;
}

bool celerix::transport::tcp_channels::max_ip_or_subnetwork_connections (celerix::tcp_endpoint const & endpoint_a)
{
	return max_ip_connections (endpoint_a) || max_subnetwork_connections (endpoint_a);
}

bool celerix::transport::tcp_channels::track_reachout (celerix::endpoint const & endpoint_a)
{
	auto const tcp_endpoint = celerix::transport::map_endpoint_to_tcp (endpoint_a);

	// Don't overload single IP
	if (max_ip_or_subnetwork_connections (tcp_endpoint))
	{
		return false;
	}
	if (node.network.excluded_peers.check (tcp_endpoint))
	{
		return false;
	}
	if (node.flags.disable_tcp_realtime)
	{
		return false;
	}

	// Don't keepalive to nodes that already sent us something
	if (find_channel (tcp_endpoint) != nullptr)
	{
		return false;
	}

	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto [it, inserted] = attempts.emplace (tcp_endpoint);
	return inserted;
}

void celerix::transport::tcp_channels::purge (std::chrono::steady_clock::time_point cutoff_deadline)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	auto should_close = [this, cutoff_deadline] (auto const & channel) {
		// Remove channels that haven't successfully sent a message within the cutoff time
		if (auto last = channel->get_last_packet_sent (); last < cutoff_deadline)
		{
			node.stats.inc (celerix::stat::type::tcp_channels_purge, celerix::stat::detail::idle);
			node.logger.debug (celerix::log::type::tcp_channels, "Closing idle channel: {} (idle for {}s)",
			channel->to_string (),
			celerix::log::seconds_delta (last));

			return true; // Close
		}
		// Check if any tcp channels belonging to old protocol versions which may still be alive due to async operations
		if (channel->get_network_version () < node.network_params.network.protocol_version_min)
		{
			node.stats.inc (celerix::stat::type::tcp_channels_purge, celerix::stat::detail::outdated);
			node.logger.debug (celerix::log::type::tcp_channels, "Closing channel with old protocol version: {}", channel->to_string ());

			return true; // Close
		}
		return false;
	};

	for (auto const & entry : channels)
	{
		if (should_close (entry.channel))
		{
			entry.channel->close ();
		}
	}

	erase_if (channels, [this] (auto const & entry) {
		if (!entry.channel->alive ())
		{
			node.logger.debug (celerix::log::type::tcp_channels, "Removing dead channel: {}", entry.channel->to_string ());
			entry.channel->close ();
			return true; // Erase
		}
		return false;
	});

	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<last_attempt_tag> ().lower_bound (cutoff_deadline));
	attempts.get<last_attempt_tag> ().erase (attempts.get<last_attempt_tag> ().begin (), attempts_cutoff);
}

void celerix::transport::tcp_channels::keepalive ()
{
	celerix::keepalive message{ node.network_params.network };
	node.network.random_fill (message.peers);

	celerix::unique_lock<celerix::mutex> lock{ mutex };

	auto const cutoff_time = std::chrono::steady_clock::now () - node.network_params.network.keepalive_period;

	// Wake up channels
	std::vector<std::shared_ptr<celerix::transport::tcp_channel>> to_wakeup;
	for (auto const & entry : channels)
	{
		if (entry.channel->get_last_packet_sent () < cutoff_time)
		{
			to_wakeup.push_back (entry.channel);
		}
	}

	lock.unlock ();

	for (auto & channel : to_wakeup)
	{
		channel->send (message, celerix::transport::traffic_type::keepalive);
	}
}

std::optional<celerix::keepalive> celerix::transport::tcp_channels::sample_keepalive ()
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	size_t counter = 0;
	while (counter++ < channels.size ())
	{
		auto index = rng.random (channels.size ());
		if (auto server = channels.get<random_access_tag> ()[index].server)
		{
			if (auto keepalive = server->pop_last_keepalive ())
			{
				return keepalive;
			}
		}
	}

	return std::nullopt;
}

std::deque<std::shared_ptr<celerix::transport::channel>> celerix::transport::tcp_channels::list (uint8_t minimum_version) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	std::deque<std::shared_ptr<celerix::transport::channel>> result;
	for (auto const & entry : channels)
	{
		if (entry.channel->get_network_version () >= minimum_version)
		{
			result.push_back (entry.channel);
		}
	}
	return result;
}

bool celerix::transport::tcp_channels::start_tcp (celerix::endpoint const & endpoint)
{
	return node.tcp_listener.connect (endpoint.address (), endpoint.port ());
}

celerix::container_info celerix::transport::tcp_channels::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("channels", channels.size ());
	info.put ("attempts", attempts.size ());
	return info;
}