#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/udp.hpp>

std::chrono::seconds constexpr nano::transport::udp_channels::syn_cookie_cutoff;

nano::transport::channel_udp::channel_udp (nano::transport::udp_channels & channels_a, nano::endpoint const & endpoint_a, unsigned network_version_a) :
endpoint (endpoint_a),
network_version (network_version_a),
channels (channels_a)
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
	channels.send (buffer_a, endpoint, callback_a);
}

std::function<void(boost::system::error_code const &, size_t)> nano::transport::channel_udp::callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	// clang-format off
	return [ buffer_a, node = std::weak_ptr<nano::node> (channels.node.shared ()), detail_a, callback_a ](boost::system::error_code const & ec, size_t size_a)
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
	// clang-format on
}

std::string nano::transport::channel_udp::to_string () const
{
	return boost::str (boost::format ("UDP: %1%") % endpoint);
}

nano::transport::udp_channels::udp_channels (nano::node & node_a, uint16_t port_a) :
node (node_a),
strand (node_a.io_ctx.get_executor ()),
socket (node_a.io_ctx, nano::endpoint (boost::asio::ip::address_v6::any (), port_a))
{
	boost::system::error_code ec;
	auto port (socket.local_endpoint (ec).port ());
	if (ec)
	{
		node.logger.try_log ("Unable to retrieve port: ", ec.message ());
	}
	local_endpoint = nano::endpoint (boost::asio::ip::address_v6::loopback (), port);
}

void nano::transport::udp_channels::send (boost::asio::const_buffer buffer_a, nano::endpoint endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a)
{
	boost::asio::post (strand,
	[this, buffer_a, endpoint_a, callback_a]() {
		this->socket.async_send_to (buffer_a, endpoint_a,
		boost::asio::bind_executor (strand, callback_a));
	});
}

std::shared_ptr<nano::transport::channel_udp> nano::transport::udp_channels::insert (nano::endpoint const & endpoint_a, unsigned network_version_a)
{
	assert (endpoint_a.address ().is_v6 ());
	std::shared_ptr<nano::transport::channel_udp> result;
	if (!not_a_peer (endpoint_a, node.config.allow_local_peers))
	{
		std::unique_lock<std::mutex> lock (mutex);
		auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
		if (existing != channels.get<endpoint_tag> ().end ())
		{
			result = existing->channel;
		}
		else
		{
			result = std::make_shared<nano::transport::channel_udp> (*this, endpoint_a, network_version_a);
			channels.get<endpoint_tag> ().insert ({ result });
			lock.unlock ();
			node.network.channel_observer (result);
		}
	}
	return result;
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
			auto index (nano::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (peers_size - 1)));
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

void nano::transport::udp_channels::store_all (nano::node & node_a)
{
	// We can't hold the mutex while starting a write transaction, so
	// we collect endpoints to be saved and then relase the lock.
	std::vector<nano::endpoint> endpoints;
	{
		std::lock_guard<std::mutex> lock (mutex);
		endpoints.reserve (channels.size ());
		std::transform (channels.begin (), channels.end (),
		std::back_inserter (endpoints), [](const auto & channel) { return channel.endpoint (); });
	}
	if (!endpoints.empty ())
	{
		// Clear all peers then refresh with the current list of peers
		auto transaction (node_a.store.tx_begin_write ());
		node_a.store.peer_clear (transaction);
		for (auto endpoint : endpoints)
		{
			nano::endpoint_key endpoint_key (endpoint.address ().to_v6 ().to_bytes (), endpoint.port ());
			node_a.store.peer_put (transaction, std::move (endpoint_key));
		}
	}
}

namespace
{
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long address_a)
{
	return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}
}

bool nano::transport::udp_channels::reserved_address (nano::endpoint const & endpoint_a, bool allow_local_peers)
{
	assert (endpoint_a.address ().is_v6 ());
	auto bytes (endpoint_a.address ().to_v6 ());
	auto result (false);
	static auto const rfc1700_min (mapped_from_v4_bytes (0x00000000ul));
	static auto const rfc1700_max (mapped_from_v4_bytes (0x00fffffful));
	static auto const rfc1918_1_min (mapped_from_v4_bytes (0x0a000000ul));
	static auto const rfc1918_1_max (mapped_from_v4_bytes (0x0afffffful));
	static auto const rfc1918_2_min (mapped_from_v4_bytes (0xac100000ul));
	static auto const rfc1918_2_max (mapped_from_v4_bytes (0xac1ffffful));
	static auto const rfc1918_3_min (mapped_from_v4_bytes (0xc0a80000ul));
	static auto const rfc1918_3_max (mapped_from_v4_bytes (0xc0a8fffful));
	static auto const rfc6598_min (mapped_from_v4_bytes (0x64400000ul));
	static auto const rfc6598_max (mapped_from_v4_bytes (0x647ffffful));
	static auto const rfc5737_1_min (mapped_from_v4_bytes (0xc0000200ul));
	static auto const rfc5737_1_max (mapped_from_v4_bytes (0xc00002fful));
	static auto const rfc5737_2_min (mapped_from_v4_bytes (0xc6336400ul));
	static auto const rfc5737_2_max (mapped_from_v4_bytes (0xc63364fful));
	static auto const rfc5737_3_min (mapped_from_v4_bytes (0xcb007100ul));
	static auto const rfc5737_3_max (mapped_from_v4_bytes (0xcb0071fful));
	static auto const ipv4_multicast_min (mapped_from_v4_bytes (0xe0000000ul));
	static auto const ipv4_multicast_max (mapped_from_v4_bytes (0xeffffffful));
	static auto const rfc6890_min (mapped_from_v4_bytes (0xf0000000ul));
	static auto const rfc6890_max (mapped_from_v4_bytes (0xfffffffful));
	static auto const rfc6666_min (boost::asio::ip::address_v6::from_string ("100::"));
	static auto const rfc6666_max (boost::asio::ip::address_v6::from_string ("100::ffff:ffff:ffff:ffff"));
	static auto const rfc3849_min (boost::asio::ip::address_v6::from_string ("2001:db8::"));
	static auto const rfc3849_max (boost::asio::ip::address_v6::from_string ("2001:db8:ffff:ffff:ffff:ffff:ffff:ffff"));
	static auto const rfc4193_min (boost::asio::ip::address_v6::from_string ("fc00::"));
	static auto const rfc4193_max (boost::asio::ip::address_v6::from_string ("fd00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	static auto const ipv6_multicast_min (boost::asio::ip::address_v6::from_string ("ff00::"));
	static auto const ipv6_multicast_max (boost::asio::ip::address_v6::from_string ("ff00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	if (bytes >= rfc1700_min && bytes <= rfc1700_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_1_min && bytes <= rfc5737_1_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_2_min && bytes <= rfc5737_2_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_3_min && bytes <= rfc5737_3_max)
	{
		result = true;
	}
	else if (bytes >= ipv4_multicast_min && bytes <= ipv4_multicast_max)
	{
		result = true;
	}
	else if (bytes >= rfc6890_min && bytes <= rfc6890_max)
	{
		result = true;
	}
	else if (bytes >= rfc6666_min && bytes <= rfc6666_max)
	{
		result = true;
	}
	else if (bytes >= rfc3849_min && bytes <= rfc3849_max)
	{
		result = true;
	}
	else if (bytes >= ipv6_multicast_min && bytes <= ipv6_multicast_max)
	{
		result = true;
	}
	else if (!allow_local_peers)
	{
		if (bytes >= rfc1918_1_min && bytes <= rfc1918_1_max)
		{
			result = true;
		}
		else if (bytes >= rfc1918_2_min && bytes <= rfc1918_2_max)
		{
			result = true;
		}
		else if (bytes >= rfc1918_3_min && bytes <= rfc1918_3_max)
		{
			result = true;
		}
		else if (bytes >= rfc6598_min && bytes <= rfc6598_max)
		{
			result = true;
		}
		else if (bytes >= rfc4193_min && bytes <= rfc4193_max)
		{
			result = true;
		}
	}
	return result;
}

nano::endpoint nano::transport::udp_channels::tcp_peer ()
{
	nano::endpoint result (boost::asio::ip::address_v6::any (), 0);
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.get<last_tcp_attempt_tag> ().begin ()), n (channels.get<last_tcp_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->network_version >= protocol_version_reasonable_min)
		{
			result = i->endpoint ();
			channels.get<last_tcp_attempt_tag> ().modify (i, [](channel_udp_wrapper & wrapper_a) {
				wrapper_a.channel->last_tcp_attempt = std::chrono::steady_clock::now ();
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

void nano::transport::udp_channels::receive ()
{
	if (node.config.logging.network_packet_logging ())
	{
		node.logger.try_log ("Receiving packet");
	}

	auto data (node.network.buffer_container.allocate ());

	assert (strand.running_in_this_thread ());
	socket.async_receive_from (boost::asio::buffer (data->buffer, nano::network::buffer_size), data->endpoint,
	boost::asio::bind_executor (strand,
	[this, data](boost::system::error_code const & error, std::size_t size_a) {
		if (!error && !stopped)
		{
			data->size = size_a;
			this->node.network.buffer_container.enqueue (data);
			this->receive ();
		}
		else
		{
			this->node.network.buffer_container.release (data);
			if (error)
			{
				if (this->node.config.logging.network_logging ())
				{
					this->node.logger.try_log (boost::str (boost::format ("UDP Receive error: %1%") % error.message ()));
				}
			}
			if (!stopped)
			{
				this->node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [this]() { this->receive (); });
			}
		}
	}));
}

void nano::transport::udp_channels::start ()
{
	for (size_t i = 0; i < node.config.io_threads; ++i)
	{
		boost::asio::post (strand, [this]() {
			receive ();
		});
	}
	ongoing_keepalive ();
	ongoing_syn_cookie_cleanup ();
}

void nano::transport::udp_channels::stop ()
{
	// Stop and invalidate local endpoint
	stopped = true;
	local_endpoint = nano::endpoint (boost::asio::ip::address_v6::loopback (), 0);

	// clang-format off
	boost::asio::post (strand, [this] {
		boost::system::error_code ignored;
		this->socket.close (ignored);
	});
	// clang-format on
}

nano::endpoint nano::transport::udp_channels::get_local_endpoint () const
{
	return local_endpoint;
}

namespace
{
class udp_message_visitor : public nano::message_visitor
{
public:
	udp_message_visitor (nano::node & node_a, nano::endpoint const & endpoint_a) :
	node (node_a),
	endpoint (endpoint_a)
	{
	}
	void keepalive (nano::keepalive const & message_a) override
	{
		if (!node.network.udp_channels.max_ip_connections (endpoint))
		{
			auto cookie (node.network.udp_channels.assign_syn_cookie (endpoint));
			if (cookie)
			{
				node.network.send_node_id_handshake (endpoint, *cookie, boost::none);
			}
		}
		message (message_a);
	}
	void publish (nano::publish const & message_a) override
	{
		message (message_a);
	}
	void confirm_req (nano::confirm_req const & message_a) override
	{
		message (message_a);
	}
	void confirm_ack (nano::confirm_ack const & message_a) override
	{
		message (message_a);
	}
	void bulk_pull (nano::bulk_pull const &) override
	{
		assert (false);
	}
	void bulk_pull_account (nano::bulk_pull_account const &) override
	{
		assert (false);
	}
	void bulk_push (nano::bulk_push const &) override
	{
		assert (false);
	}
	void frontier_req (nano::frontier_req const &) override
	{
		assert (false);
	}
	void node_id_handshake (nano::node_id_handshake const & message_a) override
	{
		if (node.config.logging.network_node_id_handshake_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Received node_id_handshake message from %1% with query %2% and response account %3%") % endpoint % (message_a.query ? message_a.query->to_string () : std::string ("[none]")) % (message_a.response ? message_a.response->first.to_account () : std::string ("[none]"))));
		}
		boost::optional<nano::uint256_union> out_query;
		boost::optional<nano::uint256_union> out_respond_to;
		if (message_a.query)
		{
			out_respond_to = message_a.query;
		}
		auto validated_response (false);
		if (message_a.response)
		{
			if (!node.network.udp_channels.validate_syn_cookie (endpoint, message_a.response->first, message_a.response->second))
			{
				validated_response = true;
				if (message_a.response->first != node.node_id.pub)
				{
					auto channel (node.network.udp_channels.insert (endpoint, message_a.header.version_using));
					if (channel)
					{
						channel->node_id = message_a.response->first;
					}
				}
			}
			else if (node.config.logging.network_node_id_handshake_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Failed to validate syn cookie signature %1% by %2%") % message_a.response->second.to_string () % message_a.response->first.to_account ()));
			}
		}
		if (!validated_response && node.network.udp_channels.channel (endpoint) == nullptr)
		{
			out_query = node.network.udp_channels.assign_syn_cookie (endpoint);
		}
		if (out_query || out_respond_to)
		{
			node.network.send_node_id_handshake (endpoint, out_query, out_respond_to);
		}
		message (message_a);
	}
	void message (nano::message const & message_a)
	{
		auto channel (node.network.udp_channels.channel (endpoint));
		if (channel)
		{
			channel->last_packet_received = std::chrono::steady_clock::now ();
			node.network.udp_channels.modify (channel);
			node.process_message (message_a, channel);
		}
	}
	nano::node & node;
	nano::endpoint endpoint;
	std::shared_ptr<nano::transport::channel_udp> channel;
};
}

void nano::transport::udp_channels::receive_action (nano::message_buffer * data_a)
{
	auto allowed_sender (true);
	if (data_a->endpoint == local_endpoint)
	{
		allowed_sender = false;
	}
	else if (reserved_address (data_a->endpoint, node.config.allow_local_peers))
	{
		allowed_sender = false;
	}
	if (allowed_sender)
	{
		udp_message_visitor visitor (node, data_a->endpoint);
		nano::message_parser parser (node.block_uniquer, node.vote_uniquer, visitor, node.work);
		parser.deserialize_buffer (data_a->buffer, data_a->size);
		if (parser.status != nano::message_parser::parse_status::success)
		{
			node.stats.inc (nano::stat::type::error);

			switch (parser.status)
			{
				case nano::message_parser::parse_status::insufficient_work:
					// We've already increment error count, update detail only
					node.stats.inc_detail_only (nano::stat::type::error, nano::stat::detail::insufficient_work);
					break;
				case nano::message_parser::parse_status::invalid_magic:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_magic);
					break;
				case nano::message_parser::parse_status::invalid_network:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_network);
					break;
				case nano::message_parser::parse_status::invalid_header:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_header);
					break;
				case nano::message_parser::parse_status::invalid_message_type:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_message_type);
					break;
				case nano::message_parser::parse_status::invalid_keepalive_message:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_keepalive_message);
					break;
				case nano::message_parser::parse_status::invalid_publish_message:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_publish_message);
					break;
				case nano::message_parser::parse_status::invalid_confirm_req_message:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_confirm_req_message);
					break;
				case nano::message_parser::parse_status::invalid_confirm_ack_message:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_confirm_ack_message);
					break;
				case nano::message_parser::parse_status::invalid_node_id_handshake_message:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::invalid_node_id_handshake_message);
					break;
				case nano::message_parser::parse_status::outdated_version:
					node.stats.inc (nano::stat::type::udp, nano::stat::detail::outdated_version);
					break;
				case nano::message_parser::parse_status::success:
					/* Already checked, unreachable */
					break;
			}
		}
		else
		{
			node.stats.add (nano::stat::type::traffic, nano::stat::dir::in, data_a->size);
		}
	}
	else
	{
		if (node.config.logging.network_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Reserved sender %1%") % data_a->endpoint.address ().to_string ()));
		}

		node.stats.inc_detail_only (nano::stat::type::error, nano::stat::detail::bad_sender);
	}
}

void nano::transport::udp_channels::process_packets ()
{
	while (!stopped)
	{
		auto data (node.network.buffer_container.dequeue ());
		if (data == nullptr)
		{
			break;
		}
		receive_action (data);
		node.network.buffer_container.release (data);
	}
}

std::shared_ptr<nano::transport::channel> nano::transport::udp_channels::create (nano::endpoint const & endpoint_a)
{
	return std::make_shared<nano::transport::channel_udp> (*this, endpoint_a);
}

bool nano::transport::udp_channels::not_a_peer (nano::endpoint const & endpoint_a, bool allow_local_peers)
{
	bool result (false);
	if (endpoint_a.address ().to_v6 ().is_unspecified ())
	{
		result = true;
	}
	else if (reserved_address (endpoint_a, allow_local_peers))
	{
		result = true;
	}
	else if (endpoint_a == local_endpoint)
	{
		result = true;
	}
	else if (!network_params.network.is_test_network () && max_ip_connections (endpoint_a))
	{
		result = true;
	}
	return result;
}

bool nano::transport::udp_channels::max_ip_connections (nano::endpoint const & endpoint_a)
{
	std::unique_lock<std::mutex> lock (mutex);
	bool result (channels.get<ip_address_tag> ().count (endpoint_a.address ()) >= max_peers_per_ip);
	return result;
}

bool nano::transport::udp_channels::reachout (nano::endpoint const & endpoint_a, bool allow_local_peers)
{
	// Don't contact invalid IPs
	bool error = node.network.udp_channels.not_a_peer (endpoint_a, allow_local_peers);
	if (!error)
	{
		auto endpoint_l (nano::transport::map_endpoint_to_v6 (endpoint_a));
		// Don't keepalive to nodes that already sent us something
		nano::transport::channel_udp channel (node.network.udp_channels, endpoint_l);
		error |= node.network.udp_channels.channel (endpoint_l) != nullptr;
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (attempts.find (endpoint_l));
		error |= existing != attempts.end ();
		attempts.insert ({ endpoint_l, std::chrono::steady_clock::now () });
	}
	return error;
}

std::unique_ptr<nano::seq_con_info_component> nano::transport::udp_channels::collect_seq_con_info (std::string const & name)
{
	size_t channels_count = 0;
	size_t attemps_count = 0;
	size_t syn_cookies_count = 0;
	size_t syn_cookies_per_ip_count = 0;
	{
		std::lock_guard<std::mutex> guard (mutex);
		channels_count = channels.size ();
		attemps_count = attempts.size ();
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

void nano::transport::udp_channels::purge (std::chrono::steady_clock::time_point const & cutoff_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto disconnect_cutoff (channels.get<last_packet_received_tag> ().lower_bound (cutoff_a));
	channels.get<last_packet_received_tag> ().erase (channels.get<last_packet_received_tag> ().begin (), disconnect_cutoff);
	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<1> ().lower_bound (cutoff_a));
	attempts.get<1> ().erase (attempts.get<1> ().begin (), attempts_cutoff);
}

boost::optional<nano::uint256_union> nano::transport::udp_channels::assign_syn_cookie (nano::endpoint const & endpoint)
{
	auto ip_addr (endpoint.address ());
	assert (ip_addr.is_v6 ());
	std::lock_guard<std::mutex> lock (mutex);
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

bool nano::transport::udp_channels::validate_syn_cookie (nano::endpoint const & endpoint, nano::account const & node_id, nano::signature const & sig)
{
	auto ip_addr (endpoint.address ());
	assert (ip_addr.is_v6 ());
	std::lock_guard<std::mutex> lock (mutex);
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

void nano::transport::udp_channels::purge_syn_cookies (std::chrono::steady_clock::time_point const & cutoff)
{
	std::lock_guard<std::mutex> lock (mutex);
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

void nano::transport::udp_channels::ongoing_syn_cookie_cleanup ()
{
	purge_syn_cookies (std::chrono::steady_clock::now () - syn_cookie_cutoff);
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + (syn_cookie_cutoff * 2), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.udp_channels.ongoing_syn_cookie_cleanup ();
		}
	});
}

void nano::transport::udp_channels::ongoing_keepalive ()
{
	nano::keepalive message;
	random_fill (message.peers);
	std::unique_lock<std::mutex> lock (mutex);
	auto keepalive_cutoff (channels.get<last_packet_received_tag> ().lower_bound (std::chrono::steady_clock::now () - network_params.node.period));
	for (auto i (channels.get<last_packet_received_tag> ().begin ()); i != keepalive_cutoff; ++i)
	{
		i->channel->send (message);
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + network_params.node.period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.udp_channels.ongoing_keepalive ();
		}
	});
}

std::deque<std::shared_ptr<nano::transport::channel_udp>> nano::transport::udp_channels::list (size_t count_a)
{
	std::deque<std::shared_ptr<nano::transport::channel_udp>> result;
	{
		std::lock_guard<std::mutex> lock (mutex);
		for (auto i (channels.begin ()), j (channels.end ()); i != j; ++i)
		{
			result.push_back (i->channel);
		}
	}
	random_pool::shuffle (result.begin (), result.end ());
	if (result.size () > count_a)
	{
		result.resize (count_a, nullptr);
	}
	return result;
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::deque<std::shared_ptr<nano::transport::channel_udp>> nano::transport::udp_channels::list_fanout ()
{
	auto result (list (node.network.size_sqrt ()));
	return result;
}

void nano::transport::udp_channels::modify (std::shared_ptr<nano::transport::channel_udp> channel_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<endpoint_tag> ().find (channel_a->endpoint));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [](channel_udp_wrapper &) {});
	}
}
