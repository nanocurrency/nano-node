#include <nano/crypto_lib/random_pool_shuffle.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/message_processor.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/portmapping.hpp>
#include <nano/node/telemetry.hpp>

using namespace std::chrono_literals;

// TODO: Return to static const and remove "disable_large_votes" when rolled out
std::size_t nano::network::confirm_req_hashes_max{ 255 };
std::size_t nano::network::confirm_ack_hashes_max{ 255 };

/*
 * network
 */

nano::network::network (nano::node & node, uint16_t port) :
	config{ node.config.network },
	node{ node },
	id{ nano::network_constants::active_network },
	syn_cookies{ node.config.network.max_peers_per_ip, node.logger },
	resolver{ node.io_ctx },
	filter{ node.config.network.duplicate_filter_size, node.config.network.duplicate_filter_cutoff },
	tcp_channels{ node },
	port{ port }
{
}

nano::network::~network ()
{
	// All threads must be stopped before this destructor
	debug_assert (!cleanup_thread.joinable ());
	debug_assert (!keepalive_thread.joinable ());
	debug_assert (!reachout_thread.joinable ());
	debug_assert (!reachout_cached_thread.joinable ());
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

	if (config.peer_reachout.count () > 0)
	{
		reachout_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_reachout ();
		});
	}
	if (config.cached_peer_reachout.count () > 0)
	{
		reachout_cached_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_reachout_cached ();
		});
	}

	if (!node.flags.disable_tcp_realtime)
	{
		tcp_channels.start ();
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

void nano::network::send_keepalive (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	nano::keepalive message{ node.network_params.network };
	random_fill (message.peers);
	channel_a->send (message);
}

void nano::network::send_keepalive_self (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	nano::keepalive message{ node.network_params.network };
	fill_keepalive_self (message.peers);
	channel_a->send (message);
}

void nano::network::flood_message (nano::message & message_a, nano::transport::buffer_drop_policy const drop_policy_a, float const scale_a)
{
	for (auto & i : list (fanout (scale_a)))
	{
		i->send (message_a, nullptr, drop_policy_a);
	}
}

void nano::network::flood_keepalive (float const scale_a)
{
	nano::keepalive message{ node.network_params.network };
	random_fill (message.peers);
	flood_message (message, nano::transport::buffer_drop_policy::limiter, scale_a);
}

void nano::network::flood_keepalive_self (float const scale_a)
{
	nano::keepalive message{ node.network_params.network };
	fill_keepalive_self (message.peers);
	flood_message (message, nano::transport::buffer_drop_policy::limiter, scale_a);
}

void nano::network::flood_block (std::shared_ptr<nano::block> const & block, nano::transport::buffer_drop_policy const drop_policy)
{
	nano::publish message{ node.network_params.network, block };
	flood_message (message, drop_policy);
}

void nano::network::flood_block_initial (std::shared_ptr<nano::block> const & block)
{
	nano::publish message{ node.network_params.network, block, /* is_originator */ true };
	for (auto const & rep : node.rep_crawler.principal_representatives ())
	{
		rep.channel->send (message, nullptr, nano::transport::buffer_drop_policy::no_limiter_drop);
	}
	for (auto & peer : list_non_pr (fanout (1.0)))
	{
		peer->send (message, nullptr, nano::transport::buffer_drop_policy::no_limiter_drop);
	}
}

void nano::network::flood_vote (std::shared_ptr<nano::vote> const & vote, float scale, bool rebroadcasted)
{
	nano::confirm_ack message{ node.network_params.network, vote, rebroadcasted };
	for (auto & i : list (fanout (scale)))
	{
		i->send (message, nullptr);
	}
}

void nano::network::flood_vote_pr (std::shared_ptr<nano::vote> const & vote, bool rebroadcasted)
{
	nano::confirm_ack message{ node.network_params.network, vote, rebroadcasted };
	for (auto const & i : node.rep_crawler.principal_representatives ())
	{
		i.channel->send (message, nullptr, nano::transport::buffer_drop_policy::no_limiter_drop);
	}
}

void nano::network::flood_block_many (std::deque<std::shared_ptr<nano::block>> blocks_a, std::function<void ()> callback_a, unsigned delay_a)
{
	if (!blocks_a.empty ())
	{
		auto block_l (blocks_a.front ());
		blocks_a.pop_front ();
		flood_block (block_l);
		if (!blocks_a.empty ())
		{
			std::weak_ptr<nano::node> node_w (node.shared ());
			node.workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a + std::rand () % delay_a), [node_w, blocks (std::move (blocks_a)), callback_a, delay_a] () {
				if (auto node_l = node_w.lock ())
				{
					node_l->network.flood_block_many (std::move (blocks), callback_a, delay_a);
				}
			});
		}
		else if (callback_a)
		{
			callback_a ();
		}
	}
}

void nano::network::inbound (const nano::message & message, const std::shared_ptr<nano::transport::channel> & channel)
{
	debug_assert (message.header.network == node.network_params.network.current_network);
	debug_assert (message.header.version_using >= node.network_params.network.protocol_version_min);

	node.message_processor.process (message, channel);
}

// Send keepalives to all the peers we've been notified of
void nano::network::merge_peers (std::array<nano::endpoint, 8> const & peers_a)
{
	for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
	{
		merge_peer (*i);
	}
}

void nano::network::merge_peer (nano::endpoint const & peer_a)
{
	if (track_reachout (peer_a))
	{
		node.stats.inc (nano::stat::type::network, nano::stat::detail::merge_peer);

		tcp_channels.start_tcp (peer_a);
	}
}

bool nano::network::not_a_peer (nano::endpoint const & endpoint_a, bool allow_local_peers)
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

std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list (std::size_t count_a, uint8_t minimum_version_a, bool include_tcp_temporary_channels_a)
{
	std::deque<std::shared_ptr<nano::transport::channel>> result;
	tcp_channels.list (result, minimum_version_a, include_tcp_temporary_channels_a);
	nano::random_pool_shuffle (result.begin (), result.end ());
	if (count_a > 0 && result.size () > count_a)
	{
		result.resize (count_a, nullptr);
	}
	return result;
}

std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list_non_pr (std::size_t count_a)
{
	std::deque<std::shared_ptr<nano::transport::channel>> result;
	tcp_channels.list (result);
	nano::random_pool_shuffle (result.begin (), result.end ());
	result.erase (std::remove_if (result.begin (), result.end (), [this] (std::shared_ptr<nano::transport::channel> const & channel) {
		return node.rep_crawler.is_pr (channel);
	}),
	result.end ());
	if (result.size () > count_a)
	{
		result.resize (count_a, nullptr);
	}
	return result;
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::size_t nano::network::fanout (float scale) const
{
	return static_cast<std::size_t> (std::ceil (scale * size_sqrt ()));
}

std::unordered_set<std::shared_ptr<nano::transport::channel>> nano::network::random_set (std::size_t count_a, uint8_t min_version_a, bool include_temporary_channels_a) const
{
	return tcp_channels.random_set (count_a, min_version_a, include_temporary_channels_a);
}

void nano::network::random_fill (std::array<nano::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size (), 0, false)); // Don't include channels with ephemeral remote ports
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
	return nano::endpoint (boost::asio::ip::address_v6::loopback (), port);
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

float nano::network::size_sqrt () const
{
	return static_cast<float> (std::sqrt (size ()));
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
		tcp_channels.erase (channel_a.get_tcp_endpoint ());
	}
}

void nano::network::exclude (std::shared_ptr<nano::transport::channel> const & channel)
{
	// Add to peer exclusion list
	excluded_peers.add (channel->get_tcp_endpoint ());

	// Disconnect
	erase (*channel);
}

bool nano::network::verify_handshake_response (const nano::node_id_handshake::response_payload & response, const nano::endpoint & remote_endpoint)
{
	// Prevent connection with ourselves
	if (response.node_id == node.node_id.pub)
	{
		node.stats.inc (nano::stat::type::handshake, nano::stat::detail::invalid_node_id);
		return false; // Fail
	}

	// Prevent mismatched genesis
	if (response.v2 && response.v2->genesis != node.network_params.ledger.genesis->hash ())
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

std::optional<nano::node_id_handshake::query_payload> nano::network::prepare_handshake_query (const nano::endpoint & remote_endpoint)
{
	if (auto cookie = syn_cookies.assign (remote_endpoint); cookie)
	{
		nano::node_id_handshake::query_payload query{ *cookie };
		return query;
	}
	return std::nullopt;
}

nano::node_id_handshake::response_payload nano::network::prepare_handshake_response (const nano::node_id_handshake::query_payload & query, bool v2) const
{
	nano::node_id_handshake::response_payload response{};
	response.node_id = node.node_id.pub;
	if (v2)
	{
		nano::node_id_handshake::response_payload::v2_payload response_v2{};
		response_v2.salt = nano::random_pool::generate<uint256_union> ();
		response_v2.genesis = node.network_params.ledger.genesis->hash ();
		response.v2 = response_v2;
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