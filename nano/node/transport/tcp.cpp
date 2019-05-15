#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>

nano::transport::channel_tcp::channel_tcp (nano::node & node_a, std::shared_ptr<nano::socket> socket_a) :
channel (node_a),
socket (socket_a)
{
}

nano::transport::channel_tcp::~channel_tcp ()
{
	std::lock_guard<std::mutex> lk (channel_mutex);
	if (socket)
	{
		socket->close ();
	}
}

size_t nano::transport::channel_tcp::hash_code () const
{
	std::hash<::nano::tcp_endpoint> hash;
	return hash (socket->remote_endpoint ());
}

bool nano::transport::channel_tcp::operator== (nano::transport::channel const & other_a) const
{
	bool result (false);
	auto other_l (dynamic_cast<nano::transport::channel_tcp const *> (&other_a));
	if (other_l != nullptr)
	{
		return *this == *other_l;
	}
	return result;
}

void nano::transport::channel_tcp::send_buffer (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a)
{
	socket->async_write (buffer_a, callback (buffer_a, detail_a, callback_a));
}

std::function<void(boost::system::error_code const &, size_t)> nano::transport::channel_tcp::callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	// clang-format off
	return [ buffer_a, node = std::weak_ptr<nano::node> (node.shared ()), callback_a ](boost::system::error_code const & ec, size_t size_a)
	{
		if (auto node_l = node.lock ())
		{
			if (ec == boost::system::errc::host_unreachable)
			{
				node_l->stats.inc (nano::stat::type::error, nano::stat::detail::unreachable_host, nano::stat::dir::out);
			}
			if (callback_a)
			{
				callback_a (ec, size_a);
			}
		}
	};
	// clang-format on
}

std::string nano::transport::channel_tcp::to_string () const
{
	return boost::str (boost::format ("%1%") % socket->remote_endpoint ());
}

nano::transport::tcp_channels::tcp_channels (nano::node & node_a) :
node (node_a)
{
}

bool nano::transport::tcp_channels::insert (std::shared_ptr<nano::transport::channel_tcp> channel_a)
{
	auto endpoint (channel_a->get_tcp_endpoint ());
	assert (endpoint.address ().is_v6 ());
	bool error (true);
	if (!node.network.not_a_peer (nano::transport::map_tcp_to_endpoint (endpoint), node.config.allow_local_peers))
	{
		std::unique_lock<std::mutex> lock (mutex);
		auto existing (channels.get<endpoint_tag> ().find (endpoint));
		if (existing == channels.get<endpoint_tag> ().end ())
		{
			channels.get<endpoint_tag> ().insert ({ channel_a });
			error = false;
			lock.unlock ();
			node.network.channel_observer (channel_a);
		}
	}
	return error;
}

void nano::transport::tcp_channels::erase (nano::tcp_endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	channels.get<endpoint_tag> ().erase (endpoint_a);
}

size_t nano::transport::tcp_channels::size () const
{
	std::lock_guard<std::mutex> lock (mutex);
	return channels.size ();
}

std::shared_ptr<nano::transport::channel_tcp> nano::transport::tcp_channels::find_channel (nano::tcp_endpoint const & endpoint_a) const
{
	std::lock_guard<std::mutex> lock (mutex);
	std::shared_ptr<nano::transport::channel_tcp> result;
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<nano::transport::channel>> nano::transport::tcp_channels::random_set (size_t count_a) const
{
	std::unordered_set<std::shared_ptr<nano::transport::channel>> result;
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
			auto index (nano::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (peers_size - 1)));
			result.insert (channels.get<random_access_tag> ()[index].channel);
		}
	}
	return result;
}


void nano::transport::tcp_channels::random_fill (std::array<nano::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size ()));
	assert (peers.size () <= target_a.size ());
	auto endpoint (nano::endpoint (boost::asio::ip::address_v6{}, 0));
	assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		assert ((*i)->get_endpoint ().address ().is_v6 ());
		assert (j < target_a.end ());
		*j = (*i)->get_endpoint ();
	}
}

bool nano::transport::tcp_channels::store_all (bool clear_peers)
{
	// We can't hold the mutex while starting a write transaction, so
	// we collect endpoints to be saved and then relase the lock.
	std::vector<nano::endpoint> endpoints;
	{
		std::lock_guard<std::mutex> lock (mutex);
		endpoints.reserve (channels.size ());
		std::transform (channels.begin (), channels.end (),
		std::back_inserter (endpoints), [](const auto & channel) { return nano::transport::map_tcp_to_endpoint (channel.endpoint ()); });
	}
	bool result (false);
	if (!endpoints.empty ())
	{
		// Clear all peers then refresh with the current list of peers
		auto transaction (node.store.tx_begin_write ());
		if (clear_peers)
		{
			node.store.peer_clear (transaction);
		}
		for (auto endpoint : endpoints)
		{
			nano::endpoint_key endpoint_key (endpoint.address ().to_v6 ().to_bytes (), endpoint.port ());
			node.store.peer_put (transaction, std::move (endpoint_key));
		}
		result = true;
	}
	return result;
}

std::shared_ptr<nano::transport::channel_tcp> nano::transport::tcp_channels::find_node_id (nano::account const & node_id_a)
{
	std::shared_ptr<nano::transport::channel_tcp> result;
	std::lock_guard<std::mutex> lock (mutex);
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
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= protocol_version_reasonable_min)
		{
			result = i->endpoint ();
			channels.get<last_bootstrap_attempt_tag> ().modify (i, [](channel_tcp_wrapper & wrapper_a) {
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

void nano::transport::tcp_channels::process_message (nano::message const & message_a, nano::tcp_endpoint const & endpoint_a, nano::account const & node_id_a)
{
	if (!stopped)
	{
		auto channel (node.network.find_channel (nano::transport::map_tcp_to_endpoint (endpoint_a)));
		if (channel)
		{
			node.process_message (message_a, channel);
		}
		else
		{
			channel = node.network.search_response_channel (endpoint_a, node_id_a);
			if (channel)
			{
				node.process_message (message_a, channel);
			}
			else
			{
				auto udp_channel (std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, nano::transport::map_tcp_to_endpoint (endpoint_a)));
				node.process_message (message_a, udp_channel);
			}
		}
	}
}


void nano::transport::tcp_channels::start ()
{
	ongoing_keepalive ();
	ongoing_syn_cookie_cleanup ();
}

void nano::transport::tcp_channels::stop ()
{
	stopped = true;
	// Close all TCP sockets
	for (auto i (channels.begin ()), j (channels.end ()); i != j; ++i)
	{
		if (i->channel->socket != nullptr)
		{
			i->channel->socket->close ();
		}
	}
}

bool nano::transport::tcp_channels::max_ip_connections (nano::tcp_endpoint const & endpoint_a)
{
	std::unique_lock<std::mutex> lock (mutex);
	bool result (channels.get<ip_address_tag> ().count (endpoint_a.address ()) >= nano::transport::max_peers_per_ip);
	return result;
}

bool nano::transport::tcp_channels::reachout (nano::endpoint const & endpoint_a)
{
	auto tcp_endpoint (nano::transport::map_endpoint_to_tcp (endpoint_a));
	// Don't overload single IP
	bool error = max_ip_connections (tcp_endpoint);
	if (!error)
	{
		// Don't keepalive to nodes that already sent us something
		error |= find_channel (tcp_endpoint) != nullptr;
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (attempts.find (tcp_endpoint));
		error |= existing != attempts.end ();
		attempts.insert ({ tcp_endpoint, std::chrono::steady_clock::now () });
	}
	return error;
}


std::unique_ptr<nano::seq_con_info_component> nano::transport::tcp_channels::collect_seq_con_info (std::string const & name)
{
	size_t channels_count = 0;
	size_t attemps_count = 0;
	size_t syn_cookies_count = 0;
	size_t syn_cookies_per_ip_count = 0;
	{
		std::lock_guard<std::mutex> guard (mutex);
		channels_count = channels.size ();
		attemps_count = attempts.size ();
		std::lock_guard<std::mutex> syn_cookie_guard (syn_cookie_mutex);
		syn_cookies_count = syn_cookies.size ();
		syn_cookies_per_ip_count = syn_cookies_per_ip.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "channels", channels_count, sizeof (decltype (channels)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "attempts", attemps_count, sizeof (decltype (attempts)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "syn_cookies", syn_cookies_count, sizeof (decltype (syn_cookies)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "syn_cookies_per_ip", syn_cookies_per_ip_count, sizeof (decltype (syn_cookies_per_ip)::value_type) }));

	return composite;
}

void nano::transport::tcp_channels::purge (std::chrono::steady_clock::time_point const & cutoff_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto disconnect_cutoff (channels.get<last_packet_sent_tag> ().lower_bound (cutoff_a));
	channels.get<last_packet_sent_tag> ().erase (channels.get<last_packet_sent_tag> ().begin (), disconnect_cutoff);
	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<1> ().lower_bound (cutoff_a));
	attempts.get<1> ().erase (attempts.get<1> ().begin (), attempts_cutoff);
}

boost::optional<nano::uint256_union> nano::transport::tcp_channels::assign_syn_cookie (nano::tcp_endpoint const & endpoint_a)
{
	auto ip_addr (endpoint_a.address ());
	assert (ip_addr.is_v6 ());
	std::lock_guard<std::mutex> lock (syn_cookie_mutex);
	unsigned & ip_cookies = syn_cookies_per_ip[ip_addr];
	boost::optional<nano::uint256_union> result;
	if (ip_cookies < nano::transport::max_peers_per_ip)
	{
		if (syn_cookies.find (endpoint_a) == syn_cookies.end ())
		{
			nano::uint256_union query;
			random_pool::generate_block (query.bytes.data (), query.bytes.size ());
			syn_cookie_info info{ query, std::chrono::steady_clock::now () };
			syn_cookies[endpoint_a] = info;
			++ip_cookies;
			result = query;
		}
	}
	return result;
}

bool nano::transport::tcp_channels::validate_syn_cookie (nano::tcp_endpoint const & endpoint_a, nano::account const & node_id, nano::signature const & sig)
{
	auto ip_addr (endpoint_a.address ());
	assert (ip_addr.is_v6 ());
	std::lock_guard<std::mutex> lock (syn_cookie_mutex);
	auto result (true);
	auto cookie_it (syn_cookies.find (endpoint_a));
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

void nano::transport::tcp_channels::purge_syn_cookies (std::chrono::steady_clock::time_point const & cutoff_a)
{
	std::lock_guard<std::mutex> lock (syn_cookie_mutex);
	auto it (syn_cookies.begin ());
	while (it != syn_cookies.end ())
	{
		auto info (it->second);
		if (info.created_at < cutoff_a)
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

void nano::transport::tcp_channels::ongoing_syn_cookie_cleanup ()
{
	purge_syn_cookies (std::chrono::steady_clock::now () - nano::transport::syn_cookie_cutoff);
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + (nano::transport::syn_cookie_cutoff * 2), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.tcp_channels.ongoing_syn_cookie_cleanup ();
		}
	});
}

void nano::transport::tcp_channels::ongoing_keepalive ()
{
	nano::keepalive message;
	node.network.random_fill (message.peers);
	std::unique_lock<std::mutex> lock (mutex);
	// Wake up channels
	std::vector<std::shared_ptr<nano::transport::channel_tcp>> send_list;
	auto keepalive_sent_cutoff (channels.get<last_packet_sent_tag> ().lower_bound (std::chrono::steady_clock::now () - network_params.node.period));
	for (auto i (channels.get<last_packet_sent_tag> ().begin ()); i != keepalive_sent_cutoff; ++i)
	{
		send_list.push_back (i->channel);
	}
	lock.unlock ();
	for (auto & channel : send_list)
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		channel->send (message, [node_w, channel](boost::system::error_code const & ec, size_t size_a) {
			if (auto node_l = node_w.lock ())
			{
				if (!ec)
				{
					node_l->network.tcp_channels.modify (channel, [](std::shared_ptr<nano::transport::channel_tcp> channel_a) {
						channel_a->set_last_packet_sent (std::chrono::steady_clock::now ());
					});
				}
			}
		});
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + network_params.node.period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.tcp_channels.ongoing_keepalive ();
		}
	});
}

void nano::transport::tcp_channels::list (std::deque<std::shared_ptr<nano::transport::channel>> & deque_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.begin ()), j (channels.end ()); i != j; ++i)
	{
		deque_a.push_back (i->channel);
	}
}

void nano::transport::tcp_channels::modify (std::shared_ptr<nano::transport::channel_tcp> channel_a, std::function<void(std::shared_ptr<nano::transport::channel_tcp>)> modify_callback_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<endpoint_tag> ().find (channel_a->get_tcp_endpoint ()));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [modify_callback_a](channel_tcp_wrapper & wrapper_a) {
			modify_callback_a (wrapper_a.channel);
		});
	}
}

void nano::transport::tcp_channels::start_tcp (nano::endpoint const & endpoint_a, std::function<void(std::shared_ptr<nano::transport::channel>)> const & callback_a)
{
	auto socket (std::make_shared<nano::socket> (node.shared_from_this (), boost::none, nano::socket::concurrency::multi_writer));
	auto channel (std::make_shared<nano::transport::channel_tcp> (node, socket));
	auto tcp_endpoint (nano::transport::map_endpoint_to_tcp (endpoint_a));
	std::weak_ptr<nano::node> node_w (node.shared ());
	channel->socket->async_connect (tcp_endpoint,
	[node_w, channel, endpoint_a, callback_a](boost::system::error_code const & ec) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel)
			{
				// TCP node ID handshake
				auto cookie (node_l->network.tcp_channels.assign_syn_cookie (nano::transport::map_endpoint_to_tcp (endpoint_a)));
				nano::node_id_handshake message (cookie, boost::none);
				auto bytes = message.to_bytes ();
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Node ID handshake request sent with node ID %1% to %2%: query %3%") % node_l->node_id.pub.to_account () % endpoint_a % (*cookie).to_string ()));
				}
				std::shared_ptr<std::vector<uint8_t>> receive_buffer (std::make_shared<std::vector<uint8_t>> ());
				receive_buffer->resize (256);
				channel->send_buffer (bytes, nano::stat::detail::node_id_handshake, [node_w, channel, endpoint_a, receive_buffer, callback_a](boost::system::error_code const & ec, size_t size_a) {
					if (auto node_l = node_w.lock ())
					{
						if (!ec && channel)
						{
							node_l->network.tcp_channels.start_tcp_receive_header (channel, endpoint_a, receive_buffer, callback_a);
						}
						else
						{
							if (node_l->config.logging.network_node_id_handshake_logging ())
							{
								node_l->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % endpoint_a % ec.message ()));
							}
							if (callback_a)
							{
								auto channel_udp (std::make_shared<nano::transport::channel_udp> (node_l->network.udp_channels, endpoint_a));
								callback_a (channel_udp); // Fallback to UDP
							}
						}
					}
				});
			}
			else if (callback_a)
			{
				auto channel_udp (std::make_shared<nano::transport::channel_udp> (node_l->network.udp_channels, endpoint_a));
				callback_a (channel_udp); // Fallback to UDP
			}
		}
	});
}

void nano::transport::tcp_channels::start_tcp_receive_header (std::shared_ptr<nano::transport::channel_tcp> channel_a, nano::endpoint const & endpoint_a, std::shared_ptr<std::vector<uint8_t>> receive_buffer_a, std::function<void(std::shared_ptr<nano::transport::channel>)> const & callback_a)
{
	std::weak_ptr<nano::node> node_w (node.shared ());
	channel_a->socket->async_read (receive_buffer_a, 8, [node_w, channel_a, endpoint_a, receive_buffer_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel_a)
			{
				assert (size_a == 8);
				nano::bufferstream type_stream (receive_buffer_a->data (), size_a);
				auto error (false);
				nano::message_header header (error, type_stream);
				if (!error && header.type == nano::message_type::node_id_handshake && header.node_id_handshake_is_response () && header.node_id_handshake_is_query () && header.version_using >= nano::protocol_version_min)
				{
					channel_a->set_network_version (header.version_using);
					node_l->network.tcp_channels.start_tcp_receive (channel_a, endpoint_a, receive_buffer_a, callback_a);
				}
				else if (callback_a)
				{
					auto channel_udp (std::make_shared<nano::transport::channel_udp> (node_l->network.udp_channels, endpoint_a));
					callback_a (channel_udp); // Fallback to UDP
				}
			}
			else
			{
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Error reading node_id_handshake header from %1%: %2%") % endpoint_a % ec.message ()));
				}
				if (callback_a)
				{
					auto channel_udp (std::make_shared<nano::transport::channel_udp> (node_l->network.udp_channels, endpoint_a));
					callback_a (channel_udp); // Fallback to UDP
				}
			}
		}
	});
}

void nano::transport::tcp_channels::start_tcp_receive (std::shared_ptr<nano::transport::channel_tcp> channel_a, nano::endpoint const & endpoint_a, std::shared_ptr<std::vector<uint8_t>> receive_buffer_a, std::function<void(std::shared_ptr<nano::transport::channel>)> const & callback_a)
{
	std::weak_ptr<nano::node> node_w (node.shared ());
	channel_a->socket->async_read (receive_buffer_a, sizeof (nano::account) + sizeof (nano::account) + sizeof (nano::signature), [node_w, channel_a, endpoint_a, receive_buffer_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel_a)
			{
				node_l->stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
				auto error (false);
				nano::bufferstream stream (receive_buffer_a->data (), size_a);
				nano::message_header header (nano::message_type::node_id_handshake);
				header.flag_set (nano::message_header::node_id_handshake_query_flag);
				header.flag_set (nano::message_header::node_id_handshake_response_flag);
				nano::node_id_handshake message (error, stream, header);
				if (message.response && message.query)
				{
					auto node_id (message.response->first);
					if (!node_l->network.tcp_channels.validate_syn_cookie (nano::transport::map_endpoint_to_tcp (endpoint_a), node_id, message.response->second))
					{
						if (node_id != node_l->node_id.pub)
						{
							channel_a->set_node_id (node_id);
							channel_a->set_last_packet_received (std::chrono::steady_clock::now ());
							if (!node_l->network.tcp_channels.find_node_id (node_id))
							{
								boost::optional<std::pair<nano::account, nano::signature>> response (std::make_pair (node_l->node_id.pub, nano::sign_message (node_l->node_id.prv, node_l->node_id.pub, *message.query)));
								nano::node_id_handshake response_message (boost::none, response);
								auto bytes = response_message.to_bytes ();
								if (node_l->config.logging.network_node_id_handshake_logging ())
								{
									node_l->logger.try_log (boost::str (boost::format ("Node ID handshake response sent with node ID %1% to %2%: query %3%") % node_l->node_id.pub.to_account () % endpoint_a % (*message.query).to_string ()));
								}
								channel_a->send_buffer (bytes, nano::stat::detail::node_id_handshake, [node_w, channel_a, endpoint_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
									if (auto node_l = node_w.lock ())
									{
										if (!ec && channel_a)
										{
											// Insert new node ID connection
											channel_a->set_last_packet_sent (std::chrono::steady_clock::now ());
											node_l->network.tcp_channels.insert (channel_a);
											if (callback_a)
											{
												callback_a (channel_a);
											}
										}
										else
										{
											if (node_l->config.logging.network_node_id_handshake_logging ())
											{
												node_l->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % endpoint_a % ec.message ()));
											}
											if (callback_a)
											{
												auto channel_udp (std::make_shared<nano::transport::channel_udp> (node_l->network.udp_channels, endpoint_a));
												callback_a (channel_udp); // Fallback to UDP
											}
										}
									}
								});
							}
							// If node ID is known, don't establish new connection
						}
					}
				}
			}
			else
			{
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Error reading node_id_handshake from %1%: %2%") % endpoint_a % ec.message ()));
				}
				if (callback_a)
				{
					auto channel_udp (std::make_shared<nano::transport::channel_udp> (node_l->network.udp_channels, endpoint_a));
					callback_a (channel_udp); // Fallback to UDP
				}
			}
		}
	});
}
