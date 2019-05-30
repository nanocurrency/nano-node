#include <nano/node/network.hpp>
#include <nano/node/node.hpp>

#include <numeric>
#include <sstream>

nano::network::network (nano::node & node_a, uint16_t port_a) :
buffer_container (node_a.stats, nano::network::buffer_size, 4096), // 2Mb receive buffer
resolver (node_a.io_ctx),
node (node_a),
udp_channels (node_a, port_a),
tcp_channels (node_a),
disconnect_observer ([]() {})
{
	boost::thread::attributes attrs;
	nano::thread_attributes::set (attrs);
	for (size_t i = 0; i < node.config.network_threads; ++i)
	{
		packet_processing_threads.push_back (boost::thread (attrs, [this]() {
			nano::thread_role::set (nano::thread_role::name::packet_processing);
			try
			{
				udp_channels.process_packets ();
			}
			catch (boost::system::error_code & ec)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, ec.message ());
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, ec.message ());
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, err.what ());
				release_assert (false);
			}
			catch (...)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, "Unknown exception");
				release_assert (false);
			}
			if (this->node.config.logging.network_packet_logging ())
			{
				this->node.logger.try_log ("Exiting packet processing thread");
			}
		}));
	}
}

nano::network::~network ()
{
	for (auto & thread : packet_processing_threads)
	{
		thread.join ();
	}
}

void nano::network::start ()
{
	ongoing_cleanup ();
	udp_channels.start ();
	tcp_channels.start ();
}

void nano::network::stop ()
{
	udp_channels.stop ();
	tcp_channels.stop ();
	resolver.cancel ();
	buffer_container.stop ();
}

void nano::network::send_keepalive (std::shared_ptr<nano::transport::channel> channel_a)
{
	nano::keepalive message;
	random_fill (message.peers);
	channel_a->send (message);
}

void nano::network::send_keepalive_self (std::shared_ptr<nano::transport::channel> channel_a)
{
	nano::keepalive message;
	if (node.config.external_address != boost::asio::ip::address_v6{} && node.config.external_port != 0)
	{
		message.peers[0] = nano::endpoint (node.config.external_address, node.config.external_port);
	}
	else
	{
		auto external_address (node.port_mapping.external_address ());
		if (external_address.address () != boost::asio::ip::address_v4::any ())
		{
			message.peers[0] = nano::endpoint (boost::asio::ip::address_v6{}, endpoint ().port ());
			message.peers[1] = external_address;
		}
		else
		{
			message.peers[0] = nano::endpoint (boost::asio::ip::address_v6{}, endpoint ().port ());
		}
	}
	channel_a->send (message);
}

void nano::network::send_node_id_handshake (std::shared_ptr<nano::transport::channel> channel_a, boost::optional<nano::uint256_union> const & query, boost::optional<nano::uint256_union> const & respond_to)
{
	boost::optional<std::pair<nano::account, nano::signature>> response (boost::none);
	if (respond_to)
	{
		response = std::make_pair (node.node_id.pub, nano::sign_message (node.node_id.prv, node.node_id.pub, *respond_to));
		assert (!nano::validate_message (response->first, *respond_to, response->second));
	}
	nano::node_id_handshake message (query, response);
	if (node.config.logging.network_node_id_handshake_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Node ID handshake sent with node ID %1% to %2%: query %3%, respond_to %4% (signature %5%)") % node.node_id.pub.to_account () % channel_a->get_endpoint () % (query ? query->to_string () : std::string ("[none]")) % (respond_to ? respond_to->to_string () : std::string ("[none]")) % (response ? response->second.to_string () : std::string ("[none]"))));
	}
	channel_a->send (message);
}

template <typename T>
bool confirm_block (nano::transaction const & transaction_a, nano::node & node_a, T & list_a, std::shared_ptr<nano::block> block_a, bool also_publish)
{
	bool result (false);
	if (node_a.config.enable_voting)
	{
		auto hash (block_a->hash ());
		// Search in cache
		auto votes (node_a.votes_cache.find (hash));
		if (votes.empty ())
		{
			// Generate new vote
			node_a.wallets.foreach_representative (transaction_a, [&result, &list_a, &node_a, &transaction_a, &hash](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
				result = true;
				auto vote (node_a.store.vote_generate (transaction_a, pub_a, prv_a, std::vector<nano::block_hash> (1, hash)));
				nano::confirm_ack confirm (vote);
				auto vote_bytes = confirm.to_bytes ();
				for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
				{
					j->get ()->send_buffer (vote_bytes, nano::stat::detail::confirm_ack);
				}
				node_a.votes_cache.add (vote);
			});
		}
		else
		{
			// Send from cache
			for (auto & vote : votes)
			{
				nano::confirm_ack confirm (vote);
				auto vote_bytes = confirm.to_bytes ();
				for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
				{
					j->get ()->send_buffer (vote_bytes, nano::stat::detail::confirm_ack);
				}
			}
		}
		// Republish if required
		if (also_publish)
		{
			nano::publish publish (block_a);
			auto publish_bytes (publish.to_bytes ());
			for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
			{
				j->get ()->send_buffer (publish_bytes, nano::stat::detail::publish);
			}
		}
	}
	return result;
}

bool confirm_block (nano::transaction const & transaction_a, nano::node & node_a, std::shared_ptr<nano::transport::channel> channel_a, std::shared_ptr<nano::block> block_a, bool also_publish)
{
	std::array<std::shared_ptr<nano::transport::channel>, 1> endpoints = { channel_a };
	auto result (confirm_block (transaction_a, node_a, endpoints, std::move (block_a), also_publish));
	return result;
}

void nano::network::confirm_hashes (nano::transaction const & transaction_a, std::shared_ptr<nano::transport::channel> channel_a, std::vector<nano::block_hash> blocks_bundle_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &blocks_bundle_a, &channel_a, &transaction_a](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, blocks_bundle_a));
			nano::confirm_ack confirm (vote);
			std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
			{
				nano::vectorstream stream (*bytes);
				confirm.serialize (stream);
			}
			channel_a->send_buffer (bytes, nano::stat::detail::confirm_ack);
			this->node.votes_cache.add (vote);
		});
	}
}

bool nano::network::send_votes_cache (std::shared_ptr<nano::transport::channel> channel_a, nano::block_hash const & hash_a)
{
	// Search in cache
	auto votes (node.votes_cache.find (hash_a));
	// Send from cache
	for (auto & vote : votes)
	{
		nano::confirm_ack confirm (vote);
		auto vote_bytes = confirm.to_bytes ();
		channel_a->send_buffer (vote_bytes, nano::stat::detail::confirm_ack);
	}
	// Returns true if votes were sent
	bool result (!votes.empty ());
	return result;
}

void nano::network::flood_message (nano::message const & message_a)
{
	auto list (list_fanout ());
	for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
	{
		(*i)->send (message_a);
	}
}

void nano::network::flood_block_batch (std::deque<std::shared_ptr<nano::block>> blocks_a, unsigned delay_a)
{
	auto block (blocks_a.front ());
	blocks_a.pop_front ();
	flood_block (block);
	if (!blocks_a.empty ())
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a + std::rand () % delay_a), [node_w, blocks_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.flood_block_batch (blocks_a, delay_a);
			}
		});
	}
}

void nano::network::broadcast_confirm_req (std::shared_ptr<nano::block> block_a)
{
	auto list (std::make_shared<std::vector<std::shared_ptr<nano::transport::channel>>> (node.rep_crawler.representative_endpoints (std::numeric_limits<size_t>::max ())));
	if (list->empty () || node.rep_crawler.total_weight () < node.config.online_weight_minimum.number ())
	{
		// broadcast request to all peers (with max limit 2 * sqrt (peers count))
		auto peers (node.network.list (std::min (static_cast<size_t> (100), 2 * node.network.size_sqrt ())));
		list->clear ();
		for (auto & peer : peers)
		{
			list->push_back (peer);
		}
	}

	/*
	 * In either case (broadcasting to all representatives, or broadcasting to
	 * all peers because there are not enough connected representatives),
	 * limit each instance to a single random up-to-32 selection.  The invoker
	 * of "broadcast_confirm_req" will be responsible for calling it again
	 * if the votes for a block have not arrived in time.
	 */
	const size_t max_endpoints = 32;
	random_pool::shuffle (list->begin (), list->end ());
	if (list->size () > max_endpoints)
	{
		list->erase (list->begin () + max_endpoints, list->end ());
	}

	broadcast_confirm_req_base (block_a, list, 0);
}

void nano::network::broadcast_confirm_req_base (std::shared_ptr<nano::block> block_a, std::shared_ptr<std::vector<std::shared_ptr<nano::transport::channel>>> endpoints_a, unsigned delay_a, bool resumption)
{
	const size_t max_reps = 10;
	if (!resumption && node.config.logging.network_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Broadcasting confirm req for block %1% to %2% representatives") % block_a->hash ().to_string () % endpoints_a->size ()));
	}
	auto count (0);
	while (!endpoints_a->empty () && count < max_reps)
	{
		auto channel (endpoints_a->back ());
		// Confirmation request with full block
		if (node.network_params.network.is_live_network ())
		{
			nano::confirm_req req (block_a);
			channel->send (req);
		}
		// Confirmation request with hash + root
		else
		{
			nano::confirm_req req (block_a->hash (), block_a->root ());
			channel->send (req);
		}
		endpoints_a->pop_back ();
		count++;
	}
	if (!endpoints_a->empty ())
	{
		delay_a += std::rand () % broadcast_interval_ms;

		std::weak_ptr<nano::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a), [node_w, block_a, endpoints_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_base (block_a, endpoints_a, delay_a, true);
			}
		});
	}
}

void nano::network::broadcast_confirm_req_batch (std::unordered_map<std::shared_ptr<nano::transport::channel>, std::vector<std::pair<nano::block_hash, nano::block_hash>>> request_bundle_a, unsigned delay_a, bool resumption)
{
	const size_t max_reps = 10;
	if (!resumption && node.config.logging.network_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Broadcasting batch confirm req to %1% representatives") % request_bundle_a.size ()));
	}
	auto count (0);
	while (!request_bundle_a.empty () && count < max_reps)
	{
		auto j (request_bundle_a.begin ());
		count++;
		std::vector<std::pair<nano::block_hash, nano::block_hash>> roots_hashes;
		// Limit max request size hash + root to 6 pairs
		while (roots_hashes.size () <= confirm_req_hashes_max && !j->second.empty ())
		{
			roots_hashes.push_back (j->second.back ());
			j->second.pop_back ();
		}
		nano::confirm_req req (roots_hashes);
		j->first->send (req);
		if (j->second.empty ())
		{
			request_bundle_a.erase (j);
		}
	}
	if (!request_bundle_a.empty ())
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a), [node_w, request_bundle_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_batch (request_bundle_a, delay_a + 50, true);
			}
		});
	}
}

void nano::network::broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<std::shared_ptr<nano::transport::channel>>>>> deque_a, unsigned delay_a)
{
	auto pair (deque_a.front ());
	deque_a.pop_front ();
	auto block (pair.first);
	// confirm_req to representatives
	auto endpoints (pair.second);
	if (!endpoints->empty ())
	{
		broadcast_confirm_req_base (block, endpoints, delay_a);
	}
	/* Continue while blocks remain
	Broadcast with random delay between delay_a & 2*delay_a */
	if (!deque_a.empty ())
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a + std::rand () % delay_a), [node_w, deque_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_batch (deque_a, delay_a);
			}
		});
	}
}

namespace
{
class network_message_visitor : public nano::message_visitor
{
public:
	network_message_visitor (nano::node & node_a, std::shared_ptr<nano::transport::channel> channel_a) :
	node (node_a),
	channel (channel_a)
	{
	}
	void keepalive (nano::keepalive const & message_a) override
	{
		if (node.config.logging.network_keepalive_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Received keepalive message from %1%") % channel->to_string ()));
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
		node.network.merge_peers (message_a.peers);
	}
	void publish (nano::publish const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Publish message from %1% for %2%") % channel->to_string () % message_a.block->hash ().to_string ()));
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in);
		if (!node.block_processor.full ())
		{
			node.process_active (message_a.block);
		}
		node.active.publish (message_a.block);
	}
	void confirm_req (nano::confirm_req const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			if (!message_a.roots_hashes.empty ())
			{
				node.logger.try_log (boost::str (boost::format ("Confirm_req message from %1% for hashes:roots %2%") % channel->to_string () % message_a.roots_string ()));
			}
			else
			{
				node.logger.try_log (boost::str (boost::format ("Confirm_req message from %1% for %2%") % channel->to_string () % message_a.block->hash ().to_string ()));
			}
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::in);
		// Don't load nodes with disabled voting
		if (node.config.enable_voting && node.wallets.reps_count)
		{
			if (message_a.block != nullptr)
			{
				auto hash (message_a.block->hash ());
				if (!node.network.send_votes_cache (channel, hash))
				{
					auto transaction (node.store.tx_begin_read ());
					auto successor (node.ledger.successor (transaction, message_a.block->qualified_root ()));
					if (successor != nullptr)
					{
						auto same_block (successor->hash () == hash);
						confirm_block (transaction, node, channel, std::move (successor), !same_block);
					}
				}
			}
			else if (!message_a.roots_hashes.empty ())
			{
				auto transaction (node.store.tx_begin_read ());
				std::vector<nano::block_hash> blocks_bundle;
				for (auto & root_hash : message_a.roots_hashes)
				{
					if (!node.network.send_votes_cache (channel, root_hash.first) && node.store.block_exists (transaction, root_hash.first))
					{
						blocks_bundle.push_back (root_hash.first);
					}
					else
					{
						nano::block_hash successor (0);
						// Search for block root
						successor = node.store.block_successor (transaction, root_hash.second);
						// Search for account root
						if (successor.is_zero () && node.store.account_exists (transaction, root_hash.second))
						{
							nano::account_info info;
							auto error (node.store.account_get (transaction, root_hash.second, info));
							assert (!error);
							successor = info.open_block;
						}
						if (!successor.is_zero ())
						{
							if (!node.network.send_votes_cache (channel, successor))
							{
								blocks_bundle.push_back (successor);
							}
							auto successor_block (node.store.block_get (transaction, successor));
							assert (successor_block != nullptr);
							nano::publish publish (successor_block);
							channel->send (publish);
						}
					}
				}
				if (!blocks_bundle.empty ())
				{
					node.network.confirm_hashes (transaction, channel, blocks_bundle);
				}
			}
		}
	}
	void confirm_ack (nano::confirm_ack const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Received confirm_ack message from %1% for %2%sequence %3%") % channel->to_string () % message_a.vote->hashes_string () % std::to_string (message_a.vote->sequence)));
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in);
		for (auto & vote_block : message_a.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<nano::block>> (vote_block));
				if (!node.block_processor.full ())
				{
					node.process_active (block);
				}
				node.active.publish (block);
			}
		}
		node.vote_processor.vote (message_a.vote, channel);
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
		node.stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
	}
	nano::node & node;
	std::shared_ptr<nano::transport::channel> channel;
};
}

void nano::network::process_message (nano::message const & message_a, std::shared_ptr<nano::transport::channel> channel_a)
{
	network_message_visitor visitor (node, channel_a);
	message_a.visit (visitor);
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
	if (!reachout (peer_a, node.config.allow_local_peers))
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		node.network.tcp_channels.start_tcp (peer_a, [node_w](std::shared_ptr<nano::transport::channel> channel_a) {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.send_keepalive (channel_a);
			}
		});
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

bool nano::network::reachout (nano::endpoint const & endpoint_a, bool allow_local_peers)
{
	// Don't contact invalid IPs
	bool error = not_a_peer (endpoint_a, allow_local_peers);
	if (!error)
	{
		error |= udp_channels.reachout (endpoint_a);
		error |= tcp_channels.reachout (endpoint_a);
	}
	return error;
}

std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list (size_t count_a)
{
	std::deque<std::shared_ptr<nano::transport::channel>> result;
	tcp_channels.list (result);
	udp_channels.list (result);
	random_pool::shuffle (result.begin (), result.end ());
	if (result.size () > count_a)
	{
		result.resize (count_a, nullptr);
	}
	return result;
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::deque<std::shared_ptr<nano::transport::channel>> nano::network::list_fanout ()
{
	auto result (list (size_sqrt ()));
	return result;
}

std::unordered_set<std::shared_ptr<nano::transport::channel>> nano::network::random_set (size_t count_a) const
{
	std::unordered_set<std::shared_ptr<nano::transport::channel>> result (tcp_channels.random_set (count_a));
	std::unordered_set<std::shared_ptr<nano::transport::channel>> udp_random (udp_channels.random_set (count_a));
	for (auto i (udp_random.begin ()), n (udp_random.end ()); i != n && result.size () < count_a * 1.5; ++i)
	{
		result.insert (*i);
	}
	while (result.size () > count_a)
	{
		result.erase (result.begin ());
	}
	return result;
}

void nano::network::random_fill (std::array<nano::endpoint, 8> & target_a) const
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

nano::tcp_endpoint nano::network::bootstrap_peer ()
{
	auto result (udp_channels.bootstrap_peer ());
	if (result == nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
	{
		result = tcp_channels.bootstrap_peer ();
	}
	return result;
}

std::shared_ptr<nano::transport::channel> nano::network::find_channel (nano::endpoint const & endpoint_a)
{
	std::shared_ptr<nano::transport::channel> result (tcp_channels.find_channel (nano::transport::map_endpoint_to_tcp (endpoint_a)));
	if (!result)
	{
		result = udp_channels.channel (endpoint_a);
	}
	return result;
}

std::shared_ptr<nano::transport::channel> nano::network::find_node_id (nano::account const & node_id_a)
{
	std::shared_ptr<nano::transport::channel> result (tcp_channels.find_node_id (node_id_a));
	if (!result)
	{
		result = udp_channels.find_node_id (node_id_a);
	}
	return result;
}

void nano::network::add_response_channels (nano::tcp_endpoint const & endpoint_a, std::vector<nano::tcp_endpoint> insert_channels)
{
	std::lock_guard<std::mutex> lock (response_channels_mutex);
	response_channels.emplace (endpoint_a, insert_channels);
}

std::shared_ptr<nano::transport::channel> nano::network::search_response_channel (nano::tcp_endpoint const & endpoint_a, nano::account const & node_id_a)
{
	// Search by node ID
	std::shared_ptr<nano::transport::channel> result (find_node_id (node_id_a));
	if (!result)
	{
		// Search in response channels
		std::unique_lock<std::mutex> lock (response_channels_mutex);
		auto existing (response_channels.find (endpoint_a));
		if (existing != response_channels.end ())
		{
			auto channels_list (existing->second);
			lock.unlock ();
			// TCP
			for (auto & i : channels_list)
			{
				auto search_channel (tcp_channels.find_channel (i));
				if (search_channel != nullptr)
				{
					result = search_channel;
					break;
				}
			}
			// UDP
			if (!result)
			{
				for (auto & i : channels_list)
				{
					auto udp_endpoint (nano::transport::map_tcp_to_endpoint (i));
					auto search_channel (udp_channels.channel (udp_endpoint));
					if (search_channel != nullptr)
					{
						result = search_channel;
						break;
					}
				}
			}
		}
	}
	return result;
}

void nano::network::remove_response_channel (nano::tcp_endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (response_channels_mutex);
	response_channels.erase (endpoint_a);
}

size_t nano::network::response_channels_size ()
{
	std::lock_guard<std::mutex> lock (response_channels_mutex);
	return response_channels.size ();
}

nano::endpoint nano::network::endpoint ()
{
	return udp_channels.get_local_endpoint ();
}

void nano::network::cleanup (std::chrono::steady_clock::time_point const & cutoff_a)
{
	tcp_channels.purge (cutoff_a);
	udp_channels.purge (cutoff_a);
	if (node.network.empty ())
	{
		disconnect_observer ();
	}
}

void nano::network::ongoing_cleanup ()
{
	cleanup (std::chrono::steady_clock::now () - node.network_params.node.cutoff);
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + node.network_params.node.period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.ongoing_cleanup ();
		}
	});
}

size_t nano::network::size () const
{
	return tcp_channels.size () + udp_channels.size ();
}

size_t nano::network::size_sqrt () const
{
	return (static_cast<size_t> (std::ceil (std::sqrt (size ()))));
}

bool nano::network::empty () const
{
	return size () == 0;
}