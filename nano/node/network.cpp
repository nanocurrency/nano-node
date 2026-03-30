#include <nano/crypto_lib/random_pool_shuffle.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/message_processor.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/portmapping.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/formatting.hpp>

using namespace std::chrono_literals;

// TODO: Return to static const and remove "disable_large_votes" when rolled out
std::size_t nano::network::confirm_req_hashes_max{ 255 };
std::size_t nano::network::confirm_ack_hashes_max{ 255 };

/*
 * network
 */

nano::network::network (nano::node & node_a, uint16_t port_a) :
	config{ node_a.config.network },
	node{ node_a },
	id{ node.network_params.network.network () },
	syn_cookies{ node.config.network.max_peers_per_ip, node.logger },
	resolver{ node.io_ctx },
	filter{ node.config.network.duplicate_filter_size, node.config.network.duplicate_filter_cutoff },
	tcp_channels{ node },
	port{ port_a }
{
	node.observers.channel_connected.add ([this] (std::shared_ptr<nano::transport::channel> const & channel) {
		node.stats.inc (nano::stat::type::network, nano::stat::detail::connected);
		node.logger.debug (nano::log::type::network, "Connected to: {}", channel);
	});
}

nano::network::~network ()
{
	// All threads must be stopped before this destructor
	debug_assert (!cleanup_thread.joinable ());
	debug_assert (!keepalive_thread.joinable ());
	debug_assert (!reachout_thread.joinable ());
	debug_assert (!reachout_cached_thread.joinable ());
	debug_assert (!reachout_preconfigured_thread.joinable ());
}

void nano::network::start ()
{
	cleanup_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::network_cleanup);
		run_cleanup ();
	});

	keepalive_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::network_keepalive);
		run_keepalive ();
	});

	if (!node.flags.disable_reachout && config.peer_reachout.count () > 0)
	{
		reachout_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_reachout ();
		});
	}
	else
	{
		node.logger.warn (nano::log::type::network, "Peer reachout is disabled");
	}

	if (!node.flags.disable_reachout && config.cached_peer_reachout.count () > 0)
	{
		reachout_cached_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_reachout_cached ();
		});
	}
	else
	{
		node.logger.warn (nano::log::type::network, "Cached peer reachout is disabled");
	}

	if (!node.flags.disable_reachout_preconfigured)
	{
		reachout_preconfigured_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_reachout_preconfigured ();
		});
	}

	if (!node.flags.disable_tcp_realtime)
	{
		tcp_channels.start ();
	}
	else
	{
		node.logger.warn (nano::log::type::network, "Realtime TCP is disabled");
	}
}

void nano::network::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	tcp_channels.stop ();
	resolver.cancel ();

	join_or_pass (keepalive_thread);
	join_or_pass (cleanup_thread);
	join_or_pass (reachout_thread);
	join_or_pass (reachout_cached_thread);
	join_or_pass (reachout_preconfigured_thread);

	port = 0;
}

void nano::network::run_cleanup ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		std::chrono::seconds const interval = node.network_params.network.is_dev_network () ? 1s : 5s;

		condition.wait_for (lock, interval);
		if (stopped)
		{
			return;
		}
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_cleanup);

		if (!node.flags.disable_connection_cleanup)
		{
			auto const cutoff = std::chrono::steady_clock::now () - node.network_params.network.cleanup_cutoff ();
			cleanup (cutoff);
		}

		auto const syn_cookie_cutoff = std::chrono::steady_clock::now () - node.network_params.network.syn_cookie_cutoff;
		syn_cookies.purge (syn_cookie_cutoff);

		filter.update (interval.count ());

		lock.lock ();
	}
}

void nano::network::run_keepalive ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, node.network_params.network.keepalive_period);
		if (stopped)
		{
			return;
		}
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_keepalive);

		flood_keepalive (0.75f);
		flood_keepalive_self (0.25f);

		tcp_channels.keepalive ();

		lock.lock ();
	}
}

void nano::network::run_reachout ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, node.network_params.network.merge_period);
		if (stopped)
		{
			return;
		}
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_reachout);

		auto keepalive = tcp_channels.sample_keepalive ();
		if (keepalive)
		{
			for (auto const & peer : keepalive->peers)
			{
				if (stopped)
				{
					return;
				}

				node.stats.inc (nano::stat::type::network, nano::stat::detail::reachout_live);

				merge_peer (peer);

				// Throttle reachout attempts
				std::this_thread::sleep_for (node.network_params.network.merge_period);
			}
		}

		lock.lock ();
	}
}

void nano::network::run_reachout_cached ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, node.network_params.network.merge_period);
		if (stopped)
		{
			return;
		}
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_reachout_cached);

		auto cached_peers = node.peer_history.peers ();
		for (auto const & peer : cached_peers)
		{
			if (stopped)
			{
				return;
			}

			node.stats.inc (nano::stat::type::network, nano::stat::detail::reachout_cached);

			merge_peer (peer);

			// Throttle reachout attempts
			std::this_thread::sleep_for (node.network_params.network.merge_period);
		}

		lock.lock ();
	}
}

void nano::network::run_reachout_preconfigured ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s);
		if (stopped)
		{
			return;
		}
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_reachout_preconfigured);

		auto const target_interval = empty ()
		? node.network_params.network.reachout_preconfigured_warmup_period
		: node.network_params.network.reachout_preconfigured_period;

		if (reachout_preconfigured_interval.elapse (target_interval))
		{
			reachout_preconfigured ();
		}

		lock.lock ();
	}
}

void nano::network::trigger_reachout ()
{
	node.stats.inc (nano::stat::type::network, nano::stat::detail::trigger_reachout);
	reachout_preconfigured_interval.reset ();
	condition.notify_all ();
}

void nano::network::reachout (std::string const & address_a, uint16_t port_a)
{
	auto node_l (node.shared_from_this ());
	resolver.async_resolve (boost::asio::ip::tcp::resolver::query (address_a, std::to_string (port_a)), [this, node_l, address_a, port_a] (boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::tcp::resolver::iterator{}); i != n; ++i)
			{
				auto endpoint (nano::transport::map_endpoint_to_v6 (i->endpoint ()));
				auto channel (find_channel (endpoint));
				if (!channel)
				{
					tcp_channels.start_tcp (endpoint);
				}
				else
				{
					send_keepalive (channel);
				}
			}
		}
		else
		{
			node.logger.error (nano::log::type::network, "Error resolving address for reachout: {}:{} ({})", address_a, port_a, ec.message ());
		}
	});
}

void nano::network::reachout_preconfigured ()
{
	if (node.config.preconfigured_peers.empty ())
	{
		return;
	}

	node.stats.inc (nano::stat::type::network, nano::stat::detail::reachout_preconfigured);

	for (auto const & peer : node.config.preconfigured_peers)
	{
		reachout (peer, node.network_params.network.default_node_port);
	}
}

void nano::network::send_keepalive (std::shared_ptr<nano::transport::channel> const & channel) const
{
	nano::messages::keepalive message{ node.network_params.network };
	random_fill (message.peers);
	channel->send (message, nano::transport::traffic_type::keepalive);
}

void nano::network::send_keepalive_self (std::shared_ptr<nano::transport::channel> const & channel) const
{
	nano::messages::keepalive message{ node.network_params.network };
	fill_keepalive_self (message.peers);
	channel->send (message, nano::transport::traffic_type::keepalive);
}

bool nano::network::check_capacity (nano::transport::traffic_type type, size_t target_count) const
{
	if (target_count == 0 || size () == 0)
	{
		return true;
	}
	auto channels = list (target_count, [type] (auto const & channel) {
		return !channel->max (type); // Only use channels that are not full for this traffic type
	});
	return !channels.empty () && channels.size () >= target_count / 2; // We need to have at least half of the target capacity available
}

bool nano::network::check_capacity_fanout (nano::transport::traffic_type type, float scale) const
{
	return check_capacity (type, fanout (scale));
}

bool nano::network::check_capacity_ratio (nano::transport::traffic_type type, float ratio) const
{
	auto const target_count = static_cast<size_t> (std::ceil (size () * ratio));
	return check_capacity (type, target_count);
}

size_t nano::network::flood_message (nano::messages::message const & message, nano::transport::traffic_type type, float scale) const
{
	auto channels = list (fanout (scale), [type] (auto const & channel) {
		return !channel->max (type); // Only use channels that are not full for this traffic type
	});
	size_t result = 0;
	for (auto const & channel : channels)
	{
		// TODO: Serialize message only once
		bool sent = channel->send (message, type);
		result += sent;
	}
	return result;
}

size_t nano::network::flood_message_all (nano::messages::message const & message, nano::transport::traffic_type type) const
{
	auto channels = list (); // Get all peers
	size_t result = 0;
	for (auto const & channel : channels)
	{
		if (!channel->max (type))
		{
			// TODO: Serialize message only once
			result += channel->send (message, type);
		}
	}
	return result;
}

size_t nano::network::flood_keepalive (float scale) const
{
	nano::messages::keepalive message{ node.network_params.network };
	random_fill (message.peers);
	return flood_message (message, nano::transport::traffic_type::keepalive, scale);
}

size_t nano::network::flood_keepalive_self (float scale) const
{
	nano::messages::keepalive message{ node.network_params.network };
	fill_keepalive_self (message.peers);
	return flood_message (message, nano::transport::traffic_type::keepalive, scale);
}

size_t nano::network::flood_vote (std::shared_ptr<nano::vote> const & vote, nano::transport::traffic_type type, bool rebroadcasted) const
{
	nano::messages::confirm_ack message{ node.network_params.network, vote, rebroadcasted };
	return flood_message (message, type);
}

size_t nano::network::flood_vote_all (std::shared_ptr<nano::vote> const & vote, nano::transport::traffic_type type, bool rebroadcasted) const
{
	nano::messages::confirm_ack message{ node.network_params.network, vote, rebroadcasted };
	return flood_message_all (message, type);
}

size_t nano::network::flood_vote_non_pr (std::shared_ptr<nano::vote> const & vote, float scale) const
{
	nano::messages::confirm_ack message{ node.network_params.network, vote };

	auto const type = transport::traffic_type::vote;

	auto channels = list_non_pr (fanout (scale), [type] (auto const & channel) {
		return !channel->max (type); // Only use channels that are not full for this traffic type
	});

	size_t result = 0;
	for (auto & channel : channels)
	{
		bool sent = channel->send (message, type);
		result += sent;
	}
	return result;
}

size_t nano::network::flood_vote_pr (std::shared_ptr<nano::vote> const & vote) const
{
	nano::messages::confirm_ack message{ node.network_params.network, vote };

	auto const type = nano::transport::traffic_type::vote;

	size_t result = 0;
	for (auto const & channel : node.rep_crawler.principal_representatives ())
	{
		bool sent = channel.channel->send (message, type);
		result += sent;
	}
	return result;
}

size_t nano::network::flood_block (std::shared_ptr<nano::block> const & block, nano::transport::traffic_type type) const
{
	nano::messages::publish message{ node.network_params.network, block };
	return flood_message (message, type);
}

size_t nano::network::flood_block_initial (std::shared_ptr<nano::block> const & block) const
{
	nano::messages::publish message{ node.network_params.network, block, /* is_originator */ true };

	size_t result = 0;
	for (auto const & rep : node.rep_crawler.principal_representatives ())
	{
		bool sent = rep.channel->send (message, nano::transport::traffic_type::block_broadcast_initial);
		result += sent;
	}
	for (auto & peer : list_non_pr (fanout (1.0)))
	{
		bool sent = peer->send (message, nano::transport::traffic_type::block_broadcast_initial);
		result += sent;
	}
	return result;
}

size_t nano::network::flood_block_all (std::shared_ptr<nano::block> const & block, nano::transport::traffic_type type) const
{
	nano::messages::publish message{ node.network_params.network, block };
	return flood_message_all (message, type);
}

void nano::network::flood_block_many (std::deque<std::shared_ptr<nano::block>> blocks, nano::transport::traffic_type type, std::chrono::milliseconds delay, std::function<void ()> callback) const
{
	if (blocks.empty ())
	{
		return;
	}

	auto block = blocks.front ();
	blocks.pop_front ();

	flood_block (block, type);

	if (!blocks.empty ())
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		node.workers.post_delayed (delay, [node_w, type, blocks = std::move (blocks), delay, callback] () mutable {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.flood_block_many (std::move (blocks), type, delay, callback);
			}
		});
	}
	else if (callback)
	{
		callback ();
	}
}

// Send keepalives to all the peers we've been notified of
void nano::network::merge_peers (std::array<nano::endpoint, 8> const & peers_a)
{
	for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
	{
		merge_peer (*i);
	}
}

bool nano::network::merge_peer (nano::endpoint const & peer)
{
	if (track_reachout (peer))
	{
		node.stats.inc (nano::stat::type::network, nano::stat::detail::merge_peer);
		node.logger.debug (nano::log::type::network, "Initiating peer merge: {}", peer);
		bool started = tcp_channels.start_tcp (peer);
		if (!started)
		{
			node.stats.inc (nano::stat::type::tcp, nano::stat::detail::merge_peer_failed);
			node.logger.debug (nano::log::type::network, "Peer merge failed: {}", peer);
		}
		return started;
	}
	return false; // Not initiated
}

bool nano::network::not_a_peer (nano::endpoint const & endpoint_a, bool allow_local_peers) const
{
	bool result (false);
	if (endpoint_a.address ().to_v6 ().is_unspecified ())
	{
		result = true;
	}
	else if (nano::transport::reserved_address (endpoint_a, allow_local_peers))
	{
		result = true;
	}
	else if (endpoint_a == endpoint ())
	{
		result = true;
	}
	return result;
}

bool nano::network::track_reachout (nano::endpoint const & endpoint_a)
{
	// Don't contact invalid IPs
	if (not_a_peer (endpoint_a, node.config.allow_local_peers))
	{
		return false;
	}
	return tcp_channels.track_reachout (endpoint_a);
}

std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list (std::size_t max_count, channel_filter filter) const
{
	auto result = tcp_channels.list (filter);
	nano::random_pool_shuffle (result.begin (), result.end ()); // Randomize returned peer order
	if (max_count > 0 && result.size () > max_count)
	{
		result.resize (max_count, nullptr);
	}
	return result;
}

std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list_non_pr (std::size_t max_count, channel_filter filter) const
{
	auto result = tcp_channels.list (filter);

	auto partition_point = std::partition (result.begin (), result.end (),
	[this] (std::shared_ptr<nano::transport::channel> const & channel) {
		return !node.rep_crawler.is_pr (channel);
	});
	result.resize (std::distance (result.begin (), partition_point));

	nano::random_pool_shuffle (result.begin (), result.end ()); // Randomize returned peer order

	if (result.size () > max_count)
	{
		result.resize (max_count, nullptr);
	}
	return result;
}

std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list (std::size_t max_count, uint8_t minimum_version) const
{
	return list (max_count, [minimum_version] (auto const & channel) { return channel->get_network_version () >= minimum_version; });
}

std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list_non_pr (std::size_t max_count, uint8_t minimum_version) const
{
	return list_non_pr (max_count, [minimum_version] (auto const & channel) { return channel->get_network_version () >= minimum_version; });
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::size_t nano::network::fanout (float scale) const
{
	auto fanout_l = std::max (static_cast<float> (config.minimum_fanout), size_log ());
	return static_cast<std::size_t> (std::ceil (scale * fanout_l));
}

std::unordered_set<std::shared_ptr<nano::transport::channel>> nano::network::random_set (std::size_t max_count, uint8_t minimum_version) const
{
	return tcp_channels.random_set (max_count, minimum_version);
}

void nano::network::random_fill (std::array<nano::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size (), 0));
	debug_assert (peers.size () <= target_a.size ());
	auto endpoint (nano::endpoint (boost::asio::ip::address_v6{}, 0));
	debug_assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		debug_assert ((*i)->get_peering_endpoint ().address ().is_v6 ());
		debug_assert (j < target_a.end ());
		*j = (*i)->get_peering_endpoint ();
	}
}

void nano::network::fill_keepalive_self (std::array<nano::endpoint, 8> & target_a) const
{
	random_fill (target_a);
	// We will clobber values in index 0 and 1 and if there are only 2 nodes in the system, these are the only positions occupied
	// Move these items to index 2 and 3 so they propagate
	target_a[2] = target_a[0];
	target_a[3] = target_a[1];
	// Replace part of message with node external address or listening port
	target_a[1] = nano::endpoint (boost::asio::ip::address_v6{}, 0); // For node v19 (response channels)
	if (node.config.external_address != boost::asio::ip::address_v6{}.to_string () && node.config.external_port != 0)
	{
		target_a[0] = nano::endpoint (boost::asio::ip::make_address_v6 (node.config.external_address), node.config.external_port);
	}
	else
	{
		auto external_address (node.port_mapping.external_address ());
		if (external_address.address () != boost::asio::ip::address_v4::any ())
		{
			target_a[0] = nano::endpoint (boost::asio::ip::address_v6{}, port);
			boost::system::error_code ec;
			auto external_v6 = boost::asio::ip::make_address_v6 (external_address.address ().to_string (), ec);
			target_a[1] = nano::endpoint (external_v6, external_address.port ());
		}
		else
		{
			target_a[0] = nano::endpoint (boost::asio::ip::address_v6{}, port);
		}
	}
}

nano::tcp_endpoint nano::network::bootstrap_peer ()
{
	return tcp_channels.bootstrap_peer ();
}

std::shared_ptr<nano::transport::channel> nano::network::find_channel (nano::endpoint const & endpoint_a)
{
	return tcp_channels.find_channel (nano::transport::map_endpoint_to_tcp (endpoint_a));
}

std::shared_ptr<nano::transport::channel> nano::network::find_node_id (nano::account const & node_id_a)
{
	return tcp_channels.find_node_id (node_id_a);
}

nano::endpoint nano::network::endpoint () const
{
	return nano::endpoint{ boost::asio::ip::address_v6::loopback (), port };
}

void nano::network::cleanup (std::chrono::steady_clock::time_point const & cutoff)
{
	tcp_channels.purge (cutoff);

	if (node.network.empty ())
	{
		disconnect_observer ();
	}
}

std::size_t nano::network::size () const
{
	return tcp_channels.size ();
}

float nano::network::size_log () const
{
	auto size_l = std::max (static_cast<size_t> (1u), size ()); // Clamp size to domain of std::log
	return static_cast<float> (std::log (size_l));
}

bool nano::network::empty () const
{
	return size () == 0;
}

void nano::network::erase (nano::transport::channel const & channel_a)
{
	auto const channel_type = channel_a.get_type ();
	if (channel_type == nano::transport::transport_type::tcp)
	{
		tcp_channels.erase (channel_a.get_remote_endpoint ());
	}
}

void nano::network::exclude (std::shared_ptr<nano::transport::channel> const & channel)
{
	// Add to peer exclusion list
	excluded_peers.add (channel->get_remote_endpoint ());

	// Disconnect
	erase (*channel);
}

bool nano::network::verify_handshake_response (const nano::messages::node_id_handshake::response_payload & response, const nano::endpoint & remote_endpoint)
{
	// Prevent connection with ourselves
	if (response.node_id == node.node_id.pub)
	{
		node.stats.inc (nano::stat::type::handshake, nano::stat::detail::invalid_node_id);
		return false; // Fail
	}

	// Prevent mismatched genesis
	auto genesis = response.genesis ();
	if (genesis && *genesis != node.network_params.ledger.genesis->hash ())
	{
		node.stats.inc (nano::stat::type::handshake, nano::stat::detail::invalid_genesis);
		return false; // Fail
	}

	auto cookie = syn_cookies.cookie (remote_endpoint);
	if (!cookie)
	{
		node.stats.inc (nano::stat::type::handshake, nano::stat::detail::missing_cookie);
		return false; // Fail
	}

	if (!response.validate (*cookie))
	{
		node.stats.inc (nano::stat::type::handshake, nano::stat::detail::invalid_signature);
		return false; // Fail
	}

	node.stats.inc (nano::stat::type::handshake, nano::stat::detail::ok);
	return true; // OK
}

std::optional<nano::messages::node_id_handshake::query_payload> nano::network::prepare_handshake_query (const nano::endpoint & remote_endpoint)
{
	if (auto cookie = syn_cookies.assign (remote_endpoint); cookie)
	{
		nano::messages::node_id_handshake::query_payload query{ *cookie };
		return query;
	}
	return std::nullopt;
}

nano::messages::node_id_handshake::response_payload nano::network::prepare_handshake_response (const nano::messages::node_id_handshake::query_payload & query, nano::messages::handshake_version version) const
{
	using handshake_version = nano::messages::handshake_version;
	using response_payload = nano::messages::node_id_handshake::response_payload;

	response_payload response{};
	response.node_id = node.node_id.pub;
	switch (version)
	{
		case handshake_version::v3:
		{
			response_payload::v3_payload v3{};
			v3.salt = nano::random_pool::generate<uint256_union> ();
			v3.genesis = node.network_params.ledger.genesis->hash ();
			v3.flags = node.get_capabilities ();
			response.ext = v3;
			break;
		}
		case handshake_version::v2:
		{
			response_payload::v2_payload v2{};
			v2.salt = nano::random_pool::generate<uint256_union> ();
			v2.genesis = node.network_params.ledger.genesis->hash ();
			response.ext = v2;
			break;
		}
		case handshake_version::v1:
			break;
	}
	response.sign (query.cookie, node.node_id);
	return response;
}

nano::container_info nano::network::container_info () const
{
	nano::container_info info;
	info.add ("tcp_channels", tcp_channels.container_info ());
	info.add ("syn_cookies", syn_cookies.container_info ());
	info.add ("excluded_peers", excluded_peers.container_info ());
	return info;
}

/*
 * syn_cookies
 */

nano::syn_cookies::syn_cookies (std::size_t max_cookies_per_ip_a, nano::logger & logger_a) :
	max_cookies_per_ip (max_cookies_per_ip_a),
	logger (logger_a)
{
}

std::optional<nano::uint256_union> nano::syn_cookies::assign (nano::endpoint const & endpoint_a)
{
	auto ip_addr (endpoint_a.address ());
	debug_assert (ip_addr.is_v6 ());
	nano::lock_guard<nano::mutex> lock{ syn_cookie_mutex };
	unsigned & ip_cookies = cookies_per_ip[ip_addr];
	std::optional<nano::uint256_union> result;
	if (ip_cookies < max_cookies_per_ip)
	{
		if (cookies.find (endpoint_a) == cookies.end ())
		{
			nano::uint256_union query;
			random_pool::generate_block (query.bytes.data (), query.bytes.size ());
			syn_cookie_info info{ query, std::chrono::steady_clock::now () };
			cookies[endpoint_a] = info;
			++ip_cookies;
			result = query;
		}
	}
	return result;
}

bool nano::syn_cookies::validate (nano::endpoint const & endpoint_a, nano::account const & node_id, nano::signature const & sig)
{
	auto ip_addr (endpoint_a.address ());
	debug_assert (ip_addr.is_v6 ());
	nano::lock_guard<nano::mutex> lock{ syn_cookie_mutex };
	auto result (true);
	auto cookie_it (cookies.find (endpoint_a));
	if (cookie_it != cookies.end () && !nano::validate_message (node_id, cookie_it->second.cookie, sig))
	{
		result = false;
		cookies.erase (cookie_it);
		unsigned & ip_cookies = cookies_per_ip[ip_addr];
		if (ip_cookies > 0)
		{
			--ip_cookies;
		}
		else
		{
			debug_assert (false && "More SYN cookies deleted than created for IP");
		}
	}
	return result;
}

void nano::syn_cookies::purge (std::chrono::steady_clock::time_point const & cutoff_a)
{
	nano::lock_guard<nano::mutex> lock{ syn_cookie_mutex };
	auto it (cookies.begin ());
	while (it != cookies.end ())
	{
		auto info (it->second);
		if (info.created_at < cutoff_a)
		{
			unsigned & per_ip = cookies_per_ip[it->first.address ()];
			if (per_ip > 0)
			{
				--per_ip;
			}
			else
			{
				debug_assert (false && "More SYN cookies deleted than created for IP");
			}
			it = cookies.erase (it);
		}
		else
		{
			++it;
		}
	}
}

std::optional<nano::uint256_union> nano::syn_cookies::cookie (const nano::endpoint & endpoint_a)
{
	auto ip_addr (endpoint_a.address ());
	debug_assert (ip_addr.is_v6 ());
	nano::lock_guard<nano::mutex> lock{ syn_cookie_mutex };
	auto cookie_it (cookies.find (endpoint_a));
	if (cookie_it != cookies.end ())
	{
		auto cookie = cookie_it->second.cookie;
		cookies.erase (cookie_it);
		unsigned & ip_cookies = cookies_per_ip[ip_addr];
		if (ip_cookies > 0)
		{
			--ip_cookies;
		}
		else
		{
			debug_assert (false && "More SYN cookies deleted than created for IP");
		}
		return cookie;
	}
	return std::nullopt;
}

std::size_t nano::syn_cookies::cookies_size () const
{
	nano::lock_guard<nano::mutex> lock{ syn_cookie_mutex };
	return cookies.size ();
}

nano::container_info nano::syn_cookies::container_info () const
{
	nano::lock_guard<nano::mutex> syn_cookie_guard{ syn_cookie_mutex };

	nano::container_info info;
	info.put ("syn_cookies", cookies.size ());
	info.put ("syn_cookies_per_ip", cookies_per_ip.size ());
	return info;
}

/*
 * network_config
 */

nano::error nano::network_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("peer_reachout", peer_reachout.count (), "Time between attempts to reach out to peers. \ntype:milliseconds");
	toml.put ("cached_peer_reachout", cached_peer_reachout.count (), "Time between attempts to reach out to cached peers. \ntype:milliseconds");
	toml.put ("max_peers_per_ip", max_peers_per_ip, "Maximum number of peers allowed from a single IP address. \ntype:size_t");
	toml.put ("max_peers_per_subnetwork", max_peers_per_subnetwork, "Maximum number of peers allowed from the same subnetwork. \ntype:size_t");
	toml.put ("duplicate_filter_size", duplicate_filter_size, "Size of the duplicate detection filter. \ntype:size_t");
	toml.put ("duplicate_filter_cutoff", duplicate_filter_cutoff, "Time in seconds before a duplicate entry expires. \ntype:uint64");
	toml.put ("minimum_fanout", minimum_fanout, "Minimum number of peers to fan out messages to. \ntype:size_t");

	return toml.get_error ();
}

nano::error nano::network_config::deserialize (nano::tomlconfig & toml)
{
	toml.get_duration ("peer_reachout", peer_reachout);
	toml.get_duration ("cached_peer_reachout", cached_peer_reachout);
	toml.get ("max_peers_per_ip", max_peers_per_ip);
	toml.get ("max_peers_per_subnetwork", max_peers_per_subnetwork);
	toml.get ("duplicate_filter_size", duplicate_filter_size);
	toml.get ("duplicate_filter_cutoff", duplicate_filter_cutoff);
	toml.get ("minimum_fanout", minimum_fanout);

	return toml.get_error ();
}