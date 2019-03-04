#include <nano/node/node.hpp>

#include <nano/lib/interface.h>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/node/rpc.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <sstream>

#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>

double constexpr nano::node::price_max;
double constexpr nano::node::free_cutoff;
std::chrono::seconds constexpr nano::node::period;
std::chrono::seconds constexpr nano::node::cutoff;
std::chrono::seconds constexpr nano::node::syn_cookie_cutoff;
std::chrono::minutes constexpr nano::node::backup_interval;
std::chrono::seconds constexpr nano::node::search_pending_interval;
std::chrono::seconds constexpr nano::node::peer_interval;
std::chrono::hours constexpr nano::node::unchecked_cleanup_interval;
std::chrono::milliseconds constexpr nano::node::process_confirmed_interval;

int constexpr nano::port_mapping::mapping_timeout;
int constexpr nano::port_mapping::check_timeout;
unsigned constexpr nano::active_transactions::request_interval_ms;
size_t constexpr nano::active_transactions::max_broadcast_queue;
size_t constexpr nano::block_arrival::arrival_size_min;
std::chrono::seconds constexpr nano::block_arrival::arrival_time_min;
uint64_t constexpr nano::online_reps::weight_period;
uint64_t constexpr nano::online_reps::weight_samples;

namespace nano
{
extern unsigned char nano_bootstrap_weights[];
extern size_t nano_bootstrap_weights_size;
}

nano::network::network (nano::node & node_a, uint16_t port) :
buffer_container (node_a.stats, nano::network::buffer_size, 4096), // 2Mb receive buffer
socket (node_a.io_ctx, nano::endpoint (boost::asio::ip::address_v6::any (), port)),
resolver (node_a.io_ctx),
node (node_a),
on (true)
{
	boost::thread::attributes attrs;
	nano::thread_attributes::set (attrs);
	for (size_t i = 0; i < node.config.network_threads; ++i)
	{
		packet_processing_threads.push_back (boost::thread (attrs, [this]() {
			nano::thread_role::set (nano::thread_role::name::packet_processing);
			try
			{
				process_packets ();
			}
			catch (boost::system::error_code & ec)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << ec.message ();
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << ec.message ();
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << err.what ();
				release_assert (false);
			}
			catch (...)
			{
				BOOST_LOG (this->node.log) << FATAL_LOG_PREFIX << "Unknown exception";
				release_assert (false);
			}
			if (this->node.config.logging.network_packet_logging ())
			{
				BOOST_LOG (this->node.log) << "Exiting packet processing thread";
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
	for (size_t i = 0; i < node.config.io_threads; ++i)
	{
		receive ();
	}
}

void nano::network::receive ()
{
	if (node.config.logging.network_packet_logging ())
	{
		BOOST_LOG (node.log) << "Receiving packet";
	}
	std::unique_lock<std::mutex> lock (socket_mutex);
	auto data (buffer_container.allocate ());
	socket.async_receive_from (boost::asio::buffer (data->buffer, nano::network::buffer_size), data->endpoint, [this, data](boost::system::error_code const & error, size_t size_a) {
		if (!error && this->on)
		{
			data->size = size_a;
			this->buffer_container.enqueue (data);
			this->receive ();
		}
		else
		{
			this->buffer_container.release (data);
			if (error)
			{
				if (this->node.config.logging.network_logging ())
				{
					BOOST_LOG (this->node.log) << boost::str (boost::format ("UDP Receive error: %1%") % error.message ());
				}
			}
			if (this->on)
			{
				this->node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [this]() { this->receive (); });
			}
		}
	});
}

void nano::network::process_packets ()
{
	auto local_endpoint (endpoint ());
	while (on.load ())
	{
		auto data (buffer_container.dequeue ());
		if (data == nullptr)
		{
			break;
		}
		//std::cerr << data->endpoint.address ().to_string ();
		receive_action (data, local_endpoint);
		buffer_container.release (data);
	}
}

void nano::network::stop ()
{
	on = false;
	std::unique_lock<std::mutex> lock (socket_mutex);
	if (socket.is_open ())
	{
		socket.close ();
	}
	resolver.cancel ();
	buffer_container.stop ();
}

void nano::network::send_keepalive (nano::endpoint const & endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	nano::keepalive message;
	node.peers.random_fill (message.peers);
	auto bytes = message.to_bytes ();
	if (node.config.logging.network_keepalive_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Keepalive req sent to %1%") % endpoint_a);
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w, endpoint_a](boost::system::error_code const & ec, size_t) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_keepalive_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending keepalive to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::out);
			}
		}
	});
}

void nano::node::keepalive (std::string const & address_a, uint16_t port_a, bool preconfigured_peer_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a, preconfigured_peer_a](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
			{
				auto endpoint (nano::map_endpoint_to_v6 (i->endpoint ()));
				node_l->send_keepalive (endpoint);
				if (preconfigured_peer_a)
				{
					node_l->peers.insert (endpoint, nano::protocol_version, true);
				}
			}
		}
		else
		{
			BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ());
		}
	});
}

void nano::network::send_node_id_handshake (nano::endpoint const & endpoint_a, boost::optional<nano::uint256_union> const & query, boost::optional<nano::uint256_union> const & respond_to)
{
	assert (endpoint_a.address ().is_v6 ());
	boost::optional<std::pair<nano::account, nano::signature>> response (boost::none);
	if (respond_to)
	{
		response = std::make_pair (node.node_id.pub, nano::sign_message (node.node_id.prv, node.node_id.pub, *respond_to));
		assert (!nano::validate_message (response->first, *respond_to, response->second));
	}
	nano::node_id_handshake message (query, response);
	auto bytes = message.to_bytes ();
	if (node.config.logging.network_node_id_handshake_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Node ID handshake sent with node ID %1% to %2%: query %3%, respond_to %4% (signature %5%)") % node.node_id.pub.to_account () % endpoint_a % (query ? query->to_string () : std::string ("[none]")) % (respond_to ? respond_to->to_string () : std::string ("[none]")) % (response ? response->second.to_string () : std::string ("[none]")));
	}
	node.stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::out);
	std::weak_ptr<nano::node> node_w (node.shared ());
	send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w, endpoint_a](boost::system::error_code const & ec, size_t) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_node_id_handshake_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending node ID handshake to %1% %2%") % endpoint_a % ec.message ());
			}
		}
	});
}

void nano::network::republish (nano::block_hash const & hash_a, std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::endpoint endpoint_a)
{
	if (node.config.logging.network_publish_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Publishing %1% to %2%") % hash_a.to_string () % endpoint_a);
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	send_buffer (buffer_a->data (), buffer_a->size (), endpoint_a, [node_w, endpoint_a](boost::system::error_code const & ec, size_t size) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending publish to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out);
			}
		}
	});
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
					node_a.network.confirm_send (confirm, vote_bytes, *j);
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
					node_a.network.confirm_send (confirm, vote_bytes, *j);
				}
			}
		}
		// Republish if required
		if (also_publish)
		{
			nano::publish publish (block_a);
			std::shared_ptr<std::vector<uint8_t>> publish_bytes;
			publish_bytes = publish.to_bytes ();
			for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
			{
				node_a.network.republish (hash, publish_bytes, *j);
			}
		}
	}
	return result;
}

bool confirm_block (nano::transaction const & transaction_a, nano::node & node_a, nano::endpoint & peer_a, std::shared_ptr<nano::block> block_a, bool also_publish)
{
	std::array<nano::endpoint, 1> endpoints;
	endpoints[0] = peer_a;
	auto result (confirm_block (transaction_a, node_a, endpoints, std::move (block_a), also_publish));
	return result;
}

void nano::network::confirm_hashes (nano::transaction const & transaction_a, nano::endpoint const & peer_a, std::vector<nano::block_hash> blocks_bundle_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &blocks_bundle_a, &peer_a, &transaction_a](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, blocks_bundle_a));
			nano::confirm_ack confirm (vote);
			std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
			{
				nano::vectorstream stream (*bytes);
				confirm.serialize (stream);
			}
			this->node.network.confirm_send (confirm, bytes, peer_a);
			this->node.votes_cache.add (vote);
		});
	}
}

bool nano::network::send_votes_cache (nano::block_hash const & hash_a, nano::endpoint const & peer_a)
{
	// Search in cache
	auto votes (node.votes_cache.find (hash_a));
	// Send from cache
	for (auto & vote : votes)
	{
		nano::confirm_ack confirm (vote);
		auto vote_bytes = confirm.to_bytes ();
		confirm_send (confirm, vote_bytes, peer_a);
	}
	// Returns true if votes were sent
	bool result (!votes.empty ());
	return result;
}

void nano::network::republish_block (std::shared_ptr<nano::block> block)
{
	auto hash (block->hash ());
	auto list (node.peers.list_fanout ());
	nano::publish message (block);
	auto bytes = message.to_bytes ();
	for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
	{
		republish (hash, bytes, *i);
	}
	if (node.config.logging.network_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% was republished to peers") % hash.to_string ());
	}
}

void nano::network::republish_block (std::shared_ptr<nano::block> block, nano::endpoint const & peer_a)
{
	auto hash (block->hash ());
	nano::publish message (block);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	republish (hash, std::make_shared<std::vector<uint8_t>> (bytes), peer_a);
	if (node.config.logging.network_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% was republished to peer") % hash.to_string ());
	}
}

void nano::network::republish_block_batch (std::deque<std::shared_ptr<nano::block>> blocks_a, unsigned delay_a)
{
	auto block (blocks_a.front ());
	blocks_a.pop_front ();
	republish_block (block);
	if (!blocks_a.empty ())
	{
		std::weak_ptr<nano::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a + std::rand () % delay_a), [node_w, blocks_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.republish_block_batch (blocks_a, delay_a);
			}
		});
	}
}

// In order to rate limit network traffic we republish:
// 1) Only if they are a non-replay vote of a block that's actively settling. Settling blocks are limited by block PoW
// 2) The rep has a weight > Y to prevent creating a lot of small-weight accounts to send out votes
// 3) Only if a vote for this block from this representative hasn't been received in the previous X second.
//    This prevents rapid publishing of votes with increasing sequence numbers.
//
// These rules are implemented by the caller, not this function.
void nano::network::republish_vote (std::shared_ptr<nano::vote> vote_a)
{
	nano::confirm_ack confirm (vote_a);
	auto bytes = confirm.to_bytes ();
	auto list (node.peers.list_fanout ());
	for (auto j (list.begin ()), m (list.end ()); j != m; ++j)
	{
		node.network.confirm_send (confirm, bytes, *j);
	}
}

void nano::network::broadcast_confirm_req (std::shared_ptr<nano::block> block_a)
{
	auto list (std::make_shared<std::vector<nano::peer_information>> (node.peers.representatives (std::numeric_limits<size_t>::max ())));
	if (list->empty () || node.peers.total_weight () < node.config.online_weight_minimum.number ())
	{
		// broadcast request to all peers (with max limit 2 * sqrt (peers count))
		list = std::make_shared<std::vector<nano::peer_information>> (node.peers.list_vector (std::min (static_cast<size_t> (100), 2 * node.peers.size_sqrt ())));
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

void nano::network::broadcast_confirm_req_base (std::shared_ptr<nano::block> block_a, std::shared_ptr<std::vector<nano::peer_information>> endpoints_a, unsigned delay_a, bool resumption)
{
	const size_t max_reps = 10;
	if (!resumption && node.config.logging.network_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Broadcasting confirm req for block %1% to %2% representatives") % block_a->hash ().to_string () % endpoints_a->size ());
	}
	auto count (0);
	while (!endpoints_a->empty () && count < max_reps)
	{
		send_confirm_req (endpoints_a->back ().endpoint, block_a);
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

void nano::network::broadcast_confirm_req_batch (std::unordered_map<nano::endpoint, std::vector<std::pair<nano::block_hash, nano::block_hash>>> request_bundle_a, unsigned delay_a, bool resumption)
{
	const size_t max_reps = 10;
	if (!resumption && node.config.logging.network_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Broadcasting batch confirm req to %1% representatives") % request_bundle_a.size ());
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
		send_confirm_req_hashes (j->first, roots_hashes);
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

void nano::network::broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<nano::peer_information>>>> deque_a, unsigned delay_a)
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

void nano::network::send_confirm_req (nano::endpoint const & endpoint_a, std::shared_ptr<nano::block> block)
{
	nano::confirm_req message (block);
	auto bytes = message.to_bytes ();
	if (node.config.logging.network_message_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm req to %1%") % endpoint_a);
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.stats.inc (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out);
	send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w](boost::system::error_code const & ec, size_t size) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending confirm request: %1%") % ec.message ());
			}
		}
	});
}

void nano::network::send_confirm_req_hashes (nano::endpoint const & endpoint_a, std::vector<std::pair<nano::block_hash, nano::block_hash>> const & roots_hashes_a)
{
	nano::confirm_req message (roots_hashes_a);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	if (node.config.logging.network_message_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm req hashes to %1%") % endpoint_a);
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.stats.inc (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out);
	send_buffer (bytes.data (), bytes.size (), endpoint_a, [node_w](boost::system::error_code const & ec, size_t size) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending confirm request: %1%") % ec.message ());
			}
		}
	});
}

template <typename T>
void rep_query (nano::node & node_a, T const & peers_a)
{
	auto transaction (node_a.store.tx_begin_read ());
	std::shared_ptr<nano::block> block (node_a.store.block_random (transaction));
	auto hash (block->hash ());
	node_a.rep_crawler.add (hash);
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		node_a.peers.rep_request (*i);
		node_a.network.send_confirm_req (*i, block);
	}
	std::weak_ptr<nano::node> node_w (node_a.shared ());
	node_a.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->rep_crawler.remove (hash);
		}
	});
}

void rep_query (nano::node & node_a, nano::endpoint const & peers_a)
{
	std::array<nano::endpoint, 1> peers;
	peers[0] = peers_a;
	rep_query (node_a, peers);
}

namespace
{
class network_message_visitor : public nano::message_visitor
{
public:
	network_message_visitor (nano::node & node_a, nano::endpoint const & sender_a) :
	node (node_a),
	sender (sender_a)
	{
	}
	virtual ~network_message_visitor () = default;
	void keepalive (nano::keepalive const & message_a) override
	{
		if (node.config.logging.network_keepalive_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received keepalive message from %1%") % sender);
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
		if (node.peers.contacted (sender, message_a.header.version_using))
		{
			auto endpoint_l (nano::map_endpoint_to_v6 (sender));
			auto cookie (node.peers.assign_syn_cookie (endpoint_l));
			if (cookie)
			{
				node.network.send_node_id_handshake (endpoint_l, *cookie, boost::none);
			}
		}
		node.network.merge_peers (message_a.peers);
	}
	void publish (nano::publish const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Publish message from %1% for %2%") % sender % message_a.block->hash ().to_string ());
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
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
				BOOST_LOG (node.log) << boost::str (boost::format ("Confirm_req message from %1% for hashes:roots %2%") % sender % message_a.roots_string ());
			}
			else
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Confirm_req message from %1% for %2%") % sender % message_a.block->hash ().to_string ());
			}
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
		// Don't load nodes with disabled voting
		if (node.config.enable_voting && node.wallets.reps_count)
		{
			if (message_a.block != nullptr)
			{
				auto hash (message_a.block->hash ());
				if (!node.network.send_votes_cache (hash, sender))
				{
					auto transaction (node.store.tx_begin_read ());
					auto successor (node.ledger.successor (transaction, nano::uint512_union (message_a.block->previous (), message_a.block->root ())));
					if (successor != nullptr)
					{
						auto same_block (successor->hash () == hash);
						confirm_block (transaction, node, sender, std::move (successor), !same_block);
					}
				}
			}
			else if (!message_a.roots_hashes.empty ())
			{
				auto transaction (node.store.tx_begin_read ());
				std::vector<nano::block_hash> blocks_bundle;
				for (auto & root_hash : message_a.roots_hashes)
				{
					if (!node.network.send_votes_cache (root_hash.first, sender) && node.store.block_exists (transaction, root_hash.first))
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
							if (!node.network.send_votes_cache (successor, sender))
							{
								blocks_bundle.push_back (successor);
							}
							auto successor_block (node.store.block_get (transaction, successor));
							assert (successor_block != nullptr);
							node.network.republish_block (std::move (successor_block), sender);
						}
					}
				}
				if (!blocks_bundle.empty ())
				{
					node.network.confirm_hashes (transaction, sender, blocks_bundle);
				}
			}
		}
	}
	void confirm_ack (nano::confirm_ack const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received confirm_ack message from %1% for %2%sequence %3%") % sender % message_a.vote->hashes_string () % std::to_string (message_a.vote->sequence));
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
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
		node.vote_processor.vote (message_a.vote, sender);
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
			BOOST_LOG (node.log) << boost::str (boost::format ("Received node_id_handshake message from %1% with query %2% and response account %3%") % sender % (message_a.query ? message_a.query->to_string () : std::string ("[none]")) % (message_a.response ? message_a.response->first.to_account () : std::string ("[none]")));
		}
		auto endpoint_l (nano::map_endpoint_to_v6 (sender));
		boost::optional<nano::uint256_union> out_query;
		boost::optional<nano::uint256_union> out_respond_to;
		if (message_a.query)
		{
			out_respond_to = message_a.query;
		}
		auto validated_response (false);
		if (message_a.response)
		{
			if (!node.peers.validate_syn_cookie (endpoint_l, message_a.response->first, message_a.response->second))
			{
				validated_response = true;
				if (message_a.response->first != node.node_id.pub)
				{
					node.peers.insert (endpoint_l, message_a.header.version_using, false, message_a.response->first);
				}
			}
			else if (node.config.logging.network_node_id_handshake_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Failed to validate syn cookie signature %1% by %2%") % message_a.response->second.to_string () % message_a.response->first.to_account ());
			}
		}
		if (!validated_response && !node.peers.known_peer (endpoint_l))
		{
			out_query = node.peers.assign_syn_cookie (endpoint_l);
		}
		if (out_query || out_respond_to)
		{
			node.network.send_node_id_handshake (sender, out_query, out_respond_to);
		}
		node.stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
	}
	nano::node & node;
	nano::endpoint sender;
};
}

void nano::network::receive_action (nano::udp_data * data_a, nano::endpoint const & local_endpoint_a)
{
	auto allowed_sender (true);
	if (!on)
	{
		allowed_sender = false;
	}
	else if (data_a->endpoint == local_endpoint_a)
	{
		allowed_sender = false;
	}
	else if (nano::reserved_address (data_a->endpoint, false) && !node.config.allow_local_peers)
	{
		allowed_sender = false;
	}
	if (allowed_sender)
	{
		network_message_visitor visitor (node, data_a->endpoint);
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

			if (node.config.logging.network_logging () && parser.status != nano::message_parser::parse_status::outdated_version)
			{
				BOOST_LOG (node.log) << "Could not parse message.  Error: " << parser.status_string ();
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
			BOOST_LOG (node.log) << boost::str (boost::format ("Reserved sender %1%") % data_a->endpoint.address ().to_string ());
		}

		node.stats.inc_detail_only (nano::stat::type::error, nano::stat::detail::bad_sender);
	}
}

// Send keepalives to all the peers we've been notified of
void nano::network::merge_peers (std::array<nano::endpoint, 8> const & peers_a)
{
	for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
	{
		if (!node.peers.reachout (*i))
		{
			send_keepalive (*i);
		}
	}
}

bool nano::operation::operator> (nano::operation const & other_a) const
{
	return wakeup > other_a.wakeup;
}

nano::alarm::alarm (boost::asio::io_context & io_ctx_a) :
io_ctx (io_ctx_a),
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::alarm);
	run ();
})
{
}

nano::alarm::~alarm ()
{
	add (std::chrono::steady_clock::now (), nullptr);
	thread.join ();
}

void nano::alarm::run ()
{
	std::unique_lock<std::mutex> lock (mutex);
	auto done (false);
	while (!done)
	{
		if (!operations.empty ())
		{
			auto & operation (operations.top ());
			if (operation.function)
			{
				if (operation.wakeup <= std::chrono::steady_clock::now ())
				{
					io_ctx.post (operation.function);
					operations.pop ();
				}
				else
				{
					auto wakeup (operation.wakeup);
					condition.wait_until (lock, wakeup);
				}
			}
			else
			{
				done = true;
			}
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void nano::alarm::add (std::chrono::steady_clock::time_point const & wakeup_a, std::function<void()> const & operation)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		operations.push (nano::operation ({ wakeup_a, operation }));
	}
	condition.notify_all ();
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (alarm & alarm, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (alarm.mutex);
		count = alarm.operations.size ();
	}
	auto sizeof_element = sizeof (decltype (alarm.operations)::value_type);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "operations", count, sizeof_element }));
	return composite;
}
}

nano::node_init::node_init () :
block_store_init (false),
wallet_init (false)
{
}

bool nano::node_init::error ()
{
	return block_store_init || wallet_init || wallets_store_init;
}

nano::vote_processor::vote_processor (nano::node & node_a) :
node (node_a),
started (false),
stopped (false),
active (false),
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::vote_processing);
	process_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

void nano::vote_processor::process_loop ()
{
	std::chrono::steady_clock::time_point start_time, end_time;
	std::chrono::steady_clock::duration elapsed_time;
	std::chrono::milliseconds elapsed_time_ms;
	uint64_t elapsed_time_ms_int;
	bool log_this_iteration;

	std::unique_lock<std::mutex> lock (mutex);
	started = true;

	lock.unlock ();
	condition.notify_all ();
	lock.lock ();

	while (!stopped)
	{
		if (!votes.empty ())
		{
			std::deque<std::pair<std::shared_ptr<nano::vote>, nano::endpoint>> votes_l;
			votes_l.swap (votes);

			log_this_iteration = false;
			if (node.config.logging.network_logging () && votes_l.size () > 50)
			{
				/*
				 * Only log the timing information for this iteration if
				 * there are a sufficient number of items for it to be relevant
				 */
				log_this_iteration = true;
				start_time = std::chrono::steady_clock::now ();
			}
			active = true;
			lock.unlock ();
			verify_votes (votes_l);
			{
				std::unique_lock<std::mutex> active_single_lock (node.active.mutex);
				auto transaction (node.store.tx_begin_read ());
				uint64_t count (1);
				for (auto & i : votes_l)
				{
					vote_blocking (transaction, i.first, i.second, true);
					// Free active_transactions mutex each 100 processed votes
					if (count % 100 == 0)
					{
						active_single_lock.unlock ();
						active_single_lock.lock ();
					}
					count++;
				}
			}
			lock.lock ();
			active = false;

			lock.unlock ();
			condition.notify_all ();
			lock.lock ();

			if (log_this_iteration)
			{
				end_time = std::chrono::steady_clock::now ();
				elapsed_time = end_time - start_time;
				elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds> (elapsed_time);
				elapsed_time_ms_int = elapsed_time_ms.count ();

				if (elapsed_time_ms_int >= 100)
				{
					/*
					 * If the time spent was less than 100ms then
					 * the results are probably not useful as well,
					 * so don't spam the logs.
					 */
					BOOST_LOG (node.log) << boost::str (boost::format ("Processed %1% votes in %2% milliseconds (rate of %3% votes per second)") % votes_l.size () % elapsed_time_ms_int % ((votes_l.size () * 1000ULL) / elapsed_time_ms_int));
				}
			}
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void nano::vote_processor::vote (std::shared_ptr<nano::vote> vote_a, nano::endpoint endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		bool process (false);
		/* Random early delection levels
		Always process votes for test network (process = true)
		Stop processing with max 144 * 1024 votes */
		if (!nano::is_test_network)
		{
			// Level 0 (< 0.1%)
			if (votes.size () < 96 * 1024)
			{
				process = true;
			}
			// Level 1 (0.1-1%)
			else if (votes.size () < 112 * 1024)
			{
				process = (representatives_1.find (vote_a->account) != representatives_1.end ());
			}
			// Level 2 (1-5%)
			else if (votes.size () < 128 * 1024)
			{
				process = (representatives_2.find (vote_a->account) != representatives_2.end ());
			}
			// Level 3 (> 5%)
			else if (votes.size () < 144 * 1024)
			{
				process = (representatives_3.find (vote_a->account) != representatives_3.end ());
			}
		}
		else
		{
			// Process for test network
			process = true;
		}
		if (process)
		{
			votes.push_back (std::make_pair (vote_a, endpoint_a));

			lock.unlock ();
			condition.notify_all ();
			lock.lock ();
		}
		else
		{
			node.stats.inc (nano::stat::type::vote, nano::stat::detail::vote_overflow);
			if (node.config.logging.vote_logging ())
			{
				BOOST_LOG (node.log) << "Votes overflow";
			}
		}
	}
}

void nano::vote_processor::verify_votes (std::deque<std::pair<std::shared_ptr<nano::vote>, nano::endpoint>> & votes_a)
{
	auto size (votes_a.size ());
	std::vector<unsigned char const *> messages;
	messages.reserve (size);
	std::vector<nano::uint256_union> hashes;
	hashes.reserve (size);
	std::vector<size_t> lengths (size, sizeof (nano::uint256_union));
	std::vector<unsigned char const *> pub_keys;
	pub_keys.reserve (size);
	std::vector<unsigned char const *> signatures;
	signatures.reserve (size);
	std::vector<int> verifications;
	verifications.resize (size);
	for (auto & vote : votes_a)
	{
		hashes.push_back (vote.first->hash ());
		messages.push_back (hashes.back ().bytes.data ());
		pub_keys.push_back (vote.first->account.bytes.data ());
		signatures.push_back (vote.first->signature.bytes.data ());
	}
	nano::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data () };
	node.checker.verify (check);
	std::remove_reference_t<decltype (votes_a)> result;
	auto i (0);
	for (auto & vote : votes_a)
	{
		assert (verifications[i] == 1 || verifications[i] == 0);
		if (verifications[i] == 1)
		{
			result.push_back (vote);
		}
		++i;
	}
	votes_a.swap (result);
}

// node.active.mutex lock required
nano::vote_code nano::vote_processor::vote_blocking (nano::transaction const & transaction_a, std::shared_ptr<nano::vote> vote_a, nano::endpoint endpoint_a, bool validated)
{
	assert (endpoint_a.address ().is_v6 ());
	assert (!node.active.mutex.try_lock ());
	auto result (nano::vote_code::invalid);
	if (validated || !vote_a->validate ())
	{
		auto max_vote (node.store.vote_max (transaction_a, vote_a));
		result = nano::vote_code::replay;
		if (!node.active.vote (vote_a, true))
		{
			result = nano::vote_code::vote;
		}
		switch (result)
		{
			case nano::vote_code::vote:
				node.observers.vote.notify (transaction_a, vote_a, endpoint_a);
			case nano::vote_code::replay:
				// This tries to assist rep nodes that have lost track of their highest sequence number by replaying our highest known vote back to them
				// Only do this if the sequence number is significantly different to account for network reordering
				// Amplify attack considerations: We're sending out a confirm_ack in response to a confirm_ack for no net traffic increase
				if (max_vote->sequence > vote_a->sequence + 10000)
				{
					nano::confirm_ack confirm (max_vote);
					node.network.confirm_send (confirm, confirm.to_bytes (), endpoint_a);
				}
				break;
			case nano::vote_code::invalid:
				assert (false);
				break;
		}
	}
	std::string status;
	switch (result)
	{
		case nano::vote_code::invalid:
			status = "Invalid";
			node.stats.inc (nano::stat::type::vote, nano::stat::detail::vote_invalid);
			break;
		case nano::vote_code::replay:
			status = "Replay";
			node.stats.inc (nano::stat::type::vote, nano::stat::detail::vote_replay);
			break;
		case nano::vote_code::vote:
			status = "Vote";
			node.stats.inc (nano::stat::type::vote, nano::stat::detail::vote_valid);
			break;
	}
	if (node.config.logging.vote_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Vote from: %1% sequence: %2% block(s): %3%status: %4%") % vote_a->account.to_account () % std::to_string (vote_a->sequence) % vote_a->hashes_string () % status);
	}
	return result;
}

void nano::vote_processor::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::vote_processor::flush ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (active || !votes.empty ())
	{
		condition.wait (lock);
	}
}

void nano::vote_processor::calculate_weights ()
{
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		representatives_1.clear ();
		representatives_2.clear ();
		representatives_3.clear ();
		auto supply (node.online_reps.online_stake ());
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.representation_begin (transaction)), n (node.store.representation_end ()); i != n; ++i)
		{
			nano::account representative (i->first);
			auto weight (node.ledger.weight (transaction, representative));
			if (weight > supply / 1000) // 0.1% or above (level 1)
			{
				representatives_1.insert (representative);
				if (weight > supply / 100) // 1% or above (level 2)
				{
					representatives_2.insert (representative);
					if (weight > supply / 20) // 5% or above (level 3)
					{
						representatives_3.insert (representative);
					}
				}
			}
		}
	}
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (node_observers & node_observers, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (collect_seq_con_info (node_observers.blocks, "blocks"));
	composite->add_component (collect_seq_con_info (node_observers.wallet, "wallet"));
	composite->add_component (collect_seq_con_info (node_observers.vote, "vote"));
	composite->add_component (collect_seq_con_info (node_observers.account_balance, "account_balance"));
	composite->add_component (collect_seq_con_info (node_observers.endpoint, "endpoint"));
	composite->add_component (collect_seq_con_info (node_observers.disconnect, "disconnect"));
	return composite;
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name)
{
	size_t votes_count = 0;
	size_t representatives_1_count = 0;
	size_t representatives_2_count = 0;
	size_t representatives_3_count = 0;

	{
		std::lock_guard<std::mutex> (vote_processor.mutex);
		votes_count = vote_processor.votes.size ();
		representatives_1_count = vote_processor.representatives_1.size ();
		representatives_2_count = vote_processor.representatives_2.size ();
		representatives_3_count = vote_processor.representatives_3.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "votes", votes_count, sizeof (decltype (vote_processor.votes)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "representatives_1", representatives_1_count, sizeof (decltype (vote_processor.representatives_1)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "representatives_2", representatives_2_count, sizeof (decltype (vote_processor.representatives_2)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "representatives_3", representatives_3_count, sizeof (decltype (vote_processor.representatives_3)::value_type) }));
	return composite;
}
}

void nano::rep_crawler::add (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	active.insert (hash_a);
}

void nano::rep_crawler::remove (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	active.erase (hash_a);
}

bool nano::rep_crawler::exists (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return active.count (hash_a) != 0;
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_crawler & rep_crawler, const std::string & name)
{
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (rep_crawler.mutex);
		count = rep_crawler.active.size ();
	}

	auto sizeof_element = sizeof (decltype (rep_crawler.active)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "active", count, sizeof_element }));
	return composite;
}
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name)
{
	size_t state_blocks_count = 0;
	size_t blocks_count = 0;
	size_t blocks_hashes_count = 0;
	size_t forced_count = 0;
	size_t rolled_back_count = 0;

	{
		std::lock_guard<std::mutex> guard (block_processor.mutex);
		state_blocks_count = block_processor.state_blocks.size ();
		blocks_count = block_processor.blocks.size ();
		blocks_hashes_count = block_processor.blocks_hashes.size ();
		forced_count = block_processor.forced.size ();
		rolled_back_count = block_processor.rolled_back.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "state_blocks", state_blocks_count, sizeof (decltype (block_processor.state_blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", blocks_count, sizeof (decltype (block_processor.blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks_hashes", blocks_hashes_count, sizeof (decltype (block_processor.blocks_hashes)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "forced", forced_count, sizeof (decltype (block_processor.forced)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "rolled_back", rolled_back_count, sizeof (decltype (block_processor.rolled_back)::value_type) }));
	composite->add_component (collect_seq_con_info (block_processor.generator, "generator"));
	return composite;
}
}

nano::node::node (nano::node_init & init_a, boost::asio::io_context & io_ctx_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, nano::alarm & alarm_a, nano::logging const & logging_a, nano::work_pool & work_a) :
node (init_a, io_ctx_a, application_path_a, alarm_a, nano::node_config (peering_port_a, logging_a), work_a)
{
}

nano::node::node (nano::node_init & init_a, boost::asio::io_context & io_ctx_a, boost::filesystem::path const & application_path_a, nano::alarm & alarm_a, nano::node_config const & config_a, nano::work_pool & work_a, nano::node_flags flags_a) :
io_ctx (io_ctx_a),
config (config_a),
flags (flags_a),
alarm (alarm_a),
work (work_a),
store_impl (std::make_unique<nano::mdb_store> (init_a.block_store_init, config.logging, application_path_a / "data.ldb", config_a.lmdb_max_dbs, !flags.disable_unchecked_drop, flags.sideband_batch_size)),
store (*store_impl),
wallets_store_impl (std::make_unique<nano::mdb_wallets_store> (init_a.wallets_store_init, application_path_a / "wallets.ldb", config_a.lmdb_max_dbs)),
wallets_store (*wallets_store_impl),
gap_cache (*this),
ledger (store, stats, config.epoch_block_link, config.epoch_block_signer),
active (*this),
network (*this, config.peering_port),
bootstrap_initiator (*this),
bootstrap (io_ctx_a, config.peering_port, *this),
peers (network.endpoint ()),
application_path (application_path_a),
wallets (init_a.wallet_init, *this),
port_mapping (*this),
checker (config.signature_checker_threads),
vote_processor (*this),
warmed_up (0),
block_processor (*this),
block_processor_thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::block_processing);
	this->block_processor.process_blocks ();
}),
online_reps (ledger, config.online_weight_minimum.number ()),
stats (config.stat_config),
vote_uniquer (block_uniquer),
startup_time (std::chrono::steady_clock::now ())
{
	wallets.observer = [this](bool active) {
		observers.wallet.notify (active);
	};
	peers.peer_observer = [this](nano::endpoint const & endpoint_a) {
		observers.endpoint.notify (endpoint_a);
	};
	peers.disconnect_observer = [this]() {
		observers.disconnect.notify ();
	};
	if (!config.callback_address.empty ())
	{
		observers.blocks.add ([this](std::shared_ptr<nano::block> block_a, nano::account const & account_a, nano::amount const & amount_a, bool is_state_send_a) {
			if (this->block_arrival.recent (block_a->hash ()))
			{
				auto node_l (shared_from_this ());
				background ([node_l, block_a, account_a, amount_a, is_state_send_a]() {
					boost::property_tree::ptree event;
					event.add ("account", account_a.to_account ());
					event.add ("hash", block_a->hash ().to_string ());
					std::string block_text;
					block_a->serialize_json (block_text);
					event.add ("block", block_text);
					event.add ("amount", amount_a.to_string_dec ());
					if (is_state_send_a)
					{
						event.add ("is_send", is_state_send_a);
					}
					std::stringstream ostream;
					boost::property_tree::write_json (ostream, event);
					ostream.flush ();
					auto body (std::make_shared<std::string> (ostream.str ()));
					auto address (node_l->config.callback_address);
					auto port (node_l->config.callback_port);
					auto target (std::make_shared<std::string> (node_l->config.callback_target));
					auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (node_l->io_ctx));
					resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [node_l, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
						if (!ec)
						{
							node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
						}
						else
						{
							if (node_l->config.logging.callback_logging ())
							{
								BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ());
							}
							node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
						}
					});
				});
			}
		});
	}
	observers.endpoint.add ([this](nano::endpoint const & endpoint_a) {
		this->network.send_keepalive (endpoint_a);
		rep_query (*this, endpoint_a);
	});
	observers.vote.add ([this](nano::transaction const & transaction, std::shared_ptr<nano::vote> vote_a, nano::endpoint const & endpoint_a) {
		assert (endpoint_a.address ().is_v6 ());
		this->gap_cache.vote (vote_a);
		this->online_reps.observe (vote_a->account);
		nano::uint128_t rep_weight;
		nano::uint128_t min_rep_weight;
		{
			rep_weight = ledger.weight (transaction, vote_a->account);
			min_rep_weight = online_reps.online_stake () / 1000;
		}
		if (rep_weight > min_rep_weight)
		{
			bool rep_crawler_exists (false);
			for (auto hash : *vote_a)
			{
				if (this->rep_crawler.exists (hash))
				{
					rep_crawler_exists = true;
					break;
				}
			}
			if (rep_crawler_exists)
			{
				// We see a valid non-replay vote for a block we requested, this node is probably a representative
				if (this->peers.rep_response (endpoint_a, vote_a->account, rep_weight))
				{
					BOOST_LOG (log) << boost::str (boost::format ("Found a representative at %1%") % endpoint_a);
					// Rebroadcasting all active votes to new representative
					auto blocks (this->active.list_blocks (true));
					for (auto i (blocks.begin ()), n (blocks.end ()); i != n; ++i)
					{
						if (*i != nullptr)
						{
							this->network.send_confirm_req (endpoint_a, *i);
						}
					}
				}
			}
		}
	});
	if (NANO_VERSION_PATCH == 0)
	{
		BOOST_LOG (log) << "Node starting, version: " << NANO_MAJOR_MINOR_VERSION;
	}
	else
	{
		BOOST_LOG (log) << "Node starting, version: " << NANO_MAJOR_MINOR_RC_VERSION;
	}

	BOOST_LOG (log) << boost::str (boost::format ("Work pool running %1% threads") % work.threads.size ());
	if (!init_a.error ())
	{
		if (config.logging.node_lifetime_tracing ())
		{
			BOOST_LOG (log) << "Constructing node";
		}
		nano::genesis genesis;
		auto transaction (store.tx_begin_write ());
		if (store.latest_begin (transaction) == store.latest_end ())
		{
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, genesis);
		}
		if (!store.block_exists (transaction, genesis.hash ()))
		{
			BOOST_LOG (log) << "Genesis block not found. Make sure the node network ID is correct.";
			std::exit (1);
		}

		node_id = nano::keypair (store.get_node_id (transaction));
		BOOST_LOG (log) << "Node ID: " << node_id.pub.to_account ();
	}
	peers.online_weight_minimum = config.online_weight_minimum.number ();
	if (nano::is_live_network || nano::is_beta_network)
	{
		nano::bufferstream weight_stream ((const uint8_t *)nano_bootstrap_weights, nano_bootstrap_weights_size);
		nano::uint128_union block_height;
		if (!nano::try_read (weight_stream, block_height))
		{
			auto max_blocks = (uint64_t)block_height.number ();
			auto transaction (store.tx_begin_read ());
			if (ledger.store.block_count (transaction).sum () < max_blocks)
			{
				ledger.bootstrap_weight_max_blocks = max_blocks;
				while (true)
				{
					nano::account account;
					if (nano::try_read (weight_stream, account.bytes))
					{
						break;
					}
					nano::amount weight;
					if (nano::try_read (weight_stream, weight.bytes))
					{
						break;
					}
					BOOST_LOG (log) << "Using bootstrap rep weight: " << account.to_account () << " -> " << weight.format_balance (Mxrb_ratio, 0, true) << " XRB";
					ledger.bootstrap_weights[account] = weight.number ();
				}
			}
		}
	}
}

nano::node::~node ()
{
	if (config.logging.node_lifetime_tracing ())
	{
		BOOST_LOG (log) << "Destructing node";
	}
	stop ();
}

void nano::node::do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const & address, uint16_t port, std::shared_ptr<std::string> target, std::shared_ptr<std::string> body, std::shared_ptr<boost::asio::ip::tcp::resolver> resolver)
{
	if (i_a != boost::asio::ip::tcp::resolver::iterator{})
	{
		auto node_l (shared_from_this ());
		auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->io_ctx));
		sock->async_connect (i_a->endpoint (), [node_l, target, body, sock, address, port, i_a, resolver](boost::system::error_code const & ec) mutable {
			if (!ec)
			{
				auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
				req->method (boost::beast::http::verb::post);
				req->target (*target);
				req->version (11);
				req->insert (boost::beast::http::field::host, address);
				req->insert (boost::beast::http::field::content_type, "application/json");
				req->body () = *body;
				req->prepare_payload ();
				boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
					if (!ec)
					{
						auto sb (std::make_shared<boost::beast::flat_buffer> ());
						auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
						boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
							if (!ec)
							{
								if (resp->result () == boost::beast::http::status::ok)
								{
									node_l->stats.inc (nano::stat::type::http_callback, nano::stat::detail::initiate, nano::stat::dir::out);
								}
								else
								{
									if (node_l->config.logging.callback_logging ())
									{
										BOOST_LOG (node_l->log) << boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ());
									}
									node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
								}
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ());
								}
								node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
							};
						});
					}
					else
					{
						if (node_l->config.logging.callback_logging ())
						{
							BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ());
						}
						node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
					}
				});
			}
			else
			{
				if (node_l->config.logging.callback_logging ())
				{
					BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ());
				}
				node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
				++i_a;
				node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
			}
		});
	}
}

bool nano::node::copy_with_compaction (boost::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (boost::polymorphic_downcast<nano::mdb_store *> (store_impl.get ())->env.environment, destination_file.string ().c_str (), MDB_CP_COMPACT);
}

void nano::node::send_keepalive (nano::endpoint const & endpoint_a)
{
	network.send_keepalive (nano::map_endpoint_to_v6 (endpoint_a));
}

void nano::node::process_fork (nano::transaction const & transaction_a, std::shared_ptr<nano::block> block_a)
{
	auto root (block_a->root ());
	if (!store.block_exists (transaction_a, block_a->type (), block_a->hash ()) && store.root_exists (transaction_a, block_a->root ()))
	{
		std::shared_ptr<nano::block> ledger_block (ledger.forked_block (transaction_a, *block_a));
		if (ledger_block)
		{
			std::weak_ptr<nano::node> this_w (shared_from_this ());
			if (!active.start (ledger_block, [this_w, root](std::shared_ptr<nano::block>) {
				    if (auto this_l = this_w.lock ())
				    {
					    auto attempt (this_l->bootstrap_initiator.current_attempt ());
					    if (attempt && attempt->mode == nano::bootstrap_mode::legacy)
					    {
						    auto transaction (this_l->store.tx_begin_read ());
						    auto account (this_l->ledger.store.frontier_get (transaction, root));
						    if (!account.is_zero ())
						    {
							    attempt->requeue_pull (nano::pull_info (account, root, root));
						    }
						    else if (this_l->ledger.store.account_exists (transaction, root))
						    {
							    attempt->requeue_pull (nano::pull_info (root, nano::block_hash (0), nano::block_hash (0)));
						    }
					    }
				    }
			    }))
			{
				BOOST_LOG (log) << boost::str (boost::format ("Resolving fork between our block: %1% and block %2% both with root %3%") % ledger_block->hash ().to_string () % block_a->hash ().to_string () % block_a->root ().to_string ());
				network.broadcast_confirm_req (ledger_block);
			}
		}
	}
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (node & node, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (collect_seq_con_info (node.alarm, "alarm"));
	composite->add_component (collect_seq_con_info (node.work, "work"));
	composite->add_component (collect_seq_con_info (node.gap_cache, "gap_cache"));
	composite->add_component (collect_seq_con_info (node.ledger, "ledger"));
	composite->add_component (collect_seq_con_info (node.active, "active"));
	composite->add_component (collect_seq_con_info (node.bootstrap_initiator, "bootstrap_initiator"));
	composite->add_component (collect_seq_con_info (node.bootstrap, "bootstrap"));
	composite->add_component (collect_seq_con_info (node.peers, "peers"));
	composite->add_component (collect_seq_con_info (node.observers, "observers"));
	composite->add_component (collect_seq_con_info (node.wallets, "wallets"));
	composite->add_component (collect_seq_con_info (node.vote_processor, "vote_processor"));
	composite->add_component (collect_seq_con_info (node.rep_crawler, "rep_crawler"));
	composite->add_component (collect_seq_con_info (node.block_processor, "block_processor"));
	composite->add_component (collect_seq_con_info (node.block_arrival, "block_arrival"));
	composite->add_component (collect_seq_con_info (node.online_reps, "online_reps"));
	composite->add_component (collect_seq_con_info (node.votes_cache, "votes_cache"));
	composite->add_component (collect_seq_con_info (node.block_uniquer, "block_uniquer"));
	composite->add_component (collect_seq_con_info (node.vote_uniquer, "vote_uniquer"));
	return composite;
}
}

nano::gap_cache::gap_cache (nano::node & node_a) :
node (node_a)
{
}

void nano::gap_cache::add (nano::transaction const & transaction_a, nano::block_hash const & hash_a, std::chrono::steady_clock::time_point time_point_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (blocks.get<1> ().find (hash_a));
	if (existing != blocks.get<1> ().end ())
	{
		blocks.get<1> ().modify (existing, [time_point_a](nano::gap_information & info) {
			info.arrival = time_point_a;
		});
	}
	else
	{
		blocks.insert ({ time_point_a, hash_a, std::unordered_set<nano::account> () });
		if (blocks.size () > max)
		{
			blocks.get<0> ().erase (blocks.get<0> ().begin ());
		}
	}
}

void nano::gap_cache::vote (std::shared_ptr<nano::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto transaction (node.store.tx_begin_read ());
	for (auto hash : *vote_a)
	{
		auto existing (blocks.get<1> ().find (hash));
		if (existing != blocks.get<1> ().end ())
		{
			auto is_new (false);
			blocks.get<1> ().modify (existing, [&](nano::gap_information & info) { is_new = info.voters.insert (vote_a->account).second; });
			if (is_new)
			{
				uint128_t tally;
				for (auto & voter : existing->voters)
				{
					tally += node.ledger.weight (transaction, voter);
				}
				bool start_bootstrap (false);
				if (!node.flags.disable_lazy_bootstrap)
				{
					if (tally >= node.config.online_weight_minimum.number ())
					{
						start_bootstrap = true;
					}
				}
				else if (!node.flags.disable_legacy_bootstrap && tally > bootstrap_threshold (transaction))
				{
					start_bootstrap = true;
				}
				if (start_bootstrap)
				{
					auto node_l (node.shared ());
					auto now (std::chrono::steady_clock::now ());
					node.alarm.add (nano::is_test_network ? now + std::chrono::milliseconds (5) : now + std::chrono::seconds (5), [node_l, hash]() {
						auto transaction (node_l->store.tx_begin_read ());
						if (!node_l->store.block_exists (transaction, hash))
						{
							if (!node_l->bootstrap_initiator.in_progress ())
							{
								BOOST_LOG (node_l->log) << boost::str (boost::format ("Missing block %1% which has enough votes to warrant lazy bootstrapping it") % hash.to_string ());
							}
							if (!node_l->flags.disable_lazy_bootstrap)
							{
								node_l->bootstrap_initiator.bootstrap_lazy (hash);
							}
							else if (!node_l->flags.disable_legacy_bootstrap)
							{
								node_l->bootstrap_initiator.bootstrap ();
							}
						}
					});
				}
			}
		}
	}
}

nano::uint128_t nano::gap_cache::bootstrap_threshold (nano::transaction const & transaction_a)
{
	auto result ((node.online_reps.online_stake () / 256) * node.config.bootstrap_fraction_numerator);
	return result;
}

size_t nano::gap_cache::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return blocks.size ();
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (gap_cache & gap_cache, const std::string & name)
{
	auto count = gap_cache.size ();
	auto sizeof_element = sizeof (decltype (gap_cache.blocks)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", count, sizeof_element }));
	return composite;
}
}

void nano::network::confirm_send (nano::confirm_ack const & confirm_a, std::shared_ptr<std::vector<uint8_t>> bytes_a, nano::endpoint const & endpoint_a)
{
	if (node.config.logging.network_publish_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm_ack for block(s) %1%to %2% sequence %3%") % confirm_a.vote->hashes_string () % endpoint_a % std::to_string (confirm_a.vote->sequence));
	}
	std::weak_ptr<nano::node> node_w (node.shared ());
	node.network.send_buffer (bytes_a->data (), bytes_a->size (), endpoint_a, [bytes_a, node_w, endpoint_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error broadcasting confirm_ack to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out);
			}
		}
	});
}

void nano::node::process_active (std::shared_ptr<nano::block> incoming)
{
	block_arrival.add (incoming->hash ());
	block_processor.add (incoming, nano::seconds_since_epoch ());
}

nano::process_return nano::node::process (nano::block const & block_a)
{
	auto transaction (store.tx_begin_write ());
	auto result (ledger.process (transaction, block_a));
	return result;
}

void nano::node::start ()
{
	network.start ();
	add_initial_peers ();
	ongoing_keepalive ();
	ongoing_syn_cookie_cleanup ();
	if (!flags.disable_legacy_bootstrap)
	{
		ongoing_bootstrap ();
	}
	else if (!flags.disable_unchecked_cleanup)
	{
		ongoing_unchecked_cleanup ();
	}
	ongoing_store_flush ();
	ongoing_rep_crawl ();
	ongoing_rep_calculation ();
	ongoing_peer_store ();
	ongoing_online_weight_calculation_queue ();
	if (!flags.disable_bootstrap_listener)
	{
		bootstrap.start ();
	}
	if (!flags.disable_backup)
	{
		backup_wallet ();
	}
	search_pending ();
	if (!flags.disable_wallet_bootstrap)
	{
		// Delay to start wallet lazy bootstrap
		auto this_l (shared ());
		alarm.add (std::chrono::steady_clock::now () + std::chrono::minutes (1), [this_l]() {
			this_l->bootstrap_wallet ();
		});
	}
	port_mapping.start ();
}

void nano::node::stop ()
{
	BOOST_LOG (log) << "Node stopping";
	block_processor.stop ();
	if (block_processor_thread.joinable ())
	{
		block_processor_thread.join ();
	}
	vote_processor.stop ();
	active.stop ();
	network.stop ();
	bootstrap_initiator.stop ();
	bootstrap.stop ();
	port_mapping.stop ();
	checker.stop ();
	wallets.stop ();
}

void nano::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, nano::network::node_port, true);
	}
}

nano::block_hash nano::node::latest (nano::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

nano::uint128_t nano::node::balance (nano::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<nano::block> nano::node::block (nano::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_get (transaction, hash_a);
}

std::pair<nano::uint128_t, nano::uint128_t> nano::node::balance_pending (nano::account const & account_a)
{
	std::pair<nano::uint128_t, nano::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a);
	result.second = ledger.account_pending (transaction, account_a);
	return result;
}

nano::uint128_t nano::node::weight (nano::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.weight (transaction, account_a);
}

nano::account nano::node::representative (nano::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	nano::account_info info;
	nano::account result (0);
	if (!store.account_get (transaction, account_a, info))
	{
		result = info.rep_block;
	}
	return result;
}

void nano::node::ongoing_keepalive ()
{
	keepalive_preconfigured (config.preconfigured_peers);
	auto peers_l (peers.purge_list (std::chrono::steady_clock::now () - cutoff));
	for (auto i (peers_l.begin ()), j (peers_l.end ()); i != j && std::chrono::steady_clock::now () - i->last_attempt > period; ++i)
	{
		network.send_keepalive (i->endpoint);
	}
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_keepalive ();
		}
	});
}

void nano::node::ongoing_syn_cookie_cleanup ()
{
	peers.purge_syn_cookies (std::chrono::steady_clock::now () - syn_cookie_cutoff);
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + (syn_cookie_cutoff * 2), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_syn_cookie_cleanup ();
		}
	});
}

void nano::node::ongoing_rep_crawl ()
{
	auto now (std::chrono::steady_clock::now ());
	auto peers_l (peers.rep_crawl ());
	rep_query (*this, peers_l);
	if (network.on)
	{
		std::weak_ptr<nano::node> node_w (shared_from_this ());
		alarm.add (now + std::chrono::seconds (4), [node_w]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->ongoing_rep_crawl ();
			}
		});
	}
}

void nano::node::ongoing_rep_calculation ()
{
	auto now (std::chrono::steady_clock::now ());
	vote_processor.calculate_weights ();
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	alarm.add (now + std::chrono::minutes (10), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_rep_calculation ();
		}
	});
}

void nano::node::ongoing_bootstrap ()
{
	auto next_wakeup (300);
	if (warmed_up < 3)
	{
		// Re-attempt bootstrapping more aggressively on startup
		next_wakeup = 5;
		if (!bootstrap_initiator.in_progress () && !peers.empty ())
		{
			++warmed_up;
		}
	}
	bootstrap_initiator.bootstrap ();
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_bootstrap ();
		}
	});
}

void nano::node::ongoing_store_flush ()
{
	{
		auto transaction (store.tx_begin_write ());
		store.flush (transaction);
	}
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_store_flush ();
		}
	});
}

void nano::node::ongoing_peer_store ()
{
	auto endpoint_peers = peers.list ();
	if (!endpoint_peers.empty ())
	{
		// Clear all peers then refresh with the current list of peers
		auto transaction (store.tx_begin_write ());
		store.peer_clear (transaction);
		for (const auto & endpoint : endpoint_peers)
		{
			nano::endpoint_key endpoint_key (endpoint.address ().to_v6 ().to_bytes (), endpoint.port ());
			store.peer_put (transaction, std::move (endpoint_key));
		}
	}

	std::weak_ptr<nano::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + peer_interval, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_peer_store ();
		}
	});
}

void nano::node::backup_wallet ()
{
	auto transaction (wallets.tx_begin_read ());
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		boost::filesystem::create_directories (backup_path);
		nano::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + backup_interval, [this_l]() {
		this_l->backup_wallet ();
	});
}

void nano::node::search_pending ()
{
	// Reload wallets from disk
	wallets.reload ();
	// Search pending
	wallets.search_pending_all ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + search_pending_interval, [this_l]() {
		this_l->search_pending ();
	});
}

void nano::node::bootstrap_wallet ()
{
	std::deque<nano::account> accounts;
	{
		std::lock_guard<std::mutex> lock (wallets.mutex);
		auto transaction (wallets.tx_begin_read ());
		for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n && accounts.size () < 128; ++i)
		{
			auto & wallet (*i->second);
			std::lock_guard<std::recursive_mutex> wallet_lock (wallet.store.mutex);
			for (auto j (wallet.store.begin (transaction)), m (wallet.store.end ()); j != m && accounts.size () < 128; ++j)
			{
				nano::account account (j->first);
				accounts.push_back (account);
			}
		}
	}
	bootstrap_initiator.bootstrap_wallet (accounts);
}

void nano::node::unchecked_cleanup ()
{
	std::deque<nano::unchecked_key> cleaning_list;
	// Collect old unchecked keys
	{
		auto now (nano::seconds_since_epoch ());
		auto transaction (store.tx_begin_read ());
		// Max 128k records to clean, max 2 minutes reading to prevent slow i/o systems start issues
		for (auto i (store.unchecked_begin (transaction)), n (store.unchecked_end ()); i != n && cleaning_list.size () < 128 * 1024 && nano::seconds_since_epoch () - now < 120; ++i)
		{
			nano::unchecked_key key (i->first);
			nano::unchecked_info info (i->second);
			if ((now - info.modified) > config.unchecked_cutoff_time.count ())
			{
				cleaning_list.push_back (key);
			}
		}
	}
	// Delete old unchecked keys in batches
	while (!cleaning_list.empty ())
	{
		size_t deleted_count (0);
		auto transaction (store.tx_begin_write ());
		while (deleted_count++ < 2 * 1024 && !cleaning_list.empty ())
		{
			auto key (cleaning_list.front ());
			cleaning_list.pop_front ();
			store.unchecked_del (transaction, key);
		}
	}
}

void nano::node::ongoing_unchecked_cleanup ()
{
	if (!bootstrap_initiator.in_progress ())
	{
		unchecked_cleanup ();
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + unchecked_cleanup_interval, [this_l]() {
		this_l->ongoing_unchecked_cleanup ();
	});
}

int nano::node::price (nano::uint128_t const & balance_a, int amount_a)
{
	assert (balance_a >= amount_a * nano::Gxrb_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= nano::Gxrb_ratio;
		auto balance_scaled ((balance_l / nano::Mxrb_ratio).convert_to<double> ());
		auto units (balance_scaled / 1000.0);
		auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
		result += std::min (std::max (0.0, unit_price), price_max);
	}
	return static_cast<int> (result * 100.0);
}

namespace
{
class work_request
{
public:
	work_request (boost::asio::io_context & io_ctx_a, boost::asio::ip::address address_a, uint16_t port_a) :
	address (address_a),
	port (port_a),
	socket (io_ctx_a)
	{
	}
	boost::asio::ip::address address;
	uint16_t port;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> response;
	boost::asio::ip::tcp::socket socket;
};
class distributed_work : public std::enable_shared_from_this<distributed_work>
{
public:
	distributed_work (std::shared_ptr<nano::node> const & node_a, nano::block_hash const & root_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a) :
	distributed_work (1, node_a, root_a, callback_a, difficulty_a)
	{
		assert (node_a != nullptr);
	}
	distributed_work (unsigned int backoff_a, std::shared_ptr<nano::node> const & node_a, nano::block_hash const & root_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a) :
	callback (callback_a),
	backoff (backoff_a),
	node (node_a),
	root (root_a),
	need_resolve (node_a->config.work_peers),
	difficulty (difficulty_a)
	{
		assert (node_a != nullptr);
		completed.clear ();
	}
	void start ()
	{
		if (need_resolve.empty ())
		{
			start_work ();
		}
		else
		{
			auto current (need_resolve.back ());
			need_resolve.pop_back ();
			auto this_l (shared_from_this ());
			boost::system::error_code ec;
			auto parsed_address (boost::asio::ip::address_v6::from_string (current.first, ec));
			if (!ec)
			{
				outstanding[parsed_address] = current.second;
				start ();
			}
			else
			{
				node->network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (current.first, std::to_string (current.second)), [current, this_l](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
					if (!ec)
					{
						for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
						{
							auto endpoint (i->endpoint ());
							this_l->outstanding[endpoint.address ()] = endpoint.port ();
						}
					}
					else
					{
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error resolving work peer: %1%:%2%: %3%") % current.first % current.second % ec.message ());
					}
					this_l->start ();
				});
			}
		}
	}
	void start_work ()
	{
		if (!outstanding.empty ())
		{
			auto this_l (shared_from_this ());
			std::lock_guard<std::mutex> lock (mutex);
			for (auto const & i : outstanding)
			{
				auto host (i.first);
				auto service (i.second);
				node->background ([this_l, host, service]() {
					auto connection (std::make_shared<work_request> (this_l->node->io_ctx, host, service));
					connection->socket.async_connect (nano::tcp_endpoint (host, service), [this_l, connection](boost::system::error_code const & ec) {
						if (!ec)
						{
							std::string request_string;
							{
								boost::property_tree::ptree request;
								request.put ("action", "work_generate");
								request.put ("hash", this_l->root.to_string ());
								std::stringstream ostream;
								boost::property_tree::write_json (ostream, request);
								request_string = ostream.str ();
							}
							auto request (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
							request->method (boost::beast::http::verb::post);
							request->target ("/");
							request->version (11);
							request->body () = request_string;
							request->prepare_payload ();
							boost::beast::http::async_write (connection->socket, *request, [this_l, connection, request](boost::system::error_code const & ec, size_t bytes_transferred) {
								if (!ec)
								{
									boost::beast::http::async_read (connection->socket, connection->buffer, connection->response, [this_l, connection](boost::system::error_code const & ec, size_t bytes_transferred) {
										if (!ec)
										{
											if (connection->response.result () == boost::beast::http::status::ok)
											{
												this_l->success (connection->response.body (), connection->address);
											}
											else
											{
												BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Work peer responded with an error %1% %2%: %3%") % connection->address % connection->port % connection->response.result ());
												this_l->failure (connection->address);
											}
										}
										else
										{
											BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to read from work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
											this_l->failure (connection->address);
										}
									});
								}
								else
								{
									BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to write to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
									this_l->failure (connection->address);
								}
							});
						}
						else
						{
							BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to connect to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
							this_l->failure (connection->address);
						}
					});
				});
			}
		}
		else
		{
			handle_failure (true);
		}
	}
	void stop ()
	{
		auto this_l (shared_from_this ());
		std::lock_guard<std::mutex> lock (mutex);
		for (auto const & i : outstanding)
		{
			auto host (i.first);
			node->background ([this_l, host]() {
				std::string request_string;
				{
					boost::property_tree::ptree request;
					request.put ("action", "work_cancel");
					request.put ("hash", this_l->root.to_string ());
					std::stringstream ostream;
					boost::property_tree::write_json (ostream, request);
					request_string = ostream.str ();
				}
				boost::beast::http::request<boost::beast::http::string_body> request;
				request.method (boost::beast::http::verb::post);
				request.target ("/");
				request.version (11);
				request.body () = request_string;
				request.prepare_payload ();
				auto socket (std::make_shared<boost::asio::ip::tcp::socket> (this_l->node->io_ctx));
				boost::beast::http::async_write (*socket, request, [socket](boost::system::error_code const & ec, size_t bytes_transferred) {
				});
			});
		}
		outstanding.clear ();
	}
	void success (std::string const & body_a, boost::asio::ip::address const & address)
	{
		auto last (remove (address));
		std::stringstream istream (body_a);
		try
		{
			boost::property_tree::ptree result;
			boost::property_tree::read_json (istream, result);
			auto work_text (result.get<std::string> ("work"));
			uint64_t work;
			if (!nano::from_string_hex (work_text, work))
			{
				if (!nano::work_validate (root, work))
				{
					set_once (work);
					stop ();
				}
				else
				{
					BOOST_LOG (node->log) << boost::str (boost::format ("Incorrect work response from %1% for root %2%: %3%") % address % root.to_string () % work_text);
					handle_failure (last);
				}
			}
			else
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Work response from %1% wasn't a number: %2%") % address % work_text);
				handle_failure (last);
			}
		}
		catch (...)
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Work response from %1% wasn't parsable: %2%") % address % body_a);
			handle_failure (last);
		}
	}
	void set_once (uint64_t work_a)
	{
		if (!completed.test_and_set ())
		{
			callback (work_a);
		}
	}
	void failure (boost::asio::ip::address const & address)
	{
		auto last (remove (address));
		handle_failure (last);
	}
	void handle_failure (bool last)
	{
		if (last)
		{
			if (!completed.test_and_set ())
			{
				if (node->config.work_threads != 0 || node->work.opencl)
				{
					auto callback_l (callback);
					// clang-format off
					node->work.generate (root, [callback_l](boost::optional<uint64_t> const & work_a) {
						callback_l (work_a.value ());
					},
					difficulty);
					// clang-format on
				}
				else
				{
					if (backoff == 1 && node->config.logging.work_generation_time ())
					{
						BOOST_LOG (node->log) << "Work peer(s) failed to generate work for root " << root.to_string () << ", retrying...";
					}
					auto now (std::chrono::steady_clock::now ());
					auto root_l (root);
					auto callback_l (callback);
					std::weak_ptr<nano::node> node_w (node);
					auto next_backoff (std::min (backoff * 2, (unsigned int)60 * 5));
					// clang-format off
					node->alarm.add (now + std::chrono::seconds (backoff), [ node_w, root_l, callback_l, next_backoff, difficulty = difficulty ] {
						if (auto node_l = node_w.lock ())
						{
							auto work_generation (std::make_shared<distributed_work> (next_backoff, node_l, root_l, callback_l, difficulty));
							work_generation->start ();
						}
					});
					// clang-format on
				}
			}
		}
	}
	bool remove (boost::asio::ip::address const & address)
	{
		std::lock_guard<std::mutex> lock (mutex);
		outstanding.erase (address);
		return outstanding.empty ();
	}
	std::function<void(uint64_t)> callback;
	unsigned int backoff; // in seconds
	std::shared_ptr<nano::node> node;
	nano::block_hash root;
	std::mutex mutex;
	std::map<boost::asio::ip::address, uint16_t> outstanding;
	std::vector<std::pair<std::string, uint16_t>> need_resolve;
	std::atomic_flag completed;
	uint64_t difficulty;
};
}

void nano::node::work_generate_blocking (nano::block & block_a, uint64_t difficulty_a)
{
	block_a.block_work_set (work_generate_blocking (block_a.root (), difficulty_a));
}

void nano::node::work_generate (nano::uint256_union const & hash_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a)
{
	auto work_generation (std::make_shared<distributed_work> (shared (), hash_a, callback_a, difficulty_a));
	work_generation->start ();
}

uint64_t nano::node::work_generate_blocking (nano::uint256_union const & hash_a, uint64_t difficulty_a)
{
	std::promise<uint64_t> promise;
	std::future<uint64_t> future = promise.get_future ();
	// clang-format off
	work_generate (hash_a, [&promise](uint64_t work_a) {
		promise.set_value (work_a);
	},
	difficulty_a);
	// clang-format on
	return future.get ();
}

void nano::node::add_initial_peers ()
{
	auto transaction (store.tx_begin_read ());
	for (auto i (store.peers_begin (transaction)), n (store.peers_end ()); i != n; ++i)
	{
		nano::endpoint endpoint (boost::asio::ip::address_v6 (i->first.address_bytes ()), i->first.port ());
		if (!peers.reachout (endpoint))
		{
			send_keepalive (endpoint);
		}
	}
}

void nano::node::block_confirm (std::shared_ptr<nano::block> block_a)
{
	active.start (block_a);
	network.broadcast_confirm_req (block_a);
	// Calculate votes for local representatives
	if (config.enable_voting && active.active (*block_a))
	{
		block_processor.generator.add (block_a->hash ());
	}
}

nano::uint128_t nano::node::delta ()
{
	auto result ((online_reps.online_stake () / 100) * config.online_weight_quorum);
	return result;
}

void nano::node::ongoing_online_weight_calculation_queue ()
{
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + (std::chrono::seconds (nano::online_reps::weight_period)), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_online_weight_calculation ();
		}
	});
}

void nano::node::ongoing_online_weight_calculation ()
{
	online_reps.sample ();
	ongoing_online_weight_calculation_queue ();
}

namespace
{
class confirmed_visitor : public nano::block_visitor
{
public:
	confirmed_visitor (nano::transaction const & transaction_a, nano::node & node_a, std::shared_ptr<nano::block> block_a, nano::block_hash const & hash_a) :
	transaction (transaction_a),
	node (node_a),
	block (block_a),
	hash (hash_a)
	{
	}
	virtual ~confirmed_visitor () = default;
	void scan_receivable (nano::account const & account_a)
	{
		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
		{
			auto wallet (i->second);
			auto transaction_l (node.wallets.tx_begin_read ());
			if (wallet->store.exists (transaction_l, account_a))
			{
				nano::account representative;
				nano::pending_info pending;
				representative = wallet->store.representative (transaction_l);
				auto error (node.store.pending_get (transaction, nano::pending_key (account_a, hash), pending));
				if (!error)
				{
					auto node_l (node.shared ());
					auto amount (pending.amount.number ());
					wallet->receive_async (block, representative, amount, [](std::shared_ptr<nano::block>) {});
				}
				else
				{
					if (!node.store.block_exists (transaction, hash))
					{
						BOOST_LOG (node.log) << boost::str (boost::format ("Confirmed block is missing:  %1%") % hash.to_string ());
						assert (false && "Confirmed block is missing");
					}
					else
					{
						BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% has already been received") % hash.to_string ());
					}
				}
			}
		}
	}
	void state_block (nano::state_block const & block_a) override
	{
		scan_receivable (block_a.hashables.link);
	}
	void send_block (nano::send_block const & block_a) override
	{
		scan_receivable (block_a.hashables.destination);
	}
	void receive_block (nano::receive_block const &) override
	{
	}
	void open_block (nano::open_block const &) override
	{
	}
	void change_block (nano::change_block const &) override
	{
	}
	nano::transaction const & transaction;
	nano::node & node;
	std::shared_ptr<nano::block> block;
	nano::block_hash const & hash;
};
}

void nano::node::process_confirmed (std::shared_ptr<nano::block> block_a, uint8_t iteration)
{
	auto hash (block_a->hash ());
	if (ledger.block_exists (block_a->type (), hash))
	{
		auto transaction (store.tx_begin_read ());
		confirmed_visitor visitor (transaction, *this, block_a, hash);
		block_a->visit (visitor);
		auto account (ledger.account (transaction, hash));
		auto amount (ledger.amount (transaction, hash));
		bool is_state_send (false);
		nano::account pending_account (0);
		if (auto state = dynamic_cast<nano::state_block *> (block_a.get ()))
		{
			is_state_send = ledger.is_send (transaction, *state);
			pending_account = state->hashables.link;
		}
		if (auto send = dynamic_cast<nano::send_block *> (block_a.get ()))
		{
			pending_account = send->hashables.destination;
		}
		observers.blocks.notify (block_a, account, amount, is_state_send);
		if (amount > 0)
		{
			observers.account_balance.notify (account, false);
			if (!pending_account.is_zero ())
			{
				observers.account_balance.notify (pending_account, true);
			}
		}
	}
	// Limit to 0.5 * 20 = 10 seconds (more than max block_processor::process_batch finish time)
	else if (iteration < 20)
	{
		iteration++;
		std::weak_ptr<nano::node> node_w (shared ());
		alarm.add (std::chrono::steady_clock::now () + process_confirmed_interval, [node_w, block_a, iteration]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->process_confirmed (block_a, iteration);
			}
		});
	}
}

void nano::node::process_message (nano::message & message_a, nano::endpoint const & sender_a)
{
	network_message_visitor visitor (*this, sender_a);
	message_a.visit (visitor);
}

nano::endpoint nano::network::endpoint ()
{
	boost::system::error_code ec;
	std::unique_lock<std::mutex> lock (socket_mutex);
	auto port (socket.local_endpoint (ec).port ());
	if (ec)
	{
		BOOST_LOG (node.log) << "Unable to retrieve port: " << ec.message ();
	}
	return nano::endpoint (boost::asio::ip::address_v6::loopback (), port);
}

bool nano::block_arrival::add (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.insert (nano::block_arrival_info{ now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool nano::block_arrival::recent (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.begin ()->arrival + arrival_time_min < now)
	{
		arrival.erase (arrival.begin ());
	}
	return arrival.get<1> ().find (hash_a) != arrival.get<1> ().end ();
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_arrival & block_arrival, const std::string & name)
{
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (block_arrival.mutex);
		count = block_arrival.arrival.size ();
	}

	auto sizeof_element = sizeof (decltype (block_arrival.arrival)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "arrival", count, sizeof_element }));
	return composite;
}
}

nano::online_reps::online_reps (nano::ledger & ledger_a, nano::uint128_t minimum_a) :
ledger (ledger_a),
minimum (minimum_a)
{
	auto transaction (ledger_a.store.tx_begin_read ());
	online = trend (transaction);
}

void nano::online_reps::observe (nano::account const & rep_a)
{
	auto transaction (ledger.store.tx_begin_read ());
	if (ledger.weight (transaction, rep_a) > nano::Gxrb_ratio)
	{
		std::lock_guard<std::mutex> lock (mutex);
		reps.insert (rep_a);
	}
}

void nano::online_reps::sample ()
{
	auto transaction (ledger.store.tx_begin_write ());
	// Discard oldest entries
	while (ledger.store.online_weight_count (transaction) >= weight_samples)
	{
		auto oldest (ledger.store.online_weight_begin (transaction));
		assert (oldest != ledger.store.online_weight_end ());
		ledger.store.online_weight_del (transaction, oldest->first);
	}
	// Calculate current active rep weight
	nano::uint128_t current;
	std::unordered_set<nano::account> reps_copy;
	{
		std::lock_guard<std::mutex> lock (mutex);
		reps_copy.swap (reps);
	}
	for (auto & i : reps_copy)
	{
		current += ledger.weight (transaction, i);
	}
	ledger.store.online_weight_put (transaction, std::chrono::system_clock::now ().time_since_epoch ().count (), current);
	auto trend_l (trend (transaction));
	std::lock_guard<std::mutex> lock (mutex);
	online = trend_l;
}

nano::uint128_t nano::online_reps::trend (nano::transaction & transaction_a)
{
	std::vector<nano::uint128_t> items;
	items.reserve (weight_samples + 1);
	items.push_back (minimum);
	for (auto i (ledger.store.online_weight_begin (transaction_a)), n (ledger.store.online_weight_end ()); i != n; ++i)
	{
		items.push_back (i->second.number ());
	}

	// Pick median value for our target vote weight
	auto median_idx = items.size () / 2;
	nth_element (items.begin (), items.begin () + median_idx, items.end ());
	return nano::uint128_t{ items[median_idx] };
}

nano::uint128_t nano::online_reps::online_stake ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return std::max (online, minimum);
}

std::vector<nano::account> nano::online_reps::list ()
{
	std::vector<nano::account> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto & i : reps)
	{
		result.push_back (i);
	}
	return result;
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name)
{
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (online_reps.mutex);
		count = online_reps.reps.size ();
	}

	auto sizeof_element = sizeof (decltype (online_reps.reps)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "arrival", count, sizeof_element }));
	return composite;
}
}

namespace
{
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long address_a)
{
	return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}
}

bool nano::reserved_address (nano::endpoint const & endpoint_a, bool blacklist_loopback)
{
	assert (endpoint_a.address ().is_v6 ());
	auto bytes (endpoint_a.address ().to_v6 ());
	auto result (false);
	static auto const rfc1700_min (mapped_from_v4_bytes (0x00000000ul));
	static auto const rfc1700_max (mapped_from_v4_bytes (0x00fffffful));
	static auto const ipv4_loopback_min (mapped_from_v4_bytes (0x7f000000ul));
	static auto const ipv4_loopback_max (mapped_from_v4_bytes (0x7ffffffful));
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
	else if (blacklist_loopback && bytes.is_loopback ())
	{
		result = true;
	}
	else if (blacklist_loopback && bytes >= ipv4_loopback_min && bytes <= ipv4_loopback_max)
	{
		result = true;
	}
	else if (nano::is_live_network)
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

void nano::network::send_buffer (uint8_t const * data_a, size_t size_a, nano::endpoint const & endpoint_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	std::unique_lock<std::mutex> lock (socket_mutex);
	if (node.config.logging.network_packet_logging ())
	{
		BOOST_LOG (node.log) << "Sending packet";
	}
	if (on.load ())
	{
		socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint_a, [this, callback_a](boost::system::error_code const & ec, size_t size_a) {
			callback_a (ec, size_a);
			this->node.stats.add (nano::stat::type::traffic, nano::stat::dir::out, size_a);
			if (ec == boost::system::errc::host_unreachable)
			{
				this->node.stats.inc (nano::stat::type::error, nano::stat::detail::unreachable_host, nano::stat::dir::out);
			}
			if (this->node.config.logging.network_packet_logging ())
			{
				BOOST_LOG (this->node.log) << "Packet send complete";
			}
		});
	}
}

std::shared_ptr<nano::node> nano::node::shared ()
{
	return shared_from_this ();
}

nano::election_vote_result::election_vote_result () :
replay (false),
processed (false)
{
}

nano::election_vote_result::election_vote_result (bool replay_a, bool processed_a)
{
	replay = replay_a;
	processed = processed_a;
}

nano::election::election (nano::node & node_a, std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a) :
confirmation_action (confirmation_action_a),
node (node_a),
election_start (std::chrono::steady_clock::now ()),
status ({ block_a, 0 }),
confirmed (false),
stopped (false),
announcements (0)
{
	last_votes.insert (std::make_pair (nano::not_an_account (), nano::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () }));
	blocks.insert (std::make_pair (block_a->hash (), block_a));
}

void nano::election::compute_rep_votes (nano::transaction const & transaction_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &transaction_a](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, status.winner));
			this->node.vote_processor.vote (vote, this->node.network.endpoint ());
		});
	}
}

void nano::election::confirm_once (nano::transaction const & transaction_a, bool confirmed_back)
{
	if (!confirmed.exchange (true))
	{
		status.election_end = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		auto winner_l (status.winner);
		auto node_l (node.shared ());
		auto confirmation_action_l (confirmation_action);
		node.background ([node_l, winner_l, confirmation_action_l]() {
			node_l->process_confirmed (winner_l);
			confirmation_action_l (winner_l);
		});
		if (!confirmed_back)
		{
			confirm_back (transaction_a);
		}
	}
}

void nano::election::confirm_back (nano::transaction const & transaction_a)
{
	std::deque<nano::block_hash> hashes = { status.winner->previous (), status.winner->source (), status.winner->link () };
	while (!hashes.empty ())
	{
		auto hash (hashes.front ());
		hashes.pop_front ();
		if (!hash.is_zero () && !node.ledger.is_epoch_link (hash))
		{
			auto existing (node.active.blocks.find (hash));
			if (existing != node.active.blocks.end () && !existing->second->confirmed && !existing->second->stopped && existing->second->blocks.size () == 1)
			{
				release_assert (existing->second->status.winner->hash () == hash);
				existing->second->confirm_once (transaction_a, true); // Avoid recursive actions
				hashes.push_back (existing->second->status.winner->previous ());
				hashes.push_back (existing->second->status.winner->source ());
				hashes.push_back (existing->second->status.winner->link ());
			}
		}
	}
}

void nano::election::stop ()
{
	stopped = true;
}

bool nano::election::have_quorum (nano::tally_t const & tally_a, nano::uint128_t tally_sum)
{
	bool result = false;
	if (tally_sum >= node.config.online_weight_minimum.number ())
	{
		auto i (tally_a.begin ());
		auto first (i->first);
		++i;
		auto second (i != tally_a.end () ? i->first : 0);
		auto delta_l (node.delta ());
		result = tally_a.begin ()->first > (second + delta_l);
	}
	return result;
}

nano::tally_t nano::election::tally (nano::transaction const & transaction_a)
{
	std::unordered_map<nano::block_hash, nano::uint128_t> block_weights;
	for (auto vote_info : last_votes)
	{
		block_weights[vote_info.second.hash] += node.ledger.weight (transaction_a, vote_info.first);
	}
	last_tally = block_weights;
	nano::tally_t result;
	for (auto item : block_weights)
	{
		auto block (blocks.find (item.first));
		if (block != blocks.end ())
		{
			result.insert (std::make_pair (item.second, block->second));
		}
	}
	return result;
}

void nano::election::confirm_if_quorum (nano::transaction const & transaction_a)
{
	auto tally_l (tally (transaction_a));
	assert (tally_l.size () > 0);
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	status.tally = winner->first;
	nano::uint128_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.config.online_weight_minimum.number () && block_l->hash () != status.winner->hash ())
	{
		auto node_l (node.shared ());
		node_l->block_processor.force (block_l);
		status.winner = block_l;
	}
	if (have_quorum (tally_l, sum))
	{
		if (node.config.logging.vote_logging () || blocks.size () > 1)
		{
			log_votes (tally_l);
		}
		confirm_once (transaction_a);
	}
}

void nano::election::log_votes (nano::tally_t const & tally_a)
{
	std::stringstream tally;
	tally << boost::str (boost::format ("\nVote tally for root %1%") % status.winner->root ().to_string ());
	for (auto i (tally_a.begin ()), n (tally_a.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\nBlock %1% weight %2%") % i->second->hash ().to_string () % i->first.convert_to<std::string> ());
	}
	for (auto i (last_votes.begin ()), n (last_votes.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\n%1% %2%") % i->first.to_account () % i->second.hash.to_string ());
	}
	BOOST_LOG (node.log) << tally.str ();
}

nano::election_vote_result nano::election::vote (nano::account rep, uint64_t sequence, nano::block_hash block_hash)
{
	// see republish_vote documentation for an explanation of these rules
	auto transaction (node.store.tx_begin_read ());
	auto replay (false);
	auto supply (node.online_reps.online_stake ());
	auto weight (node.ledger.weight (transaction, rep));
	auto should_process (false);
	if (nano::is_test_network || weight > supply / 1000) // 0.1% or above
	{
		unsigned int cooldown;
		if (weight < supply / 100) // 0.1% to 1%
		{
			cooldown = 15;
		}
		else if (weight < supply / 20) // 1% to 5%
		{
			cooldown = 5;
		}
		else // 5% or above
		{
			cooldown = 1;
		}
		auto last_vote_it (last_votes.find (rep));
		if (last_vote_it == last_votes.end ())
		{
			should_process = true;
		}
		else
		{
			auto last_vote (last_vote_it->second);
			if (last_vote.sequence < sequence || (last_vote.sequence == sequence && last_vote.hash < block_hash))
			{
				if (last_vote.time <= std::chrono::steady_clock::now () - std::chrono::seconds (cooldown))
				{
					should_process = true;
				}
			}
			else
			{
				replay = true;
			}
		}
		if (should_process)
		{
			last_votes[rep] = { std::chrono::steady_clock::now (), sequence, block_hash };
			if (!confirmed)
			{
				confirm_if_quorum (transaction);
			}
		}
	}
	return nano::election_vote_result (replay, should_process);
}

bool nano::node::validate_block_by_previous (nano::transaction const & transaction, std::shared_ptr<nano::block> block_a)
{
	bool result (false);
	nano::account account;
	if (!block_a->previous ().is_zero ())
	{
		if (store.block_exists (transaction, block_a->previous ()))
		{
			account = ledger.account (transaction, block_a->previous ());
		}
		else
		{
			result = true;
		}
	}
	else
	{
		account = block_a->root ();
	}
	if (!result && block_a->type () == nano::block_type::state)
	{
		std::shared_ptr<nano::state_block> block_l (std::static_pointer_cast<nano::state_block> (block_a));
		nano::amount prev_balance (0);
		if (!block_l->hashables.previous.is_zero ())
		{
			if (store.block_exists (transaction, block_l->hashables.previous))
			{
				prev_balance = ledger.balance (transaction, block_l->hashables.previous);
			}
			else
			{
				result = true;
			}
		}
		if (!result)
		{
			if (block_l->hashables.balance == prev_balance && !ledger.epoch_link.is_zero () && ledger.is_epoch_link (block_l->hashables.link))
			{
				account = ledger.epoch_signer;
			}
		}
	}
	if (!result && (account.is_zero () || nano::validate_message (account, block_a->hash (), block_a->block_signature ())))
	{
		result = true;
	}
	return result;
}

bool nano::election::publish (std::shared_ptr<nano::block> block_a)
{
	auto result (false);
	if (blocks.size () >= 10)
	{
		if (last_tally[block_a->hash ()] < node.online_reps.online_stake () / 10)
		{
			result = true;
		}
	}
	if (!result)
	{
		auto transaction (node.store.tx_begin_read ());
		result = node.validate_block_by_previous (transaction, block_a);
		if (!result)
		{
			if (blocks.find (block_a->hash ()) == blocks.end ())
			{
				blocks.insert (std::make_pair (block_a->hash (), block_a));
				confirm_if_quorum (transaction);
				node.network.republish_block (block_a);
			}
		}
	}
	return result;
}

size_t nano::election::last_votes_size ()
{
	std::lock_guard<std::mutex> lock (node.active.mutex);
	return last_votes.size ();
}

void nano::active_transactions::request_confirm (std::unique_lock<std::mutex> & lock_a)
{
	std::unordered_set<nano::uint512_union> inactive;
	auto transaction (node.store.tx_begin_read ());
	unsigned unconfirmed_count (0);
	unsigned unconfirmed_announcements (0);
	std::unordered_map<nano::endpoint, std::vector<std::pair<nano::block_hash, nano::block_hash>>> requests_bundle;
	std::deque<std::shared_ptr<nano::block>> rebroadcast_bundle;
	std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<nano::peer_information>>>> confirm_req_bundle;

	auto roots_size (roots.size ());
	for (auto i (roots.get<1> ().begin ()), n (roots.get<1> ().end ()); i != n; ++i)
	{
		auto root (i->root);
		auto election_l (i->election);
		if ((election_l->confirmed || election_l->stopped) && election_l->announcements >= announcement_min - 1)
		{
			if (election_l->confirmed)
			{
				confirmed.push_back (election_l->status);
				if (confirmed.size () > election_history_size)
				{
					confirmed.pop_front ();
				}
			}
			inactive.insert (root);
		}
		else
		{
			if (election_l->announcements > announcement_long)
			{
				++unconfirmed_count;
				unconfirmed_announcements += election_l->announcements;
				// Log votes for very long unconfirmed elections
				if (election_l->announcements % 50 == 1)
				{
					auto tally_l (election_l->tally (transaction));
					election_l->log_votes (tally_l);
				}
				/* Escalation for long unconfirmed elections
				Start new elections for previous block & source
				if there are less than 100 active elections */
				if (election_l->announcements % announcement_long == 1 && roots_size < 100 && !nano::is_test_network)
				{
					std::shared_ptr<nano::block> previous;
					auto previous_hash (election_l->status.winner->previous ());
					if (!previous_hash.is_zero ())
					{
						previous = node.store.block_get (transaction, previous_hash);
						if (previous != nullptr)
						{
							add (std::move (previous));
						}
					}
					/* If previous block not existing/not commited yet, block_source can cause segfault for state blocks
					So source check can be done only if previous != nullptr or previous is 0 (open account) */
					if (previous_hash.is_zero () || previous != nullptr)
					{
						auto source_hash (node.ledger.block_source (transaction, *election_l->status.winner));
						if (!source_hash.is_zero ())
						{
							auto source (node.store.block_get (transaction, source_hash));
							if (source != nullptr)
							{
								add (std::move (source));
							}
						}
					}
				}
			}
			if (election_l->announcements < announcement_long || election_l->announcements % announcement_long == 1)
			{
				if (node.ledger.could_fit (transaction, *election_l->status.winner))
				{
					// Broadcast winner
					if (rebroadcast_bundle.size () < max_broadcast_queue)
					{
						rebroadcast_bundle.push_back (election_l->status.winner);
					}
				}
				else
				{
					if (election_l->announcements != 0)
					{
						election_l->stop ();
					}
				}
			}
			if (election_l->announcements % 4 == 1)
			{
				auto reps (std::make_shared<std::vector<nano::peer_information>> (node.peers.representatives (std::numeric_limits<size_t>::max ())));
				std::unordered_set<nano::account> probable_reps;
				nano::uint128_t total_weight (0);
				for (auto j (reps->begin ()), m (reps->end ()); j != m;)
				{
					auto & rep_votes (election_l->last_votes);
					auto rep_acct (j->probable_rep_account);
					// Calculate if representative isn't recorded for several IP addresses
					if (probable_reps.find (rep_acct) == probable_reps.end ())
					{
						total_weight = total_weight + j->rep_weight.number ();
						probable_reps.insert (rep_acct);
					}
					if (rep_votes.find (rep_acct) != rep_votes.end ())
					{
						if (j + 1 == reps->end ())
						{
							reps->pop_back ();
							break;
						}

						std::swap (*j, reps->back ());
						reps->pop_back ();
						m = reps->end ();
					}
					else
					{
						++j;
						if (node.config.logging.vote_logging ())
						{
							BOOST_LOG (node.log) << "Representative did not respond to confirm_req, retrying: " << rep_acct.to_account ();
						}
					}
				}
				if ((!reps->empty () && total_weight > node.config.online_weight_minimum.number ()) || roots_size > 5)
				{
					// broadcast_confirm_req_base modifies reps, so we clone it once to avoid aliasing
					if (!nano::is_test_network)
					{
						if (confirm_req_bundle.size () < max_broadcast_queue)
						{
							confirm_req_bundle.push_back (std::make_pair (election_l->status.winner, reps));
						}
					}
					else
					{
						for (auto & rep : *reps)
						{
							auto rep_request (requests_bundle.find (rep.endpoint));
							auto block (election_l->status.winner);
							auto root_hash (std::make_pair (block->hash (), block->root ()));
							if (rep_request == requests_bundle.end ())
							{
								if (requests_bundle.size () < max_broadcast_queue)
								{
									std::vector<std::pair<nano::block_hash, nano::block_hash>> insert_vector = { root_hash };
									requests_bundle.insert (std::make_pair (rep.endpoint, insert_vector));
								}
							}
							else if (rep_request->second.size () < max_broadcast_queue * nano::network::confirm_req_hashes_max)
							{
								rep_request->second.push_back (root_hash);
							}
						}
					}
				}
				else
				{
					if (!nano::is_test_network)
					{
						confirm_req_bundle.push_back (std::make_pair (election_l->status.winner, std::make_shared<std::vector<nano::peer_information>> (node.peers.list_vector (100))));
					}
					else
					{
						for (auto & rep : *reps)
						{
							auto rep_request (requests_bundle.find (rep.endpoint));
							auto block (election_l->status.winner);
							auto root_hash (std::make_pair (block->hash (), block->root ()));
							if (rep_request == requests_bundle.end ())
							{
								std::vector<std::pair<nano::block_hash, nano::block_hash>> insert_vector = { root_hash };
								requests_bundle.insert (std::make_pair (rep.endpoint, insert_vector));
							}
							else
							{
								rep_request->second.push_back (root_hash);
							}
						}
					}
				}
			}
		}
		++election_l->announcements;
	}
	lock_a.unlock ();
	// Rebroadcast unconfirmed blocks
	if (!rebroadcast_bundle.empty ())
	{
		node.network.republish_block_batch (rebroadcast_bundle);
	}
	// Batch confirmation request
	if (!nano::is_live_network && !requests_bundle.empty ())
	{
		node.network.broadcast_confirm_req_batch (requests_bundle, 50);
	}
	//confirm_req broadcast
	if (!confirm_req_bundle.empty ())
	{
		node.network.broadcast_confirm_req_batch (confirm_req_bundle);
	}
	lock_a.lock ();
	for (auto i (inactive.begin ()), n (inactive.end ()); i != n; ++i)
	{
		auto root_it (roots.find (*i));
		assert (root_it != roots.end ());
		for (auto & block : root_it->election->blocks)
		{
			auto erased (blocks.erase (block.first));
			(void)erased;
			assert (erased == 1);
		}
		roots.erase (*i);
	}
	if (unconfirmed_count > 0)
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("%1% blocks have been unconfirmed averaging %2% announcements") % unconfirmed_count % (unconfirmed_announcements / unconfirmed_count));
	}
}

void nano::active_transactions::request_loop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	started = true;

	lock.unlock ();
	condition.notify_all ();
	lock.lock ();

	while (!stopped)
	{
		request_confirm (lock);
		const auto extra_delay (std::min (roots.size (), max_broadcast_queue) * node.network.broadcast_interval_ms * 2);
		condition.wait_for (lock, std::chrono::milliseconds (request_interval_ms + extra_delay));
	}
}

void nano::active_transactions::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
	lock.lock ();
	roots.clear ();
}

bool nano::active_transactions::start (std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return add (block_a, confirmation_action_a);
}

bool nano::active_transactions::add (std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a)
{
	auto error (true);
	if (!stopped)
	{
		auto root (nano::uint512_union (block_a->previous (), block_a->root ()));
		auto existing (roots.find (root));
		if (existing == roots.end ())
		{
			auto election (std::make_shared<nano::election> (node, block_a, confirmation_action_a));
			uint64_t difficulty (0);
			auto error (nano::work_validate (*block_a, &difficulty));
			release_assert (!error);
			roots.insert (nano::conflict_info{ root, difficulty, election });
			blocks.insert (std::make_pair (block_a->hash (), election));
		}
		error = existing != roots.end ();
	}
	return error;
}

// Validate a vote and apply it to the current election if one exists
bool nano::active_transactions::vote (std::shared_ptr<nano::vote> vote_a, bool single_lock)
{
	std::shared_ptr<nano::election> election;
	bool replay (false);
	bool processed (false);
	{
		std::unique_lock<std::mutex> lock;
		if (!single_lock)
		{
			lock = std::unique_lock<std::mutex> (mutex);
		}
		for (auto vote_block : vote_a->blocks)
		{
			nano::election_vote_result result;
			if (vote_block.which ())
			{
				auto block_hash (boost::get<nano::block_hash> (vote_block));
				auto existing (blocks.find (block_hash));
				if (existing != blocks.end ())
				{
					result = existing->second->vote (vote_a->account, vote_a->sequence, block_hash);
				}
			}
			else
			{
				auto block (boost::get<std::shared_ptr<nano::block>> (vote_block));
				auto existing (roots.find (nano::uint512_union (block->previous (), block->root ())));
				if (existing != roots.end ())
				{
					result = existing->election->vote (vote_a->account, vote_a->sequence, block->hash ());
				}
			}
			replay = replay || result.replay;
			processed = processed || result.processed;
		}
	}
	if (processed)
	{
		node.network.republish_vote (vote_a);
	}
	return replay;
}

bool nano::active_transactions::active (nano::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.find (nano::uint512_union (block_a.previous (), block_a.root ())) != roots.end ();
}

void nano::active_transactions::update_difficulty (nano::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (nano::uint512_union (block_a.previous (), block_a.root ())));
	if (existing != roots.end ())
	{
		uint64_t difficulty;
		auto error (nano::work_validate (block_a, &difficulty));
		assert (!error);
		roots.modify (existing, [difficulty](nano::conflict_info & info_a) {
			info_a.difficulty = difficulty;
		});
	}
}

// List of active blocks in elections
std::deque<std::shared_ptr<nano::block>> nano::active_transactions::list_blocks (bool single_lock)
{
	std::deque<std::shared_ptr<nano::block>> result;
	std::unique_lock<std::mutex> lock;
	if (!single_lock)
	{
		lock = std::unique_lock<std::mutex> (mutex);
	}
	for (auto i (roots.begin ()), n (roots.end ()); i != n; ++i)
	{
		result.push_back (i->election->status.winner);
	}
	return result;
}

std::deque<nano::election_status> nano::active_transactions::list_confirmed ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return confirmed;
}

void nano::active_transactions::erase (nano::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	if (roots.find (nano::uint512_union (block_a.previous (), block_a.root ())) != roots.end ())
	{
		roots.erase (nano::uint512_union (block_a.previous (), block_a.root ()));
		BOOST_LOG (node.log) << boost::str (boost::format ("Election erased for block block %1% root %2%") % block_a.hash ().to_string () % block_a.root ().to_string ());
	}
}

bool nano::active_transactions::empty ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.empty ();
}

size_t nano::active_transactions::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.size ();
}

nano::active_transactions::active_transactions (nano::node & node_a) :
node (node_a),
started (false),
stopped (false),
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::request_loop);
	request_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

nano::active_transactions::~active_transactions ()
{
	stop ();
}

bool nano::active_transactions::publish (std::shared_ptr<nano::block> block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (nano::uint512_union (block_a->previous (), block_a->root ())));
	auto result (true);
	if (existing != roots.end ())
	{
		result = existing->election->publish (block_a);
		if (!result)
		{
			blocks.insert (std::make_pair (block_a->hash (), existing->election));
		}
	}
	return result;
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name)
{
	size_t roots_count = 0;
	size_t blocks_count = 0;
	size_t confirmed_count = 0;

	{
		std::lock_guard<std::mutex> guard (active_transactions.mutex);
		roots_count = active_transactions.roots.size ();
		blocks_count = active_transactions.blocks.size ();
		confirmed_count = active_transactions.confirmed.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "roots", roots_count, sizeof (decltype (active_transactions.roots)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", blocks_count, sizeof (decltype (active_transactions.blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "confirmed", confirmed_count, sizeof (decltype (active_transactions.confirmed)::value_type) }));
	return composite;
}
}
int nano::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version_get (transaction);
}

nano::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned service_threads_a)
{
	boost::thread::attributes attrs;
	nano::thread_attributes::set (attrs);
	for (auto i (0u); i < service_threads_a; ++i)
	{
		threads.push_back (boost::thread (attrs, [&io_ctx_a]() {
			nano::thread_role::set (nano::thread_role::name::io);
			try
			{
				io_ctx_a.run ();
			}
			catch (...)
			{
#ifndef NDEBUG
				/*
				 * In a release build, catch and swallow the
				 * io_context exception, in debug mode pass it
				 * on
				 */
				throw;
#endif
			}
		}));
	}
}

nano::thread_runner::~thread_runner ()
{
	join ();
}

void nano::thread_runner::join ()
{
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

nano::inactive_node::inactive_node (boost::filesystem::path const & path, uint16_t peering_port_a) :
path (path),
io_context (std::make_shared<boost::asio::io_context> ()),
alarm (*io_context),
work (1, nullptr),
peering_port (peering_port_a)
{
	boost::system::error_code error_chmod;

	/*
	 * @warning May throw a filesystem exception
	 */
	boost::filesystem::create_directories (path);
	nano::set_secure_perm_directory (path, error_chmod);
	logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
	logging.init (path);
	node = std::make_shared<nano::node> (init, *io_context, peering_port, path, alarm, logging, work);
}

nano::inactive_node::~inactive_node ()
{
	node->stop ();
}

nano::udp_buffer::udp_buffer (nano::stat & stats, size_t size, size_t count) :
stats (stats),
free (count),
full (count),
slab (size * count),
entries (count),
stopped (false)
{
	assert (count > 0);
	assert (size > 0);
	auto slab_data (slab.data ());
	auto entry_data (entries.data ());
	for (auto i (0); i < count; ++i, ++entry_data)
	{
		*entry_data = { slab_data + i * size, 0, nano::endpoint () };
		free.push_back (entry_data);
	}
}
nano::udp_data * nano::udp_buffer::allocate ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && free.empty () && full.empty ())
	{
		stats.inc (nano::stat::type::udp, nano::stat::detail::blocking, nano::stat::dir::in);
		condition.wait (lock);
	}
	nano::udp_data * result (nullptr);
	if (!free.empty ())
	{
		result = free.front ();
		free.pop_front ();
	}
	if (result == nullptr)
	{
		result = full.front ();
		full.pop_front ();
		stats.inc (nano::stat::type::udp, nano::stat::detail::overflow, nano::stat::dir::in);
	}
	return result;
}
void nano::udp_buffer::enqueue (nano::udp_data * data_a)
{
	assert (data_a != nullptr);
	{
		std::lock_guard<std::mutex> lock (mutex);
		full.push_back (data_a);
	}
	condition.notify_all ();
}
nano::udp_data * nano::udp_buffer::dequeue ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && full.empty ())
	{
		condition.wait (lock);
	}
	nano::udp_data * result (nullptr);
	if (!full.empty ())
	{
		result = full.front ();
		full.pop_front ();
	}
	return result;
}
void nano::udp_buffer::release (nano::udp_data * data_a)
{
	assert (data_a != nullptr);
	{
		std::lock_guard<std::mutex> lock (mutex);
		free.push_back (data_a);
	}
	condition.notify_all ();
}
void nano::udp_buffer::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
}
