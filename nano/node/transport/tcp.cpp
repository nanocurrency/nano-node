#include <nano/lib/config.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/message_deserializer.hpp>
#include <nano/node/transport/tcp.hpp>

#include <boost/format.hpp>

/*
 * channel_tcp
 */

nano::transport::channel_tcp::channel_tcp (nano::node & node_a, std::weak_ptr<nano::transport::socket> socket_a) :
	channel (node_a),
	socket (std::move (socket_a))
{
}

nano::transport::channel_tcp::~channel_tcp ()
{
	nano::lock_guard<nano::mutex> lk{ channel_mutex };
	// Close socket. Exception: socket is used by tcp_server
	if (auto socket_l = socket.lock ())
	{
		if (!temporary)
		{
			socket_l->close ();
		}
	}
}

void nano::transport::channel_tcp::send_buffer (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a, nano::transport::buffer_drop_policy policy_a, nano::transport::traffic_type traffic_type)
{
	if (auto socket_l = socket.lock ())
	{
		if (!socket_l->max (traffic_type) || (policy_a == nano::transport::buffer_drop_policy::no_socket_drop && !socket_l->full (traffic_type)))
		{
			socket_l->async_write (
			buffer_a, [this_s = shared_from_this (), endpoint_a = socket_l->remote_endpoint (), node = std::weak_ptr<nano::node>{ node.shared () }, callback_a] (boost::system::error_code const & ec, std::size_t size_a) {
				if (auto node_l = node.lock ())
				{
					if (!ec)
					{
						this_s->set_last_packet_sent (std::chrono::steady_clock::now ());
					}
					if (ec == boost::system::errc::host_unreachable)
					{
						node_l->stats.inc (nano::stat::type::error, nano::stat::detail::unreachable_host, nano::stat::dir::out);
					}
					if (callback_a)
					{
						callback_a (ec, size_a);
					}
				}
			},
			traffic_type);
		}
		else
		{
			if (policy_a == nano::transport::buffer_drop_policy::no_socket_drop)
			{
				node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_write_no_socket_drop, nano::stat::dir::out);
			}
			else
			{
				node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_write_drop, nano::stat::dir::out);
			}
			if (callback_a)
			{
				callback_a (boost::system::errc::make_error_code (boost::system::errc::no_buffer_space), 0);
			}
		}
	}
	else if (callback_a)
	{
		node.background ([callback_a] () {
			callback_a (boost::system::errc::make_error_code (boost::system::errc::not_supported), 0);
		});
	}
}

std::string nano::transport::channel_tcp::to_string () const
{
	return nano::util::to_str (get_tcp_endpoint ());
}

void nano::transport::channel_tcp::operator() (nano::object_stream & obs) const
{
	nano::transport::channel::operator() (obs); // Write common data

	obs.write ("socket", socket);
}

/*
 * tcp_channels
 */

nano::transport::tcp_channels::tcp_channels (nano::node & node, std::function<void (nano::message const &, std::shared_ptr<nano::transport::channel> const &)> sink) :
	node{ node },
	message_manager{ node.config.tcp_incoming_connections_max },
	sink{ std::move (sink) }
{
}

nano::transport::tcp_channels::~tcp_channels ()
{
	debug_assert (channels.empty ());
}

void nano::transport::tcp_channels::start ()
{
}

void nano::transport::tcp_channels::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	message_manager.stop ();

	close ();
}

void nano::transport::tcp_channels::close ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	for (auto const & channel : channels)
	{
		if (channel.socket)
		{
			channel.socket->close ();
		}
		// Remove response server
		if (channel.response_server)
		{
			channel.response_server->stop ();
		}
	}

	channels.clear ();
}

bool nano::transport::tcp_channels::insert (std::shared_ptr<nano::transport::channel_tcp> const & channel_a, std::shared_ptr<nano::transport::socket> const & socket_a, std::shared_ptr<nano::transport::tcp_server> const & server_a)
{
	auto endpoint (channel_a->get_tcp_endpoint ());
	debug_assert (endpoint.address ().is_v6 ());
	auto udp_endpoint (nano::transport::map_tcp_to_endpoint (endpoint));
	bool error (true);
	if (!node.network.not_a_peer (udp_endpoint, node.config.allow_local_peers) && !stopped)
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		auto existing (channels.get<endpoint_tag> ().find (endpoint));
		if (existing == channels.get<endpoint_tag> ().end ())
		{
			auto node_id (channel_a->get_node_id ());
			if (!channel_a->temporary)
			{
				channels.get<node_id_tag> ().erase (node_id);
			}
			channels.get<endpoint_tag> ().emplace (channel_a, socket_a, server_a);
			attempts.get<endpoint_tag> ().erase (endpoint);
			error = false;
			lock.unlock ();
			node.network.channel_observer (channel_a);
		}
	}
	return error;
}

void nano::transport::tcp_channels::erase (nano::tcp_endpoint const & endpoint_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	channels.get<endpoint_tag> ().erase (endpoint_a);
}

std::size_t nano::transport::tcp_channels::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return channels.size ();
}

std::shared_ptr<nano::transport::channel_tcp> nano::transport::tcp_channels::find_channel (nano::tcp_endpoint const & endpoint_a) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	std::shared_ptr<nano::transport::channel_tcp> result;
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<nano::transport::channel>> nano::transport::tcp_channels::random_set (std::size_t count_a, uint8_t min_version, bool include_temporary_channels_a) const
{
	std::unordered_set<std::shared_ptr<nano::transport::channel>> result;
	result.reserve (count_a);
	nano::lock_guard<nano::mutex> lock{ mutex };
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

			if (channel->get_network_version () >= min_version && (include_temporary_channels_a || !channel->temporary))
			{
				result.insert (channel);
			}
		}
	}
	return result;
}

void nano::transport::tcp_channels::random_fill (std::array<nano::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size ()));
	debug_assert (peers.size () <= target_a.size ());
	auto endpoint (nano::endpoint (boost::asio::ip::address_v6{}, 0));
	debug_assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		debug_assert ((*i)->get_endpoint ().address ().is_v6 ());
		debug_assert (j < target_a.end ());
		*j = (*i)->get_endpoint ();
	}
}

bool nano::transport::tcp_channels::store_all (bool clear_peers)
{
	// We can't hold the mutex while starting a write transaction, so
	// we collect endpoints to be saved and then relase the lock.
	std::vector<nano::endpoint> endpoints;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		endpoints.reserve (channels.size ());
		std::transform (channels.begin (), channels.end (),
		std::back_inserter (endpoints), [] (auto const & channel) { return nano::transport::map_tcp_to_endpoint (channel.endpoint ()); });
	}
	bool result (false);
	if (!endpoints.empty ())
	{
		// Clear all peers then refresh with the current list of peers
		auto transaction (node.store.tx_begin_write ({ tables::peers }));
		if (clear_peers)
		{
			node.store.peer.clear (transaction);
		}
		for (auto const & endpoint : endpoints)
		{
			node.store.peer.put (transaction, nano::endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () }, nano::milliseconds_since_epoch ());
		}
		result = true;
	}
	return result;
}

std::shared_ptr<nano::transport::channel_tcp> nano::transport::tcp_channels::find_node_id (nano::account const & node_id_a)
{
	std::shared_ptr<nano::transport::channel_tcp> result;
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto existing (channels.get<node_id_tag> ().find (node_id_a));
	if (existing != channels.get<node_id_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

nano::tcp_endpoint nano::transport::tcp_channels::bootstrap_peer ()
{
	nano::tcp_endpoint result (boost::asio::ip::address_v6::any (), 0);
	nano::lock_guard<nano::mutex> lock{ mutex };
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= node.network_params.network.protocol_version_min)
		{
			result = nano::transport::map_endpoint_to_tcp (i->channel->get_peering_endpoint ());
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

void nano::transport::tcp_channels::process_messages ()
{
	while (!stopped)
	{
		auto item = message_manager.get_message ();
		if (item.message != nullptr)
		{
			process_message (*item.message, item.endpoint, item.node_id, item.socket);
		}
	}
}

void nano::transport::tcp_channels::process_message (nano::message const & message_a, nano::tcp_endpoint const & endpoint_a, nano::account const & node_id_a, std::shared_ptr<nano::transport::socket> const & socket_a)
{
	auto type_a = socket_a->type ();
	if (!stopped && message_a.header.version_using >= node.network_params.network.protocol_version_min)
	{
		auto channel (node.network.find_channel (nano::transport::map_tcp_to_endpoint (endpoint_a)));
		if (channel)
		{
			sink (message_a, channel);
		}
		else
		{
			channel = node.network.find_node_id (node_id_a);
			if (channel)
			{
				sink (message_a, channel);
			}
			else if (!node.network.excluded_peers.check (endpoint_a))
			{
				if (!node_id_a.is_zero ())
				{
					// Add temporary channel
					auto temporary_channel (std::make_shared<nano::transport::channel_tcp> (node, socket_a));
					temporary_channel->update_endpoint ();
					debug_assert (endpoint_a == temporary_channel->get_tcp_endpoint ());
					temporary_channel->set_node_id (node_id_a);
					temporary_channel->set_network_version (message_a.header.version_using);
					temporary_channel->temporary = true;
					debug_assert (type_a == nano::transport::socket::type_t::realtime || type_a == nano::transport::socket::type_t::realtime_response_server);
					// Don't insert temporary channels for response_server
					if (type_a == nano::transport::socket::type_t::realtime)
					{
						insert (temporary_channel, socket_a, nullptr);
					}
					sink (message_a, temporary_channel);
				}
				else
				{
					// Initial node_id_handshake request without node ID
					debug_assert (message_a.header.type == nano::message_type::node_id_handshake);
					node.stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
				}
			}
		}
		if (channel)
		{
			channel->set_last_packet_received (std::chrono::steady_clock::now ());
		}
	}
}

bool nano::transport::tcp_channels::max_ip_connections (nano::tcp_endpoint const & endpoint_a)
{
	if (node.flags.disable_max_peers_per_ip)
	{
		return false;
	}
	bool result{ false };
	auto const address (nano::transport::ipv4_address_or_ipv6_subnet (endpoint_a.address ()));
	nano::unique_lock<nano::mutex> lock{ mutex };
	result = channels.get<ip_address_tag> ().count (address) >= node.network_params.network.max_peers_per_ip;
	if (!result)
	{
		result = attempts.get<ip_address_tag> ().count (address) >= node.network_params.network.max_peers_per_ip;
	}
	if (result)
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_ip, nano::stat::dir::out);
	}
	return result;
}

bool nano::transport::tcp_channels::max_subnetwork_connections (nano::tcp_endpoint const & endpoint_a)
{
	if (node.flags.disable_max_peers_per_subnetwork)
	{
		return false;
	}
	bool result{ false };
	auto const subnet (nano::transport::map_address_to_subnetwork (endpoint_a.address ()));
	nano::unique_lock<nano::mutex> lock{ mutex };
	result = channels.get<subnetwork_tag> ().count (subnet) >= node.network_params.network.max_peers_per_subnetwork;
	if (!result)
	{
		result = attempts.get<subnetwork_tag> ().count (subnet) >= node.network_params.network.max_peers_per_subnetwork;
	}
	if (result)
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_subnetwork, nano::stat::dir::out);
	}
	return result;
}

bool nano::transport::tcp_channels::max_ip_or_subnetwork_connections (nano::tcp_endpoint const & endpoint_a)
{
	return max_ip_connections (endpoint_a) || max_subnetwork_connections (endpoint_a);
}

bool nano::transport::tcp_channels::track_reachout (nano::endpoint const & endpoint_a)
{
	auto const tcp_endpoint = nano::transport::map_endpoint_to_tcp (endpoint_a);

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

	nano::lock_guard<nano::mutex> lock{ mutex };
	auto [it, inserted] = attempts.emplace (tcp_endpoint);
	return inserted;
}

std::unique_ptr<nano::container_info_component> nano::transport::tcp_channels::collect_container_info (std::string const & name)
{
	std::size_t channels_count;
	std::size_t attemps_count;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		channels_count = channels.size ();
		attemps_count = attempts.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "channels", channels_count, sizeof (decltype (channels)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "attempts", attemps_count, sizeof (decltype (attempts)::value_type) }));

	return composite;
}

void nano::transport::tcp_channels::purge (std::chrono::steady_clock::time_point cutoff_deadline)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	node.logger.debug (nano::log::type::tcp_channels, "Performing periodic channel cleanup, cutoff: {}s", nano::log::seconds_delta (cutoff_deadline));

	auto should_close = [this, cutoff_deadline] (auto const & channel) {
		// Remove channels that haven't successfully sent a message within the cutoff time
		if (auto last = channel->get_last_packet_sent (); last < cutoff_deadline)
		{
			node.logger.debug (nano::log::type::tcp_channels, "Closing idle channel: {} (idle for {}s)",
			channel->to_string (),
			nano::log::seconds_delta (last));

			return true; // Close
		}
		// Check if any tcp channels belonging to old protocol versions which may still be alive due to async operations
		if (channel->get_network_version () < node.network_params.network.protocol_version_min)
		{
			node.logger.debug (nano::log::type::tcp_channels, "Closing channel with old protocol version: {}", channel->to_string ());

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
			node.logger.debug (nano::log::type::tcp_channels, "Removing dead channel: {}", entry.channel->to_string ());
			return true; // Erase
		}
		return false;
	});

	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<last_attempt_tag> ().lower_bound (cutoff_deadline));
	attempts.get<last_attempt_tag> ().erase (attempts.get<last_attempt_tag> ().begin (), attempts_cutoff);
}

void nano::transport::tcp_channels::keepalive ()
{
	nano::keepalive message{ node.network_params.network };
	node.network.random_fill (message.peers);

	nano::unique_lock<nano::mutex> lock{ mutex };

	auto const cutoff_time = std::chrono::steady_clock::now () - node.network_params.network.keepalive_period;

	// Wake up channels
	std::vector<std::shared_ptr<nano::transport::channel_tcp>> to_wakeup;
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
		channel->send (message);
	}
}

std::optional<nano::keepalive> nano::transport::tcp_channels::sample_keepalive ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	size_t counter = 0;
	while (counter++ < channels.size ())
	{
		auto index = rng.random (channels.size ());
		if (auto server = channels.get<random_access_tag> ()[index].response_server)
		{
			if (auto keepalive = server->pop_last_keepalive ())
			{
				return keepalive;
			}
		}
	}

	return std::nullopt;
}

void nano::transport::tcp_channels::list (std::deque<std::shared_ptr<nano::transport::channel>> & deque_a, uint8_t minimum_version_a, bool include_temporary_channels_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	// clang-format off
	nano::transform_if (channels.get<random_access_tag> ().begin (), channels.get<random_access_tag> ().end (), std::back_inserter (deque_a),
		[include_temporary_channels_a, minimum_version_a](auto & channel_a) { return channel_a.channel->get_network_version () >= minimum_version_a && (include_temporary_channels_a || !channel_a.channel->temporary); },
		[](auto const & channel) { return channel.channel; });
	// clang-format on
}

void nano::transport::tcp_channels::modify (std::shared_ptr<nano::transport::channel_tcp> const & channel_a, std::function<void (std::shared_ptr<nano::transport::channel_tcp> const &)> modify_callback_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto existing (channels.get<endpoint_tag> ().find (channel_a->get_tcp_endpoint ()));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [modify_callback = std::move (modify_callback_a)] (channel_entry & wrapper_a) {
			modify_callback (wrapper_a.channel);
		});
	}
}

void nano::transport::tcp_channels::start_tcp (nano::endpoint const & endpoint_a)
{
	auto socket = std::make_shared<nano::transport::socket> (node);
	std::weak_ptr<nano::transport::socket> socket_w (socket);
	auto channel (std::make_shared<nano::transport::channel_tcp> (node, socket_w));
	std::weak_ptr<nano::node> node_w (node.shared ());
	socket->async_connect (nano::transport::map_endpoint_to_tcp (endpoint_a),
	[node_w, channel, socket, endpoint_a] (boost::system::error_code const & ec) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel)
			{
				// TCP node ID handshake
				auto query = node_l->network.prepare_handshake_query (endpoint_a);
				nano::node_id_handshake message{ node_l->network_params.network, query };

				node_l->logger.debug (nano::log::type::tcp, "Handshake sent to: {} (query: {})",
				nano::util::to_str (endpoint_a),
				(query ? query->cookie.to_string () : "<none>"));

				channel->update_endpoint ();
				std::shared_ptr<std::vector<uint8_t>> receive_buffer (std::make_shared<std::vector<uint8_t>> ());
				receive_buffer->resize (256);
				channel->send (message, [node_w, channel, endpoint_a, receive_buffer] (boost::system::error_code const & ec, std::size_t size_a) {
					if (auto node_l = node_w.lock ())
					{
						if (!ec)
						{
							node_l->network.tcp_channels.start_tcp_receive_node_id (channel, endpoint_a, receive_buffer);
						}
						else
						{
							node_l->logger.debug (nano::log::type::tcp, "Error sending handshake to: {} ({})", nano::util::to_str (endpoint_a), ec.message ());

							if (auto socket_l = channel->socket.lock ())
							{
								socket_l->close ();
							}
						}
					}
				});
			}
			else
			{
				if (ec)
				{
					node_l->logger.debug (nano::log::type::tcp, "Error connecting to: {} ({})", nano::util::to_str (endpoint_a), ec.message ());
				}
				else
				{
					node_l->logger.debug (nano::log::type::tcp, "Error connecting to: {}", nano::util::to_str (endpoint_a));
				}
			}
		}
	});
}

void nano::transport::tcp_channels::start_tcp_receive_node_id (std::shared_ptr<nano::transport::channel_tcp> const & channel_a, nano::endpoint const & endpoint_a, std::shared_ptr<std::vector<uint8_t>> const & receive_buffer_a)
{
	std::weak_ptr<nano::node> node_w (node.shared ());
	auto socket_l = channel_a->socket.lock ();
	if (!socket_l)
	{
		return;
	}
	auto cleanup_node_id_handshake_socket = [socket_w = channel_a->socket, node_w] (nano::endpoint const & endpoint_a) {
		if (auto node_l = node_w.lock ())
		{
			if (auto socket_l = socket_w.lock ())
			{
				socket_l->close ();
			}
		}
	};

	auto message_deserializer = std::make_shared<nano::transport::message_deserializer> (node.network_params.network, node.network.publish_filter, node.block_uniquer, node.vote_uniquer,
	[socket_l] (std::shared_ptr<std::vector<uint8_t>> const & data_a, size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a) {
		debug_assert (socket_l != nullptr);
		socket_l->read_impl (data_a, size_a, callback_a);
	});
	message_deserializer->read ([node_w, socket_l, channel_a, endpoint_a, cleanup_node_id_handshake_socket] (boost::system::error_code ec, std::unique_ptr<nano::message> message) {
		auto node_l = node_w.lock ();
		if (!node_l)
		{
			return;
		}
		if (ec || !channel_a)
		{
			node_l->logger.debug (nano::log::type::tcp, "Error reading handshake from: {} ({})", nano::util::to_str (endpoint_a), ec.message ());

			cleanup_node_id_handshake_socket (endpoint_a);
			return;
		}
		node_l->stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
		auto error (false);

		// the header type should in principle be checked after checking the network bytes and the version numbers, I will not change it here since the benefits do not outweight the difficulties
		if (error || message->type () != nano::message_type::node_id_handshake)
		{
			node_l->logger.debug (nano::log::type::tcp, "Error reading handshake header from: {} ({})", nano::util::to_str (endpoint_a), ec.message ());

			cleanup_node_id_handshake_socket (endpoint_a);
			return;
		}
		auto & handshake = static_cast<nano::node_id_handshake &> (*message);

		if (message->header.network != node_l->network_params.network.current_network || message->header.version_using < node_l->network_params.network.protocol_version_min)
		{
			// error handling, either the networks bytes or the version is wrong
			if (message->header.network == node_l->network_params.network.current_network)
			{
				node_l->stats.inc (nano::stat::type::message, nano::stat::detail::invalid_network);
			}
			else
			{
				node_l->stats.inc (nano::stat::type::message, nano::stat::detail::outdated_version);
			}

			cleanup_node_id_handshake_socket (endpoint_a);
			// Cleanup attempt
			{
				nano::lock_guard<nano::mutex> lock{ node_l->network.tcp_channels.mutex };
				node_l->network.tcp_channels.attempts.get<endpoint_tag> ().erase (nano::transport::map_endpoint_to_tcp (endpoint_a));
			}
			return;
		}

		if (error || !handshake.response || !handshake.query)
		{
			node_l->logger.debug (nano::log::type::tcp, "Error reading handshake payload from: {} ({})", nano::util::to_str (endpoint_a), ec.message ());

			cleanup_node_id_handshake_socket (endpoint_a);
			return;
		}
		channel_a->set_network_version (handshake.header.version_using);

		debug_assert (handshake.query);
		debug_assert (handshake.response);

		auto const node_id = handshake.response->node_id;

		if (!node_l->network.verify_handshake_response (*handshake.response, endpoint_a))
		{
			cleanup_node_id_handshake_socket (endpoint_a);
			return;
		}

		/* If node ID is known, don't establish new connection
		   Exception: temporary channels from tcp_server */
		auto existing_channel (node_l->network.tcp_channels.find_node_id (node_id));
		if (existing_channel && !existing_channel->temporary)
		{
			cleanup_node_id_handshake_socket (endpoint_a);
			return;
		}

		channel_a->set_node_id (node_id);
		channel_a->set_last_packet_received (std::chrono::steady_clock::now ());

		debug_assert (handshake.query);
		auto response = node_l->network.prepare_handshake_response (*handshake.query, handshake.is_v2 ());
		nano::node_id_handshake handshake_response (node_l->network_params.network, std::nullopt, response);

		node_l->logger.debug (nano::log::type::tcp, "Handshake response sent to {} (query: {})",
		nano::util::to_str (endpoint_a),
		handshake.query->cookie.to_string ());

		channel_a->send (handshake_response, [node_w, channel_a, endpoint_a, cleanup_node_id_handshake_socket] (boost::system::error_code const & ec, std::size_t size_a) {
			auto node_l = node_w.lock ();
			if (!node_l)
			{
				return;
			}
			if (ec || !channel_a)
			{
				node_l->logger.debug (nano::log::type::tcp, "Error sending handshake response to: {} ({})", nano::util::to_str (endpoint_a), ec.message ());

				cleanup_node_id_handshake_socket (endpoint_a);
				return;
			}
			// Insert new node ID connection
			auto socket_l = channel_a->socket.lock ();
			if (!socket_l)
			{
				return;
			}
			channel_a->set_last_packet_sent (std::chrono::steady_clock::now ());
			auto response_server = std::make_shared<nano::transport::tcp_server> (socket_l, node_l);
			node_l->network.tcp_channels.insert (channel_a, socket_l, response_server);
			// Listen for possible responses
			response_server->socket->type_set (nano::transport::socket::type_t::realtime_response_server);
			response_server->remote_node_id = channel_a->get_node_id ();
			response_server->start ();
		});
	});
}
