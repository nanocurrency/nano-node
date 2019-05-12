#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/udp.hpp>

std::chrono::seconds constexpr nano::transport::udp_channels::syn_cookie_cutoff;

nano::transport::channel_udp::channel_udp (nano::transport::udp_channels & channels_a, nano::endpoint const & endpoint_a, unsigned network_version_a) :
channel (channels_a.node),
endpoint (endpoint_a),
channels (channels_a),
socket (std::make_shared<nano::socket> (channels_a.node.shared_from_this (), boost::none, nano::socket::concurrency::multi_writer))
{
	set_network_version (network_version_a);
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

void nano::transport::channel_udp::send_buffer (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a)
{
	set_last_packet_sent (std::chrono::steady_clock::now ());
	if (type == nano::transport::transport_type::tcp)
	{
		socket->async_write (buffer_a, callback (buffer_a, detail_a, callback_a));
	}
	else
	{
		channels.send (boost::asio::const_buffer (buffer_a->data (), buffer_a->size ()), endpoint, callback (buffer_a, detail_a, callback_a));
	}
}

std::function<void(boost::system::error_code const &, size_t)> nano::transport::channel_udp::callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	// clang-format off
	return [ buffer_a, node = std::weak_ptr<nano::node> (channels.node.shared ()), callback_a ](boost::system::error_code const & ec, size_t size_a)
	{
		if (auto node_l = node.lock ())
		{
			if (ec == boost::system::errc::host_unreachable)
			{
				node_l->stats.inc (nano::stat::type::error, nano::stat::detail::unreachable_host, nano::stat::dir::out);
			}
			if (size_a > 0)
			{
				node_l->stats.add (nano::stat::type::traffic, nano::stat::dir::out, size_a);
			}

			if (callback_a)
			{
				callback_a (ec, size_a);
			}
		}
	};
	// clang-format on
}

std::string nano::transport::channel_udp::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}

nano::transport::channel_udp::~channel_udp ()
{
	std::lock_guard<std::mutex> lk (channel_mutex);
	if (type == nano::transport::transport_type::tcp && socket != nullptr)
	{
		socket->close ();
	}
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
	if (!node.network.not_a_peer (endpoint_a, node.config.allow_local_peers))
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

nano::endpoint nano::transport::udp_channels::find_node_id (nano::account const & node_id_a)
{
	nano::endpoint result (boost::asio::ip::address_v6::any (), 0);
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<node_id_tag> ().find (node_id_a));
	if (existing != channels.get<node_id_tag> ().end ())
	{
		result = existing->endpoint ();
	}
	return result;
}

void nano::transport::udp_channels::clean_node_id (nano::endpoint const & endpoint_a, nano::account const & node_id_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<node_id_tag> ().equal_range (node_id_a));
	for (auto & record : boost::make_iterator_range (existing))
	{
		// Remove duplicate node ID for same IP address
		if (record.endpoint ().address () == endpoint_a.address () && record.endpoint ().port () != endpoint_a.port () && record.channel->get_type () == nano::transport::transport_type::udp)
		{
			channels.get<endpoint_tag> ().erase (record.endpoint ());
			break;
		}
	}
}

nano::endpoint nano::transport::udp_channels::tcp_peer ()
{
	nano::endpoint result (boost::asio::ip::address_v6::any (), 0);
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= protocol_version_reasonable_min)
		{
			result = i->endpoint ();
			channels.get<last_bootstrap_attempt_tag> ().modify (i, [](channel_udp_wrapper & wrapper_a) {
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

void nano::transport::udp_channels::receive ()
{
	if (node.config.logging.network_packet_logging ())
	{
		node.logger.try_log ("Receiving packet");
	}

	auto data (node.network.buffer_container.allocate ());

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
	std::lock_guard<std::mutex> lock (mutex);
	local_endpoint = nano::endpoint (boost::asio::ip::address_v6::loopback (), 0);

	// On test-net, close directly to avoid address-reuse issues. On livenet, close
	// through the strand as multiple IO threads may access the socket.
	// clang-format off
	if (node.network_params.network.is_test_network ())
	{
		this->close_socket ();
	}
	else
	{
		boost::asio::dispatch (strand, [this] {
			this->close_socket ();
		});
	}
	// clang-format on

	// Close all TCP sockets
	for (auto i (channels.begin ()), j (channels.end ()); i != j; ++i)
	{
		if (i->channel->get_type () == nano::transport::transport_type::tcp && i->channel->socket != nullptr)
		{
			i->channel->socket->close ();
		}
	}
}

void nano::transport::udp_channels::close_socket ()
{
	boost::system::error_code ignored;
	this->socket.close (ignored);
	this->local_endpoint = nano::endpoint (boost::asio::ip::address_v6::loopback (), 0);
}

nano::endpoint nano::transport::udp_channels::get_local_endpoint () const
{
	std::lock_guard<std::mutex> lock (mutex);
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
		node.network.udp_channels.common_keepalive (message_a, endpoint);
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
					node.network.udp_channels.clean_node_id (endpoint, message_a.response->first);
					auto new_channel (node.network.udp_channels.insert (endpoint, message_a.header.version_using));
					if (new_channel)
					{
						new_channel->set_node_id (message_a.response->first);
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
			auto channel (node.network.find_channel (endpoint));
			if (!channel)
			{
				channel = std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, endpoint);
			}
			node.network.send_node_id_handshake (channel, out_query, out_respond_to);
		}
		message (message_a);
	}
	void message (nano::message const & message_a)
	{
		auto channel (node.network.udp_channels.channel (endpoint));
		if (channel)
		{
			channel->set_last_packet_received (std::chrono::steady_clock::now ());
			node.network.udp_channels.modify (channel);
			node.process_message (message_a, channel);
		}
	}
	nano::node & node;
	nano::endpoint endpoint;
};
}

void nano::transport::udp_channels::receive_action (nano::message_buffer * data_a)
{
	auto allowed_sender (true);
	if (data_a->endpoint == local_endpoint)
	{
		allowed_sender = false;
	}
	else if (nano::transport::reserved_address (data_a->endpoint, node.config.allow_local_peers))
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

bool nano::transport::udp_channels::max_ip_connections (nano::endpoint const & endpoint_a)
{
	std::unique_lock<std::mutex> lock (mutex);
	bool result (channels.get<ip_address_tag> ().count (endpoint_a.address ()) >= max_peers_per_ip);
	return result;
}

bool nano::transport::udp_channels::reachout (nano::endpoint const & endpoint_a)
{
	// Don't overload single IP
	bool error = max_ip_connections (endpoint_a);
	if (!error)
	{
		auto endpoint_l (nano::transport::map_endpoint_to_v6 (endpoint_a));
		// Don't keepalive to nodes that already sent us something
		error |= channel (endpoint_l) != nullptr;
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
		i->channel->set_last_packet_sent (std::chrono::steady_clock::now ());
		i->channel->send (message);
	}
	// Wake up channels
	std::vector<std::shared_ptr<nano::transport::channel_udp>> sent_list;
	auto keepalive_sent_cutoff (channels.get<last_packet_sent_tag> ().lower_bound (std::chrono::steady_clock::now () - network_params.node.period));
	for (auto i (channels.get<last_packet_sent_tag> ().begin ()); i != keepalive_sent_cutoff; ++i)
	{
		i->channel->send (message);
		sent_list.push_back (i->channel);
	}
	for (auto & i : sent_list)
	{
		i->set_last_packet_sent (std::chrono::steady_clock::now ());
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

void nano::transport::udp_channels::start_tcp (std::shared_ptr<nano::transport::channel_udp> channel_a, std::function<void()> const & callback_a)
{
	auto endpoint (channel_a->get_endpoint ());
	std::weak_ptr<nano::node> node_w (node.shared ());
	channel_a->socket->async_connect (nano::to_tcp_endpoint (endpoint),
	[node_w, channel_a, callback_a](boost::system::error_code const & ec) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel_a)
			{
				// TCP node ID handshake
				channel_a->set_type (nano::transport::transport_type::tcp);
				auto endpoint (channel_a->get_endpoint ());
				auto cookie (node_l->network.udp_channels.assign_syn_cookie (endpoint));
				nano::node_id_handshake message (cookie, boost::none);
				auto bytes = message.to_bytes ();
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Node ID handshake sent with node ID %1% to %2%: query %3%") % node_l->node_id.pub.to_account () % channel_a->socket->remote_endpoint () % (*cookie).to_string ()));
				}
				std::shared_ptr<std::vector<uint8_t>> receive_buffer (std::make_shared<std::vector<uint8_t>> ());
				receive_buffer->resize (256);
				channel_a->set_last_packet_sent (std::chrono::steady_clock::now ());
				channel_a->send_buffer (bytes, nano::stat::detail::node_id_handshake, [node_w, channel_a, receive_buffer, callback_a](boost::system::error_code const & ec, size_t size_a) {
					if (auto node_l = node_w.lock ())
					{
						if (!ec && channel_a)
						{
							node_l->network.udp_channels.start_tcp_receive_header (channel_a, receive_buffer, callback_a);
						}
						else
						{
							if (node_l->config.logging.network_node_id_handshake_logging ())
							{
								node_l->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % channel_a->socket->remote_endpoint () % ec.message ()));
							}
							if (callback_a && channel_a)
							{
								channel_a->set_type (nano::transport::transport_type::udp);
								callback_a ();
							}
						}
					}
				});
			}
			else if (callback_a && channel_a)
			{
				channel_a->set_type (nano::transport::transport_type::udp);
				callback_a ();
			}
		}
	});
}

void nano::transport::udp_channels::start_tcp_receive_header (std::shared_ptr<nano::transport::channel_udp> channel_a, std::shared_ptr<std::vector<uint8_t>> receive_buffer_a, std::function<void()> const & callback_a)
{
	std::weak_ptr<nano::node> node_w (node.shared ());
	channel_a->socket->async_read (receive_buffer_a, 8, [node_w, channel_a, receive_buffer_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel_a)
			{
				assert (size_a == 8);
				nano::bufferstream type_stream (receive_buffer_a->data (), size_a);
				auto error (false);
				nano::message_header header (error, type_stream);
				if (!error && header.type == nano::message_type::node_id_handshake && header.node_id_handshake_is_response () && header.version_using >= nano::protocol_version_min)
				{
					channel_a->set_network_version (header.version_using);
					node_l->network.udp_channels.start_tcp_receive (channel_a, receive_buffer_a, callback_a);
				}
				else if (callback_a)
				{
					channel_a->set_type (nano::transport::transport_type::udp);
					callback_a ();
				}
			}
			else
			{
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Error reading node_id_handshake header from %1%: %2%") % channel_a->socket->remote_endpoint () % ec.message ()));
				}
				if (callback_a && channel_a)
				{
					channel_a->set_type (nano::transport::transport_type::udp);
					callback_a ();
				}
			}
		}
	});
}

void nano::transport::udp_channels::start_tcp_receive (std::shared_ptr<nano::transport::channel_udp> channel_a, std::shared_ptr<std::vector<uint8_t>> receive_buffer_a, std::function<void()> const & callback_a)
{
	std::weak_ptr<nano::node> node_w (node.shared ());
	channel_a->socket->async_read (receive_buffer_a, sizeof (nano::account) + sizeof (nano::signature), [node_w, channel_a, receive_buffer_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel_a)
			{
				node_l->stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
				auto error (false);
				nano::bufferstream stream (receive_buffer_a->data (), sizeof (nano::account) + sizeof (nano::signature));
				nano::message_header header (nano::message_type::node_id_handshake);
				header.flag_set (nano::message_header::node_id_handshake_response_flag);
				nano::node_id_handshake message (error, stream, header);
				if (message.response)
				{
					auto endpoint (channel_a->get_endpoint ());
					auto node_id (message.response->first);
					if (!node_l->network.udp_channels.validate_syn_cookie (endpoint, node_id, message.response->second))
					{
						if (node_id != node_l->node_id.pub)
						{
							channel_a->set_node_id (node_id);
							// Insert new node ID connection
							if (node_l->network.udp_channels.find_node_id (node_id).address () == boost::asio::ip::address_v6::any ())
							{
								node_l->network.send_keepalive_self (channel_a);
								channel_a->set_last_packet_received (std::chrono::steady_clock::now ());
								channel_a->set_last_packet_sent (std::chrono::steady_clock::now ());
								node_l->network.udp_channels.insert_tcp (channel_a);
								if (callback_a)
								{
									callback_a ();
								}
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
					node_l->logger.try_log (boost::str (boost::format ("Error reading node_id_handshake from %1%: %2%") % channel_a->get_endpoint () % ec.message ()));
				}
				if (callback_a && channel_a)
				{
					channel_a->set_type (nano::transport::transport_type::udp);
					callback_a ();
				}
			}
		}
	});
}

void nano::transport::udp_channels::insert_tcp (std::shared_ptr<nano::transport::channel_udp> channel_a)
{
	auto endpoint (channel_a->get_endpoint ());
	assert (endpoint.address ().is_v6 ());
	if (!node.network.not_a_peer (endpoint, node.config.allow_local_peers))
	{
		std::unique_lock<std::mutex> lock (mutex);
		auto existing (channels.get<endpoint_tag> ().find (endpoint));
		if (existing == channels.get<endpoint_tag> ().end ())
		{
			channels.get<endpoint_tag> ().insert ({ channel_a });
			lock.unlock ();
			node.network.channel_observer (channel_a);
		}
	}
}

void nano::transport::udp_channels::common_keepalive (nano::keepalive const & message_a, nano::endpoint const & endpoint_a, nano::transport::transport_type type, bool keepalive_first)
{
	if (!max_ip_connections (endpoint_a))
	{
		auto cookie (assign_syn_cookie (endpoint_a));
		if (cookie && type == nano::transport::transport_type::udp)
		{
			// New connection
			auto find_channel (channel (endpoint_a));
			if (find_channel)
			{
				node.network.send_node_id_handshake (find_channel, *cookie, boost::none);
				node.network.send_keepalive_self (find_channel);
			}
			else
			{
				find_channel = std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, endpoint_a);
				node.network.send_node_id_handshake (find_channel, *cookie, boost::none);
			}
		}
		// Check for special node port data
		std::vector<nano::endpoint> insert_response_channels;
		auto peer0 (message_a.peers[0]);
		auto peer1 (message_a.peers[1]);
		if (peer0.address () == boost::asio::ip::address_v6{} && peer0.port () != 0)
		{
			nano::endpoint new_endpoint (endpoint_a.address (), peer0.port ());
			node.network.merge_peer (new_endpoint);
			if (type == nano::transport::transport_type::tcp && keepalive_first)
			{
				insert_response_channels.push_back (new_endpoint);
			}
		}
		if (peer1.address () != boost::asio::ip::address_v6{} && peer1.port () != 0 && type == nano::transport::transport_type::tcp && keepalive_first)
		{
			insert_response_channels.push_back (peer1);
		}
		// Insert preferred response channels from first TCP keepalive
		if (!insert_response_channels.empty ())
		{
			nano::endpoint endpoint (endpoint_a);
			add_response_channels (nano::to_tcp_endpoint (endpoint), insert_response_channels);
		}
	}
}

void nano::transport::udp_channels::add_response_channels (nano::tcp_endpoint const & endpoint_a, std::vector<nano::endpoint> insert_channels)
{
	std::lock_guard<std::mutex> lock (mutex);
	response_channels.emplace (endpoint_a, insert_channels);
}

std::shared_ptr<nano::transport::channel_udp> nano::transport::udp_channels::search_response_channel (nano::tcp_endpoint const & endpoint_a)
{
	std::shared_ptr<nano::transport::channel_udp> response;
	std::unique_lock<std::mutex> lock (mutex);
	auto existing (response_channels.find (endpoint_a));
	if (existing != response_channels.end ())
	{
		auto channels_list (existing->second);
		lock.unlock ();
		for (auto & i : channels_list)
		{
			auto search_channel (channel (i));
			if (search_channel != nullptr)
			{
				response = search_channel;
				break;
			}
		}
	}
	return response;
}

void nano::transport::udp_channels::remove_response_channel (nano::tcp_endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	response_channels.erase (endpoint_a);
}

size_t nano::transport::udp_channels::response_channels_size () const
{
	std::lock_guard<std::mutex> lock (mutex);
	return response_channels.size ();
}
