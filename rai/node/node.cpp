#include <rai/node/node.hpp>

#include <rai/lib/interface.h>
#include <rai/lib/utility.hpp>
#include <rai/node/common.hpp>
#include <rai/node/rpc.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <sstream>

#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>

double constexpr rai::node::price_max;
double constexpr rai::node::free_cutoff;
std::chrono::seconds constexpr rai::node::period;
std::chrono::seconds constexpr rai::node::cutoff;
std::chrono::seconds constexpr rai::node::syn_cookie_cutoff;
std::chrono::minutes constexpr rai::node::backup_interval;
std::chrono::seconds constexpr rai::node::search_pending_interval;
int constexpr rai::port_mapping::mapping_timeout;
int constexpr rai::port_mapping::check_timeout;
unsigned constexpr rai::active_transactions::announce_interval_ms;
size_t constexpr rai::active_transactions::max_broadcast_queue;
size_t constexpr rai::block_arrival::arrival_size_min;
std::chrono::seconds constexpr rai::block_arrival::arrival_time_min;

namespace rai
{
extern unsigned char rai_bootstrap_weights[];
extern size_t rai_bootstrap_weights_size;
}

rai::network::network (rai::node & node_a, uint16_t port) :
buffer_container (node_a.stats, rai::network::buffer_size, 4096), // 2Mb receive buffer
socket (node_a.io_ctx, rai::endpoint (boost::asio::ip::address_v6::any (), port)),
resolver (node_a.io_ctx),
node (node_a),
on (true)
{
	boost::thread::attributes attrs;
	rai::thread_attributes::set (attrs);
	for (size_t i = 0; i < node.config.network_threads; ++i)
	{
		packet_processing_threads.push_back (boost::thread (attrs, [this]() {
			rai::thread_role::set (rai::thread_role::name::packet_processing);
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

rai::network::~network ()
{
	for (auto & thread : packet_processing_threads)
	{
		thread.join ();
	}
}

void rai::network::start ()
{
	for (size_t i = 0; i < node.config.io_threads; ++i)
	{
		receive ();
	}
}

void rai::network::receive ()
{
	if (node.config.logging.network_packet_logging ())
	{
		BOOST_LOG (node.log) << "Receiving packet";
	}
	std::unique_lock<std::mutex> lock (socket_mutex);
	auto data (buffer_container.allocate ());
	socket.async_receive_from (boost::asio::buffer (data->buffer, rai::network::buffer_size), data->endpoint, [this, data](boost::system::error_code const & error, size_t size_a) {
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

void rai::network::process_packets ()
{
	while (on)
	{
		auto data (buffer_container.dequeue ());
		if (data == nullptr)
		{
			break;
		}
		//std::cerr << data->endpoint.address ().to_string ();
		receive_action (data);
		buffer_container.release (data);
	}
}

void rai::network::stop ()
{
	on = false;
	socket.close ();
	resolver.cancel ();
	buffer_container.stop ();
}

void rai::network::send_keepalive (rai::endpoint const & endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	rai::keepalive message;
	node.peers.random_fill (message.peers);
	auto bytes = message.to_bytes ();
	if (node.config.logging.network_keepalive_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Keepalive req sent to %1%") % endpoint_a);
	}
	std::weak_ptr<rai::node> node_w (node.shared ());
	send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w, endpoint_a](boost::system::error_code const & ec, size_t) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_keepalive_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending keepalive to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (rai::stat::type::message, rai::stat::detail::keepalive, rai::stat::dir::out);
			}
		}
	});
}

void rai::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
			{
				node_l->send_keepalive (rai::map_endpoint_to_v6 (i->endpoint ()));
			}
		}
		else
		{
			BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ());
		}
	});
}

void rai::network::send_node_id_handshake (rai::endpoint const & endpoint_a, boost::optional<rai::uint256_union> const & query, boost::optional<rai::uint256_union> const & respond_to)
{
	assert (endpoint_a.address ().is_v6 ());
	boost::optional<std::pair<rai::account, rai::signature>> response (boost::none);
	if (respond_to)
	{
		response = std::make_pair (node.node_id.pub, rai::sign_message (node.node_id.prv, node.node_id.pub, *respond_to));
		assert (!rai::validate_message (response->first, *respond_to, response->second));
	}
	rai::node_id_handshake message (query, response);
	auto bytes = message.to_bytes ();
	if (node.config.logging.network_node_id_handshake_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Node ID handshake sent with node ID %1% to %2%: query %3%, respond_to %4% (signature %5%)") % node.node_id.pub.to_account () % endpoint_a % (query ? query->to_string () : std::string ("[none]")) % (respond_to ? respond_to->to_string () : std::string ("[none]")) % (response ? response->second.to_string () : std::string ("[none]")));
	}
	node.stats.inc (rai::stat::type::message, rai::stat::detail::node_id_handshake, rai::stat::dir::out);
	std::weak_ptr<rai::node> node_w (node.shared ());
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

void rai::network::republish (rai::block_hash const & hash_a, std::shared_ptr<std::vector<uint8_t>> buffer_a, rai::endpoint endpoint_a)
{
	if (node.config.logging.network_publish_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Publishing %1% to %2%") % hash_a.to_string () % endpoint_a);
	}
	std::weak_ptr<rai::node> node_w (node.shared ());
	send_buffer (buffer_a->data (), buffer_a->size (), endpoint_a, [buffer_a, node_w, endpoint_a](boost::system::error_code const & ec, size_t size) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending publish to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (rai::stat::type::message, rai::stat::detail::publish, rai::stat::dir::out);
			}
		}
	});
}

template <typename T>
bool confirm_block (rai::transaction const & transaction_a, rai::node & node_a, T & list_a, std::shared_ptr<rai::block> block_a, bool also_publish)
{
	bool result (false);
	if (node_a.config.enable_voting)
	{
		node_a.wallets.foreach_representative (transaction_a, [&result, &block_a, &list_a, &node_a, &transaction_a, also_publish](rai::public_key const & pub_a, rai::raw_key const & prv_a) {
			result = true;
			auto hash (block_a->hash ());
			auto vote (node_a.store.vote_generate (transaction_a, pub_a, prv_a, std::vector<rai::block_hash> (1, hash)));
			rai::confirm_ack confirm (vote);
			auto vote_bytes = confirm.to_bytes ();
			rai::publish publish (block_a);
			std::shared_ptr<std::vector<uint8_t>> publish_bytes;
			if (also_publish)
			{
				publish_bytes = publish.to_bytes ();
			}
			for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
			{
				node_a.network.confirm_send (confirm, vote_bytes, *j);
				if (also_publish)
				{
					node_a.network.republish (hash, publish_bytes, *j);
				}
			}
		});
	}
	return result;
}

bool confirm_block (rai::transaction const & transaction_a, rai::node & node_a, rai::endpoint & peer_a, std::shared_ptr<rai::block> block_a, bool also_publish)
{
	std::array<rai::endpoint, 1> endpoints;
	endpoints[0] = peer_a;
	auto result (confirm_block (transaction_a, node_a, endpoints, std::move (block_a), also_publish));
	return result;
}

void rai::network::republish_block (std::shared_ptr<rai::block> block)
{
	auto hash (block->hash ());
	auto list (node.peers.list_fanout ());
	rai::publish message (block);
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

void rai::network::republish_block_batch (std::deque<std::shared_ptr<rai::block>> blocks_a, unsigned delay_a)
{
	auto block (blocks_a.front ());
	blocks_a.pop_front ();
	republish_block (block);
	if (!blocks_a.empty ())
	{
		std::weak_ptr<rai::node> node_w (node.shared ());
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
void rai::network::republish_vote (std::shared_ptr<rai::vote> vote_a)
{
	rai::confirm_ack confirm (vote_a);
	auto bytes = confirm.to_bytes ();
	auto list (node.peers.list_fanout ());
	for (auto j (list.begin ()), m (list.end ()); j != m; ++j)
	{
		node.network.confirm_send (confirm, bytes, *j);
	}
}

void rai::network::broadcast_confirm_req (std::shared_ptr<rai::block> block_a)
{
	auto list (std::make_shared<std::vector<rai::peer_information>> (node.peers.representatives (std::numeric_limits<size_t>::max ())));
	if (list->empty () || node.peers.total_weight () < node.config.online_weight_minimum.number ())
	{
		// broadcast request to all peers
		list = std::make_shared<std::vector<rai::peer_information>> (node.peers.list_vector (100));
	}

	/*
	 * In either case (broadcasting to all representatives, or broadcasting to
	 * all peers because there are not enough connected representatives),
	 * limit each instance to a single random up-to-32 selection.  The invoker
	 * of "broadcast_confirm_req" will be responsible for calling it again
	 * if the votes for a block have not arrived in time.
	 */
	const size_t max_endpoints = 32;
	std::random_shuffle (list->begin (), list->end ());
	if (list->size () > max_endpoints)
	{
		list->erase (list->begin () + max_endpoints, list->end ());
	}

	broadcast_confirm_req_base (block_a, list, 0);
}

void rai::network::broadcast_confirm_req_base (std::shared_ptr<rai::block> block_a, std::shared_ptr<std::vector<rai::peer_information>> endpoints_a, unsigned delay_a, bool resumption)
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

		std::weak_ptr<rai::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a), [node_w, block_a, endpoints_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_base (block_a, endpoints_a, delay_a, true);
			}
		});
	}
}

void rai::network::broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<rai::block>, std::shared_ptr<std::vector<rai::peer_information>>>> deque_a, unsigned delay_a)
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
		std::weak_ptr<rai::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a + std::rand () % delay_a), [node_w, deque_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_batch (deque_a, delay_a);
			}
		});
	}
}

void rai::network::send_confirm_req (rai::endpoint const & endpoint_a, std::shared_ptr<rai::block> block)
{
	rai::confirm_req message (block);
	auto bytes = message.to_bytes ();
	if (node.config.logging.network_message_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm req to %1%") % endpoint_a);
	}
	std::weak_ptr<rai::node> node_w (node.shared ());
	node.stats.inc (rai::stat::type::message, rai::stat::detail::confirm_req, rai::stat::dir::out);
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

template <typename T>
void rep_query (rai::node & node_a, T const & peers_a)
{
	auto transaction (node_a.store.tx_begin_read ());
	std::shared_ptr<rai::block> block (node_a.store.block_random (transaction));
	auto hash (block->hash ());
	node_a.rep_crawler.add (hash);
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		node_a.peers.rep_request (*i);
		node_a.network.send_confirm_req (*i, block);
	}
	std::weak_ptr<rai::node> node_w (node_a.shared ());
	node_a.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->rep_crawler.remove (hash);
		}
	});
}

void rep_query (rai::node & node_a, rai::endpoint const & peers_a)
{
	std::array<rai::endpoint, 1> peers;
	peers[0] = peers_a;
	rep_query (node_a, peers);
}

namespace
{
class network_message_visitor : public rai::message_visitor
{
public:
	network_message_visitor (rai::node & node_a, rai::endpoint const & sender_a) :
	node (node_a),
	sender (sender_a)
	{
	}
	virtual ~network_message_visitor () = default;
	void keepalive (rai::keepalive const & message_a) override
	{
		if (node.config.logging.network_keepalive_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received keepalive message from %1%") % sender);
		}
		node.stats.inc (rai::stat::type::message, rai::stat::detail::keepalive, rai::stat::dir::in);
		if (node.peers.contacted (sender, message_a.header.version_using))
		{
			auto endpoint_l (rai::map_endpoint_to_v6 (sender));
			auto cookie (node.peers.assign_syn_cookie (endpoint_l));
			if (cookie)
			{
				node.network.send_node_id_handshake (endpoint_l, *cookie, boost::none);
			}
		}
		node.network.merge_peers (message_a.peers);
	}
	void publish (rai::publish const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Publish message from %1% for %2%") % sender % message_a.block->hash ().to_string ());
		}
		node.stats.inc (rai::stat::type::message, rai::stat::detail::publish, rai::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
		node.process_active (message_a.block);
		node.active.publish (message_a.block);
	}
	void confirm_req (rai::confirm_req const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Confirm_req message from %1% for %2%") % sender % message_a.block->hash ().to_string ());
		}
		node.stats.inc (rai::stat::type::message, rai::stat::detail::confirm_req, rai::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
		// Don't load nodes with disabled voting
		if (node.config.enable_voting)
		{
			auto transaction (node.store.tx_begin_read ());
			auto successor (node.ledger.successor (transaction, message_a.block->root ()));
			if (successor != nullptr)
			{
				auto same_block (successor->hash () == message_a.block->hash ());
				confirm_block (transaction, node, sender, std::move (successor), !same_block);
			}
		}
	}
	void confirm_ack (rai::confirm_ack const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received confirm_ack message from %1% for %2%sequence %3%") % sender % message_a.vote->hashes_string () % std::to_string (message_a.vote->sequence));
		}
		node.stats.inc (rai::stat::type::message, rai::stat::detail::confirm_ack, rai::stat::dir::in);
		node.peers.contacted (sender, message_a.header.version_using);
		for (auto & vote_block : message_a.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<rai::block>> (vote_block));
				node.process_active (block);
				node.active.publish (block);
			}
		}
		node.vote_processor.vote (message_a.vote, sender);
	}
	void bulk_pull (rai::bulk_pull const &) override
	{
		assert (false);
	}
	void bulk_pull_account (rai::bulk_pull_account const &) override
	{
		assert (false);
	}
	void bulk_pull_blocks (rai::bulk_pull_blocks const &) override
	{
		assert (false);
	}
	void bulk_push (rai::bulk_push const &) override
	{
		assert (false);
	}
	void frontier_req (rai::frontier_req const &) override
	{
		assert (false);
	}
	void node_id_handshake (rai::node_id_handshake const & message_a) override
	{
		if (node.config.logging.network_node_id_handshake_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Received node_id_handshake message from %1% with query %2% and response account %3%") % sender % (message_a.query ? message_a.query->to_string () : std::string ("[none]")) % (message_a.response ? message_a.response->first.to_account () : std::string ("[none]")));
		}
		node.stats.inc (rai::stat::type::message, rai::stat::detail::node_id_handshake, rai::stat::dir::in);
		auto endpoint_l (rai::map_endpoint_to_v6 (sender));
		boost::optional<rai::uint256_union> out_query;
		boost::optional<rai::uint256_union> out_respond_to;
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
					node.peers.insert (endpoint_l, message_a.header.version_using);
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
	}
	rai::node & node;
	rai::endpoint sender;
};
}

void rai::network::receive_action (rai::udp_data * data_a)
{
	auto allowed_sender (true);
	if (data_a->endpoint == endpoint ())
	{
		allowed_sender = false;
	}
	else if (rai::reserved_address (data_a->endpoint, false) && !node.config.allow_local_peers)
	{
		allowed_sender = false;
	}
	if (allowed_sender)
	{
		network_message_visitor visitor (node, data_a->endpoint);
		rai::message_parser parser (node.block_uniquer, node.vote_uniquer, visitor, node.work);
		parser.deserialize_buffer (data_a->buffer, data_a->size);
		if (parser.status != rai::message_parser::parse_status::success)
		{
			node.stats.inc (rai::stat::type::error);

			switch (parser.status)
			{
				case rai::message_parser::parse_status::insufficient_work:
					// We've already increment error count, update detail only
					node.stats.inc_detail_only (rai::stat::type::error, rai::stat::detail::insufficient_work);
					break;
				case rai::message_parser::parse_status::invalid_magic:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_magic);
					break;
				case rai::message_parser::parse_status::invalid_network:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_network);
					break;
				case rai::message_parser::parse_status::invalid_header:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_header);
					break;
				case rai::message_parser::parse_status::invalid_message_type:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_message_type);
					break;
				case rai::message_parser::parse_status::invalid_keepalive_message:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_keepalive_message);
					break;
				case rai::message_parser::parse_status::invalid_publish_message:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_publish_message);
					break;
				case rai::message_parser::parse_status::invalid_confirm_req_message:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_confirm_req_message);
					break;
				case rai::message_parser::parse_status::invalid_confirm_ack_message:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_confirm_ack_message);
					break;
				case rai::message_parser::parse_status::invalid_node_id_handshake_message:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::invalid_node_id_handshake_message);
					break;
				case rai::message_parser::parse_status::outdated_version:
					node.stats.inc (rai::stat::type::udp, rai::stat::detail::outdated_version);
					break;
				case rai::message_parser::parse_status::success:
					/* Already checked, unreachable */
					break;
			}

			if (node.config.logging.network_logging ())
			{
				BOOST_LOG (node.log) << "Could not parse message.  Error: " << parser.status_string ();
			}
		}
		else
		{
			node.stats.add (rai::stat::type::traffic, rai::stat::dir::in, data_a->size);
		}
	}
	else
	{
		if (node.config.logging.network_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Reserved sender %1%") % data_a->endpoint.address ().to_string ());
		}

		node.stats.inc_detail_only (rai::stat::type::error, rai::stat::detail::bad_sender);
	}
}

// Send keepalives to all the peers we've been notified of
void rai::network::merge_peers (std::array<rai::endpoint, 8> const & peers_a)
{
	for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
	{
		if (!node.peers.reachout (*i))
		{
			send_keepalive (*i);
		}
	}
}

bool rai::operation::operator> (rai::operation const & other_a) const
{
	return wakeup > other_a.wakeup;
}

rai::alarm::alarm (boost::asio::io_context & io_ctx_a) :
io_ctx (io_ctx_a),
thread ([this]() {
	rai::thread_role::set (rai::thread_role::name::alarm);
	run ();
})
{
}

rai::alarm::~alarm ()
{
	add (std::chrono::steady_clock::now (), nullptr);
	thread.join ();
}

void rai::alarm::run ()
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

void rai::alarm::add (std::chrono::steady_clock::time_point const & wakeup_a, std::function<void()> const & operation)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		operations.push (rai::operation ({ wakeup_a, operation }));
	}
	condition.notify_all ();
}

rai::node_init::node_init () :
block_store_init (false),
wallet_init (false)
{
}

bool rai::node_init::error ()
{
	return block_store_init || wallet_init;
}

rai::vote_processor::vote_processor (rai::node & node_a) :
node (node_a),
started (false),
stopped (false),
active (false),
thread ([this]() {
	rai::thread_role::set (rai::thread_role::name::vote_processing);
	process_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

void rai::vote_processor::process_loop ()
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
			std::deque<std::pair<std::shared_ptr<rai::vote>, rai::endpoint>> votes_l;
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

void rai::vote_processor::vote (std::shared_ptr<rai::vote> vote_a, rai::endpoint endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		bool process (false);
		/* Random early delection levels
		Always process votes for test network (process = true)
		Stop processing with max 144 * 1024 votes */
		if (rai::rai_network != rai::rai_networks::rai_test_network)
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
			node.stats.inc (rai::stat::type::vote, rai::stat::detail::vote_overflow);
			if (node.config.logging.vote_logging ())
			{
				BOOST_LOG (node.log) << "Votes overflow";
			}
		}
	}
}

void rai::vote_processor::verify_votes (std::deque<std::pair<std::shared_ptr<rai::vote>, rai::endpoint>> & votes_a)
{
	auto size (votes_a.size ());
	std::vector<unsigned char const *> messages;
	messages.reserve (size);
	std::vector<rai::uint256_union> hashes;
	hashes.reserve (size);
	std::vector<size_t> lengths (size, sizeof (rai::uint256_union));
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
	std::promise<void> promise;
	rai::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data (), &promise };
	node.checker.add (check);
	promise.get_future ().wait ();
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
rai::vote_code rai::vote_processor::vote_blocking (rai::transaction const & transaction_a, std::shared_ptr<rai::vote> vote_a, rai::endpoint endpoint_a, bool validated)
{
	assert (endpoint_a.address ().is_v6 ());
	assert (!node.active.mutex.try_lock ());
	auto result (rai::vote_code::invalid);
	if (validated || !vote_a->validate ())
	{
		auto max_vote (node.store.vote_max (transaction_a, vote_a));
		result = rai::vote_code::replay;
		if (!node.active.vote (vote_a, true))
		{
			result = rai::vote_code::vote;
		}
		switch (result)
		{
			case rai::vote_code::vote:
				node.observers.vote.notify (transaction_a, vote_a, endpoint_a);
			case rai::vote_code::replay:
				// This tries to assist rep nodes that have lost track of their highest sequence number by replaying our highest known vote back to them
				// Only do this if the sequence number is significantly different to account for network reordering
				// Amplify attack considerations: We're sending out a confirm_ack in response to a confirm_ack for no net traffic increase
				if (max_vote->sequence > vote_a->sequence + 10000)
				{
					rai::confirm_ack confirm (max_vote);
					node.network.confirm_send (confirm, confirm.to_bytes (), endpoint_a);
				}
				break;
			case rai::vote_code::invalid:
				assert (false);
				break;
		}
	}
	std::string status;
	switch (result)
	{
		case rai::vote_code::invalid:
			status = "Invalid";
			node.stats.inc (rai::stat::type::vote, rai::stat::detail::vote_invalid);
			break;
		case rai::vote_code::replay:
			status = "Replay";
			node.stats.inc (rai::stat::type::vote, rai::stat::detail::vote_replay);
			break;
		case rai::vote_code::vote:
			status = "Vote";
			node.stats.inc (rai::stat::type::vote, rai::stat::detail::vote_valid);
			break;
	}
	if (node.config.logging.vote_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Vote from: %1% sequence: %2% block(s): %3%status: %4%") % vote_a->account.to_account () % std::to_string (vote_a->sequence) % vote_a->hashes_string () % status);
	}
	return result;
}

void rai::vote_processor::stop ()
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

void rai::vote_processor::flush ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (active || !votes.empty ())
	{
		condition.wait (lock);
	}
}

void rai::vote_processor::calculate_weights ()
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
			rai::account representative (i->first);
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

void rai::rep_crawler::add (rai::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	active.insert (hash_a);
}

void rai::rep_crawler::remove (rai::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	active.erase (hash_a);
}

bool rai::rep_crawler::exists (rai::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return active.count (hash_a) != 0;
}

rai::signature_checker::signature_checker () :
started (false),
stopped (false),
thread ([this]() { run (); })
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

rai::signature_checker::~signature_checker ()
{
	stop ();
}

void rai::signature_checker::add (rai::signature_check_set & check_a)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		checks.push_back (check_a);
	}
	condition.notify_all ();
}

void rai::signature_checker::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void rai::signature_checker::flush ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && !checks.empty ())
	{
		condition.wait (lock);
	}
}

void rai::signature_checker::verify (rai::signature_check_set & check_a)
{
	/* Verifications is vector if signatures check results
	 validate_message_batch returing "true" if there are at least 1 invalid signature */
	auto code (rai::validate_message_batch (check_a.messages, check_a.message_lengths, check_a.pub_keys, check_a.signatures, check_a.size, check_a.verifications));
	(void)code;
	release_assert (std::all_of (check_a.verifications, check_a.verifications + check_a.size, [](int verification) { return verification == 0 || verification == 1; }));
	check_a.promise->set_value ();
}

void rai::signature_checker::run ()
{
	rai::thread_role::set (rai::thread_role::name::signature_checking);
	std::unique_lock<std::mutex> lock (mutex);
	started = true;

	lock.unlock ();
	condition.notify_all ();
	lock.lock ();

	while (!stopped)
	{
		if (!checks.empty ())
		{
			auto check (checks.front ());
			checks.pop_front ();
			lock.unlock ();
			verify (check);
			condition.notify_all ();
			lock.lock ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

rai::block_processor::block_processor (rai::node & node_a) :
stopped (false),
active (false),
next_log (std::chrono::steady_clock::now ()),
node (node_a),
generator (node_a, rai::rai_network == rai::rai_networks::rai_test_network ? std::chrono::milliseconds (10) : std::chrono::milliseconds (500))
{
}

rai::block_processor::~block_processor ()
{
	stop ();
}

void rai::block_processor::stop ()
{
	generator.stop ();
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
}

void rai::block_processor::flush ()
{
	node.checker.flush ();
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && (have_blocks () || active))
	{
		condition.wait (lock);
	}
}

bool rai::block_processor::full ()
{
	std::unique_lock<std::mutex> lock (mutex);
	return (blocks.size () + state_blocks.size ()) > 16384;
}

void rai::block_processor::add (std::shared_ptr<rai::block> block_a, std::chrono::steady_clock::time_point origination)
{
	if (!rai::work_validate (block_a->root (), block_a->block_work ()))
	{
		{
			std::lock_guard<std::mutex> lock (mutex);
			if (blocks_hashes.find (block_a->hash ()) == blocks_hashes.end ())
			{
				if (block_a->type () == rai::block_type::state && !node.ledger.is_epoch_link (block_a->link ()))
				{
					state_blocks.push_back (std::make_pair (block_a, origination));
				}
				else
				{
					blocks.push_back (std::make_pair (block_a, origination));
				}
			}
			condition.notify_all ();
		}
	}
	else
	{
		BOOST_LOG (node.log) << "rai::block_processor::add called for hash " << block_a->hash ().to_string () << " with invalid work " << rai::to_string_hex (block_a->block_work ());
		assert (false && "rai::block_processor::add called with invalid work");
	}
}

void rai::block_processor::force (std::shared_ptr<rai::block> block_a)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		forced.push_back (block_a);
	}
	condition.notify_all ();
}

void rai::block_processor::process_blocks ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (have_blocks ())
		{
			active = true;
			lock.unlock ();
			process_receive_many (lock);
			lock.lock ();
			active = false;
		}
		else
		{
			lock.unlock ();
			condition.notify_all ();
			lock.lock ();

			condition.wait (lock);
		}
	}
}

bool rai::block_processor::should_log (bool first_time)
{
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (first_time || next_log < now)
	{
		next_log = now + std::chrono::seconds (15);
		result = true;
	}
	return result;
}

bool rai::block_processor::have_blocks ()
{
	assert (!mutex.try_lock ());
	return !blocks.empty () || !forced.empty () || !state_blocks.empty ();
}

void rai::block_processor::verify_state_blocks (std::unique_lock<std::mutex> & lock_a, size_t max_count)
{
	assert (!mutex.try_lock ());
	auto start_time (std::chrono::steady_clock::now ());
	std::deque<std::pair<std::shared_ptr<rai::block>, std::chrono::steady_clock::time_point>> items;
	if (max_count == std::numeric_limits<size_t>::max () || max_count >= state_blocks.size ())
	{
		items.swap (state_blocks);
	}
	else
	{
		auto keep_size (state_blocks.size () - max_count);
		items.resize (keep_size);
		std::swap_ranges (state_blocks.end () - keep_size, state_blocks.end (), items.begin ());
		state_blocks.resize (max_count);
		items.swap (state_blocks);
	}
	lock_a.unlock ();
	auto size (items.size ());
	std::vector<rai::uint256_union> hashes;
	hashes.reserve (size);
	std::vector<unsigned char const *> messages;
	messages.reserve (size);
	std::vector<size_t> lengths;
	lengths.reserve (size);
	std::vector<unsigned char const *> pub_keys;
	pub_keys.reserve (size);
	std::vector<unsigned char const *> signatures;
	signatures.reserve (size);
	std::vector<int> verifications;
	verifications.resize (size, 0);
	for (auto i (0); i < size; ++i)
	{
		auto & block (static_cast<rai::state_block &> (*items[i].first));
		hashes.push_back (block.hash ());
		messages.push_back (hashes.back ().bytes.data ());
		lengths.push_back (sizeof (decltype (hashes)::value_type));
		pub_keys.push_back (block.hashables.account.bytes.data ());
		signatures.push_back (block.signature.bytes.data ());
	}
	std::promise<void> promise;
	rai::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data (), &promise };
	node.checker.add (check);
	promise.get_future ().wait ();
	lock_a.lock ();
	for (auto i (0); i < size; ++i)
	{
		assert (verifications[i] == 1 || verifications[i] == 0);
		if (verifications[i] == 1)
		{
			blocks.push_back (items.front ());
		}
		items.pop_front ();
	}
	if (node.config.logging.timing_logging ())
	{
		auto end_time (std::chrono::steady_clock::now ());
		auto elapsed_time_ms (std::chrono::duration_cast<std::chrono::milliseconds> (end_time - start_time));
		auto elapsed_time_ms_int (elapsed_time_ms.count ());

		BOOST_LOG (node.log) << boost::str (boost::format ("Batch verified %1% state blocks in %2% milliseconds") % size % elapsed_time_ms_int);
	}
}

void rai::block_processor::process_receive_many (std::unique_lock<std::mutex> & lock_a)
{
	lock_a.lock ();
	auto start_time (std::chrono::steady_clock::now ());
	// Limit state blocks verification time
	while (!state_blocks.empty () && std::chrono::steady_clock::now () - start_time < std::chrono::seconds (2))
	{
		verify_state_blocks (lock_a, 2048);
	}
	lock_a.unlock ();
	auto transaction (node.store.tx_begin_write ());
	start_time = std::chrono::steady_clock::now ();
	lock_a.lock ();
	// Processing blocks
	auto first_time (true);
	unsigned number_of_blocks_processed (0), number_of_forced_processed (0);
	while ((!blocks.empty () || !forced.empty ()) && std::chrono::steady_clock::now () - start_time < node.config.block_processor_batch_max_time)
	{
		auto log_this_record (false);
		if (node.config.logging.timing_logging ())
		{
			if (should_log (first_time))
			{
				log_this_record = true;
			}
		}
		else
		{
			if (((blocks.size () + state_blocks.size () + forced.size ()) > 64 && should_log (false)))
			{
				log_this_record = true;
			}
		}

		if (log_this_record)
		{
			first_time = false;
			BOOST_LOG (node.log) << boost::str (boost::format ("%1% blocks (+ %2% state blocks) (+ %3% forced) in processing queue") % blocks.size () % state_blocks.size () % forced.size ());
		}
		std::pair<std::shared_ptr<rai::block>, std::chrono::steady_clock::time_point> block;
		bool force (false);
		if (forced.empty ())
		{
			block = blocks.front ();
			blocks.pop_front ();
			blocks_hashes.erase (block.first->hash ());
		}
		else
		{
			block = std::make_pair (forced.front (), std::chrono::steady_clock::now ());
			forced.pop_front ();
			force = true;
			number_of_forced_processed++;
		}
		lock_a.unlock ();
		auto hash (block.first->hash ());
		if (force)
		{
			auto successor (node.ledger.successor (transaction, block.first->root ()));
			if (successor != nullptr && successor->hash () != hash)
			{
				// Replace our block with the winner and roll back any dependent blocks
				BOOST_LOG (node.log) << boost::str (boost::format ("Rolling back %1% and replacing with %2%") % successor->hash ().to_string () % hash.to_string ());
				node.ledger.rollback (transaction, successor->hash ());
			}
		}
		/* Forced state blocks are not validated in verify_state_blocks () function
		Because of that we should set set validated_state_block as "false" for forced state blocks (!force) */
		bool validated_state_block (!force && block.first->type () == rai::block_type::state);
		auto process_result (process_receive_one (transaction, block.first, block.second, validated_state_block));
		number_of_blocks_processed++;
		(void)process_result;
		lock_a.lock ();
		/* Verify more state blocks if blocks deque is empty
		Because verification is long process, avoid large deque verification inside of write transaction */
		if (blocks.empty () && !state_blocks.empty ())
		{
			verify_state_blocks (lock_a, 256);
		}
	}
	lock_a.unlock ();

	if (node.config.logging.timing_logging ())
	{
		auto end_time (std::chrono::steady_clock::now ());
		auto elapsed_time_ms (std::chrono::duration_cast<std::chrono::milliseconds> (end_time - start_time));
		auto elapsed_time_ms_int (elapsed_time_ms.count ());

		BOOST_LOG (node.log) << boost::str (boost::format ("Processed %1% blocks (%2% blocks were forced) in %3% milliseconds") % number_of_blocks_processed % number_of_forced_processed % elapsed_time_ms_int);
	}
}

rai::process_return rai::block_processor::process_receive_one (rai::transaction const & transaction_a, std::shared_ptr<rai::block> block_a, std::chrono::steady_clock::time_point origination, bool validated_state_block)
{
	rai::process_return result;
	auto hash (block_a->hash ());
	result = node.ledger.process (transaction_a, *block_a, validated_state_block);
	switch (result.code)
	{
		case rai::process_result::progress:
		{
			if (node.config.logging.ledger_logging ())
			{
				std::string block;
				block_a->serialize_json (block);
				BOOST_LOG (node.log) << boost::str (boost::format ("Processing block %1%: %2%") % hash.to_string () % block);
			}
			if (node.block_arrival.recent (hash))
			{
				node.active.start (block_a);
				if (node.config.enable_voting)
				{
					generator.add (hash);
				}
			}
			queue_unchecked (transaction_a, hash);
			break;
		}
		case rai::process_result::gap_previous:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Gap previous for: %1%") % hash.to_string ());
			}
			node.store.unchecked_put (transaction_a, block_a->previous (), block_a);
			node.gap_cache.add (transaction_a, block_a);
			break;
		}
		case rai::process_result::gap_source:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Gap source for: %1%") % hash.to_string ());
			}
			node.store.unchecked_put (transaction_a, node.ledger.block_source (transaction_a, *block_a), block_a);
			node.gap_cache.add (transaction_a, block_a);
			break;
		}
		case rai::process_result::old:
		{
			if (node.config.logging.ledger_duplicate_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Old for: %1%") % block_a->hash ().to_string ());
			}
			queue_unchecked (transaction_a, hash);
			node.active.update_difficulty (*block_a);
			break;
		}
		case rai::process_result::bad_signature:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Bad signature for: %1%") % hash.to_string ());
			}
			break;
		}
		case rai::process_result::negative_spend:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Negative spend for: %1%") % hash.to_string ());
			}
			break;
		}
		case rai::process_result::unreceivable:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Unreceivable for: %1%") % hash.to_string ());
			}
			break;
		}
		case rai::process_result::fork:
		{
			if (origination < std::chrono::steady_clock::now () - std::chrono::seconds (15))
			{
				// Only let the bootstrap attempt know about forked blocks that not originate recently.
				node.process_fork (transaction_a, block_a);
			}
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Fork for: %1% root: %2%") % hash.to_string () % block_a->root ().to_string ());
			}
			break;
		}
		case rai::process_result::opened_burn_account:
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("*** Rejecting open block for burn account ***: %1%") % hash.to_string ());
			break;
		}
		case rai::process_result::balance_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Balance mismatch for: %1%") % hash.to_string ());
			}
			break;
		}
		case rai::process_result::representative_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Representative mismatch for: %1%") % hash.to_string ());
			}
			break;
		}
		case rai::process_result::block_position:
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% cannot follow predecessor %2%") % hash.to_string () % block_a->previous ().to_string ());
			}
			break;
		}
	}
	return result;
}

void rai::block_processor::queue_unchecked (rai::transaction const & transaction_a, rai::block_hash const & hash_a)
{
	auto cached (node.store.unchecked_get (transaction_a, hash_a));
	for (auto i (cached.begin ()), n (cached.end ()); i != n; ++i)
	{
		node.store.unchecked_del (transaction_a, rai::unchecked_key (hash_a, (*i)->hash ()));
		add (*i, std::chrono::steady_clock::time_point ());
	}
	std::lock_guard<std::mutex> lock (node.gap_cache.mutex);
	node.gap_cache.blocks.get<1> ().erase (hash_a);
}

rai::node::node (rai::node_init & init_a, boost::asio::io_context & io_ctx_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, rai::alarm & alarm_a, rai::logging const & logging_a, rai::work_pool & work_a) :
node (init_a, io_ctx_a, application_path_a, alarm_a, rai::node_config (peering_port_a, logging_a), work_a)
{
}

rai::node::node (rai::node_init & init_a, boost::asio::io_context & io_ctx_a, boost::filesystem::path const & application_path_a, rai::alarm & alarm_a, rai::node_config const & config_a, rai::work_pool & work_a) :
io_ctx (io_ctx_a),
config (config_a),
alarm (alarm_a),
work (work_a),
store_impl (std::make_unique<rai::mdb_store> (init_a.block_store_init, application_path_a / "data.ldb", config_a.lmdb_max_dbs)),
store (*store_impl),
gap_cache (*this),
ledger (store, stats, config.epoch_block_link, config.epoch_block_signer),
active (*this),
network (*this, config.peering_port),
bootstrap_initiator (*this),
bootstrap (io_ctx_a, config.peering_port, *this),
peers (network.endpoint ()),
application_path (application_path_a),
wallets (init_a.block_store_init, *this),
port_mapping (*this),
vote_processor (*this),
warmed_up (0),
block_processor (*this),
block_processor_thread ([this]() {
	rai::thread_role::set (rai::thread_role::name::block_processing);
	this->block_processor.process_blocks ();
}),
online_reps (*this),
stats (config.stat_config),
vote_uniquer (block_uniquer)
{
	wallets.observer = [this](bool active) {
		observers.wallet.notify (active);
	};
	peers.peer_observer = [this](rai::endpoint const & endpoint_a) {
		observers.endpoint.notify (endpoint_a);
	};
	peers.disconnect_observer = [this]() {
		observers.disconnect.notify ();
	};
	if (!config.callback_address.empty ())
	{
		observers.blocks.add ([this](std::shared_ptr<rai::block> block_a, rai::account const & account_a, rai::amount const & amount_a, bool is_state_send_a) {
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
							for (auto i (i_a), n (boost::asio::ip::tcp::resolver::iterator{}); i != n; ++i)
							{
								auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->io_ctx));
								sock->async_connect (i->endpoint (), [node_l, target, body, sock, address, port](boost::system::error_code const & ec) {
									if (!ec)
									{
										auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
										req->method (boost::beast::http::verb::post);
										req->target (*target);
										req->version (11);
										req->insert (boost::beast::http::field::host, address);
										req->insert (boost::beast::http::field::content_type, "application/json");
										req->body () = *body;
										//req->prepare (*req);
										//boost::beast::http::prepare(req);
										req->prepare_payload ();
										boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req](boost::system::error_code const & ec, size_t bytes_transferred) {
											if (!ec)
											{
												auto sb (std::make_shared<boost::beast::flat_buffer> ());
												auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
												boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port](boost::system::error_code const & ec, size_t bytes_transferred) {
													if (!ec)
													{
														if (resp->result () == boost::beast::http::status::ok)
														{
															node_l->stats.inc (rai::stat::type::http_callback, rai::stat::detail::initiate, rai::stat::dir::out);
														}
														else
														{
															if (node_l->config.logging.callback_logging ())
															{
																BOOST_LOG (node_l->log) << boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ());
															}
															node_l->stats.inc (rai::stat::type::error, rai::stat::detail::http_callback, rai::stat::dir::out);
														}
													}
													else
													{
														if (node_l->config.logging.callback_logging ())
														{
															BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ());
														}
														node_l->stats.inc (rai::stat::type::error, rai::stat::detail::http_callback, rai::stat::dir::out);
													};
												});
											}
											else
											{
												if (node_l->config.logging.callback_logging ())
												{
													BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ());
												}
												node_l->stats.inc (rai::stat::type::error, rai::stat::detail::http_callback, rai::stat::dir::out);
											}
										});
									}
									else
									{
										if (node_l->config.logging.callback_logging ())
										{
											BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ());
										}
										node_l->stats.inc (rai::stat::type::error, rai::stat::detail::http_callback, rai::stat::dir::out);
									}
								});
							}
						}
						else
						{
							if (node_l->config.logging.callback_logging ())
							{
								BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ());
							}
							node_l->stats.inc (rai::stat::type::error, rai::stat::detail::http_callback, rai::stat::dir::out);
						}
					});
				});
			}
		});
	}
	observers.endpoint.add ([this](rai::endpoint const & endpoint_a) {
		this->network.send_keepalive (endpoint_a);
		rep_query (*this, endpoint_a);
	});
	observers.vote.add ([this](rai::transaction const & transaction, std::shared_ptr<rai::vote> vote_a, rai::endpoint const & endpoint_a) {
		assert (endpoint_a.address ().is_v6 ());
		this->gap_cache.vote (vote_a);
		this->online_reps.vote (vote_a);
		rai::uint128_t rep_weight;
		rai::uint128_t min_rep_weight;
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
	BOOST_LOG (log) << "Node starting, version: " << RAIBLOCKS_VERSION_MAJOR << "." << RAIBLOCKS_VERSION_MINOR;
	BOOST_LOG (log) << boost::str (boost::format ("Work pool running %1% threads") % work.threads.size ());
	if (!init_a.error ())
	{
		if (config.logging.node_lifetime_tracing ())
		{
			BOOST_LOG (log) << "Constructing node";
		}
		rai::genesis genesis;
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

		node_id = rai::keypair (store.get_node_id (transaction));
		BOOST_LOG (log) << "Node ID: " << node_id.pub.to_account ();
	}
	peers.online_weight_minimum = config.online_weight_minimum.number ();
	if (rai::rai_network == rai::rai_networks::rai_live_network || rai::rai_network == rai::rai_networks::rai_beta_network)
	{
		rai::bufferstream weight_stream ((const uint8_t *)rai_bootstrap_weights, rai_bootstrap_weights_size);
		rai::uint128_union block_height;
		if (!rai::read (weight_stream, block_height))
		{
			auto max_blocks = (uint64_t)block_height.number ();
			auto transaction (store.tx_begin_read ());
			if (ledger.store.block_count (transaction).sum () < max_blocks)
			{
				ledger.bootstrap_weight_max_blocks = max_blocks;
				while (true)
				{
					rai::account account;
					if (rai::read (weight_stream, account.bytes))
					{
						break;
					}
					rai::amount weight;
					if (rai::read (weight_stream, weight.bytes))
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

rai::node::~node ()
{
	if (config.logging.node_lifetime_tracing ())
	{
		BOOST_LOG (log) << "Destructing node";
	}
	stop ();
}

bool rai::node::copy_with_compaction (boost::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (boost::polymorphic_downcast<rai::mdb_store *> (store_impl.get ())->env.environment, destination_file.string ().c_str (), MDB_CP_COMPACT);
}

void rai::node::send_keepalive (rai::endpoint const & endpoint_a)
{
	network.send_keepalive (rai::map_endpoint_to_v6 (endpoint_a));
}

void rai::node::process_fork (rai::transaction const & transaction_a, std::shared_ptr<rai::block> block_a)
{
	auto root (block_a->root ());
	if (!store.block_exists (transaction_a, block_a->type (), block_a->hash ()) && store.root_exists (transaction_a, block_a->root ()))
	{
		std::shared_ptr<rai::block> ledger_block (ledger.forked_block (transaction_a, *block_a));
		if (ledger_block)
		{
			std::weak_ptr<rai::node> this_w (shared_from_this ());
			if (!active.start (ledger_block, [this_w, root](std::shared_ptr<rai::block>) {
				    if (auto this_l = this_w.lock ())
				    {
					    auto attempt (this_l->bootstrap_initiator.current_attempt ());
					    if (attempt && !attempt->lazy_mode)
					    {
						    auto transaction (this_l->store.tx_begin_read ());
						    auto account (this_l->ledger.store.frontier_get (transaction, root));
						    if (!account.is_zero ())
						    {
							    attempt->requeue_pull (rai::pull_info (account, root, root));
						    }
						    else if (this_l->ledger.store.account_exists (transaction, root))
						    {
							    attempt->requeue_pull (rai::pull_info (root, rai::block_hash (0), rai::block_hash (0)));
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

rai::gap_cache::gap_cache (rai::node & node_a) :
node (node_a)
{
}

void rai::gap_cache::add (rai::transaction const & transaction_a, std::shared_ptr<rai::block> block_a)
{
	auto hash (block_a->hash ());
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (blocks.get<1> ().find (hash));
	if (existing != blocks.get<1> ().end ())
	{
		blocks.get<1> ().modify (existing, [](rai::gap_information & info) {
			info.arrival = std::chrono::steady_clock::now ();
		});
	}
	else
	{
		blocks.insert ({ std::chrono::steady_clock::now (), hash, std::unordered_set<rai::account> () });
		if (blocks.size () > max)
		{
			blocks.get<0> ().erase (blocks.get<0> ().begin ());
		}
	}
}

void rai::gap_cache::vote (std::shared_ptr<rai::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto transaction (node.store.tx_begin_read ());
	for (auto hash : *vote_a)
	{
		auto existing (blocks.get<1> ().find (hash));
		if (existing != blocks.get<1> ().end ())
		{
			auto is_new (false);
			blocks.get<1> ().modify (existing, [&](rai::gap_information & info) { is_new = info.voters.insert (vote_a->account).second; });
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
					node.alarm.add (rai::rai_network == rai::rai_networks::rai_test_network ? now + std::chrono::milliseconds (5) : now + std::chrono::seconds (5), [node_l, hash]() {
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

rai::uint128_t rai::gap_cache::bootstrap_threshold (rai::transaction const & transaction_a)
{
	auto result ((node.online_reps.online_stake () / 256) * node.config.bootstrap_fraction_numerator);
	return result;
}

void rai::network::confirm_send (rai::confirm_ack const & confirm_a, std::shared_ptr<std::vector<uint8_t>> bytes_a, rai::endpoint const & endpoint_a)
{
	if (node.config.logging.network_publish_logging ())
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm_ack for block(s) %1%to %2% sequence %3%") % confirm_a.vote->hashes_string () % endpoint_a % std::to_string (confirm_a.vote->sequence));
	}
	std::weak_ptr<rai::node> node_w (node.shared ());
	node.network.send_buffer (bytes_a->data (), bytes_a->size (), endpoint_a, [bytes_a, node_w, endpoint_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (ec && node_l->config.logging.network_logging ())
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Error broadcasting confirm_ack to %1%: %2%") % endpoint_a % ec.message ());
			}
			else
			{
				node_l->stats.inc (rai::stat::type::message, rai::stat::detail::confirm_ack, rai::stat::dir::out);
			}
		}
	});
}

void rai::node::process_active (std::shared_ptr<rai::block> incoming)
{
	block_arrival.add (incoming->hash ());
	block_processor.add (incoming, std::chrono::steady_clock::now ());
}

rai::process_return rai::node::process (rai::block const & block_a)
{
	auto transaction (store.tx_begin_write ());
	auto result (ledger.process (transaction, block_a));
	return result;
}

void rai::node::start ()
{
	network.start ();
	ongoing_keepalive ();
	ongoing_syn_cookie_cleanup ();
	if (!flags.disable_legacy_bootstrap)
	{
		ongoing_bootstrap ();
	}
	ongoing_store_flush ();
	ongoing_rep_crawl ();
	ongoing_rep_calculation ();
	if (!flags.disable_bootstrap_listener)
	{
		bootstrap.start ();
	}
	backup_wallet ();
	search_pending ();
	online_reps.recalculate_stake ();
	port_mapping.start ();
	add_initial_peers ();
}

void rai::node::stop ()
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

void rai::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, rai::network::node_port);
	}
}

rai::block_hash rai::node::latest (rai::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

rai::uint128_t rai::node::balance (rai::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<rai::block> rai::node::block (rai::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_get (transaction, hash_a);
}

std::pair<rai::uint128_t, rai::uint128_t> rai::node::balance_pending (rai::account const & account_a)
{
	std::pair<rai::uint128_t, rai::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a);
	result.second = ledger.account_pending (transaction, account_a);
	return result;
}

rai::uint128_t rai::node::weight (rai::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.weight (transaction, account_a);
}

rai::account rai::node::representative (rai::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	rai::account_info info;
	rai::account result (0);
	if (!store.account_get (transaction, account_a, info))
	{
		result = info.rep_block;
	}
	return result;
}

void rai::node::ongoing_keepalive ()
{
	keepalive_preconfigured (config.preconfigured_peers);
	auto peers_l (peers.purge_list (std::chrono::steady_clock::now () - cutoff));
	for (auto i (peers_l.begin ()), j (peers_l.end ()); i != j && std::chrono::steady_clock::now () - i->last_attempt > period; ++i)
	{
		network.send_keepalive (i->endpoint);
	}
	std::weak_ptr<rai::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_keepalive ();
		}
	});
}

void rai::node::ongoing_syn_cookie_cleanup ()
{
	peers.purge_syn_cookies (std::chrono::steady_clock::now () - syn_cookie_cutoff);
	std::weak_ptr<rai::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + (syn_cookie_cutoff * 2), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_syn_cookie_cleanup ();
		}
	});
}

void rai::node::ongoing_rep_crawl ()
{
	auto now (std::chrono::steady_clock::now ());
	auto peers_l (peers.rep_crawl ());
	rep_query (*this, peers_l);
	if (network.on)
	{
		std::weak_ptr<rai::node> node_w (shared_from_this ());
		alarm.add (now + std::chrono::seconds (4), [node_w]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->ongoing_rep_crawl ();
			}
		});
	}
}

void rai::node::ongoing_rep_calculation ()
{
	auto now (std::chrono::steady_clock::now ());
	vote_processor.calculate_weights ();
	std::weak_ptr<rai::node> node_w (shared_from_this ());
	alarm.add (now + std::chrono::minutes (10), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_rep_calculation ();
		}
	});
}

void rai::node::ongoing_bootstrap ()
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
	std::weak_ptr<rai::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_bootstrap ();
		}
	});
}

void rai::node::ongoing_store_flush ()
{
	{
		auto transaction (store.tx_begin_write ());
		store.flush (transaction);
	}
	std::weak_ptr<rai::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_store_flush ();
		}
	});
}

void rai::node::backup_wallet ()
{
	auto transaction (store.tx_begin_read ());
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		boost::filesystem::create_directories (backup_path);
		rai::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + backup_interval, [this_l]() {
		this_l->backup_wallet ();
	});
}

void rai::node::search_pending ()
{
	wallets.search_pending_all ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + search_pending_interval, [this_l]() {
		this_l->search_pending ();
	});
}

int rai::node::price (rai::uint128_t const & balance_a, int amount_a)
{
	assert (balance_a >= amount_a * rai::Gxrb_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= rai::Gxrb_ratio;
		auto balance_scaled ((balance_l / rai::Mxrb_ratio).convert_to<double> ());
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
	distributed_work (std::shared_ptr<rai::node> const & node_a, rai::block_hash const & root_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a) :
	distributed_work (1, node_a, root_a, callback_a, difficulty_a)
	{
		assert (node_a != nullptr);
	}
	distributed_work (unsigned int backoff_a, std::shared_ptr<rai::node> const & node_a, rai::block_hash const & root_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a) :
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
					connection->socket.async_connect (rai::tcp_endpoint (host, service), [this_l, connection](boost::system::error_code const & ec) {
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
			if (!rai::from_string_hex (work_text, work))
			{
				if (!rai::work_validate (root, work))
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
					node->work.generate (root, [callback_l](boost::optional<uint64_t> const & work_a) {
						callback_l (work_a.value ());
					},
					difficulty);
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
					std::weak_ptr<rai::node> node_w (node);
					auto next_backoff (std::min (backoff * 2, (unsigned int)60 * 5));
					node->alarm.add (now + std::chrono::seconds (backoff), [ node_w, root_l, callback_l, next_backoff, difficulty = difficulty ] {
						if (auto node_l = node_w.lock ())
						{
							auto work_generation (std::make_shared<distributed_work> (next_backoff, node_l, root_l, callback_l, difficulty));
							work_generation->start ();
						}
					});
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
	std::shared_ptr<rai::node> node;
	rai::block_hash root;
	std::mutex mutex;
	std::map<boost::asio::ip::address, uint16_t> outstanding;
	std::vector<std::pair<std::string, uint16_t>> need_resolve;
	std::atomic_flag completed;
	uint64_t difficulty;
};
}

void rai::node::work_generate_blocking (rai::block & block_a, uint64_t difficulty_a)
{
	block_a.block_work_set (work_generate_blocking (block_a.root (), difficulty_a));
}

void rai::node::work_generate (rai::uint256_union const & hash_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a)
{
	auto work_generation (std::make_shared<distributed_work> (shared (), hash_a, callback_a, difficulty_a));
	work_generation->start ();
}

uint64_t rai::node::work_generate_blocking (rai::uint256_union const & hash_a, uint64_t difficulty_a)
{
	std::promise<uint64_t> promise;
	work_generate (hash_a, [&promise](uint64_t work_a) {
		promise.set_value (work_a);
	},
	difficulty_a);
	return promise.get_future ().get ();
}

void rai::node::add_initial_peers ()
{
}

void rai::node::block_confirm (std::shared_ptr<rai::block> block_a)
{
	active.start (block_a);
	network.broadcast_confirm_req (block_a);
}

rai::uint128_t rai::node::delta ()
{
	auto result ((online_reps.online_stake () / 100) * config.online_weight_quorum);
	return result;
}

namespace
{
class confirmed_visitor : public rai::block_visitor
{
public:
	confirmed_visitor (rai::transaction const & transaction_a, rai::node & node_a, std::shared_ptr<rai::block> block_a, rai::block_hash const & hash_a) :
	transaction (transaction_a),
	node (node_a),
	block (block_a),
	hash (hash_a)
	{
	}
	virtual ~confirmed_visitor () = default;
	void scan_receivable (rai::account const & account_a)
	{
		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
		{
			auto wallet (i->second);
			if (wallet->store.exists (transaction, account_a))
			{
				rai::account representative;
				rai::pending_info pending;
				representative = wallet->store.representative (transaction);
				auto error (node.store.pending_get (transaction, rai::pending_key (account_a, hash), pending));
				if (!error)
				{
					auto node_l (node.shared ());
					auto amount (pending.amount.number ());
					wallet->receive_async (block, representative, amount, [](std::shared_ptr<rai::block>) {});
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
	void state_block (rai::state_block const & block_a) override
	{
		scan_receivable (block_a.hashables.link);
	}
	void send_block (rai::send_block const & block_a) override
	{
		scan_receivable (block_a.hashables.destination);
	}
	void receive_block (rai::receive_block const &) override
	{
	}
	void open_block (rai::open_block const &) override
	{
	}
	void change_block (rai::change_block const &) override
	{
	}
	rai::transaction const & transaction;
	rai::node & node;
	std::shared_ptr<rai::block> block;
	rai::block_hash const & hash;
};
}

void rai::node::process_confirmed (std::shared_ptr<rai::block> block_a)
{
	auto hash (block_a->hash ());
	bool exists (ledger.block_exists (block_a->type (), hash));
	// Attempt to process confirmed block if it's not in ledger yet
	if (!exists)
	{
		auto transaction (store.tx_begin_write ());
		block_processor.process_receive_one (transaction, block_a);
		exists = store.block_exists (transaction, block_a->type (), hash);
	}
	if (exists)
	{
		auto transaction (store.tx_begin_read ());
		confirmed_visitor visitor (transaction, *this, block_a, hash);
		block_a->visit (visitor);
		auto account (ledger.account (transaction, hash));
		auto amount (ledger.amount (transaction, hash));
		bool is_state_send (false);
		rai::account pending_account (0);
		if (auto state = dynamic_cast<rai::state_block *> (block_a.get ()))
		{
			is_state_send = ledger.is_send (transaction, *state);
			pending_account = state->hashables.link;
		}
		if (auto send = dynamic_cast<rai::send_block *> (block_a.get ()))
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
}

void rai::node::process_message (rai::message & message_a, rai::endpoint const & sender_a)
{
	network_message_visitor visitor (*this, sender_a);
	message_a.visit (visitor);
}

rai::endpoint rai::network::endpoint ()
{
	boost::system::error_code ec;
	auto port (socket.local_endpoint (ec).port ());
	if (ec)
	{
		BOOST_LOG (node.log) << "Unable to retrieve port: " << ec.message ();
	}
	return rai::endpoint (boost::asio::ip::address_v6::loopback (), port);
}

bool rai::block_arrival::add (rai::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.insert (rai::block_arrival_info{ now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool rai::block_arrival::recent (rai::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.begin ()->arrival + arrival_time_min < now)
	{
		arrival.erase (arrival.begin ());
	}
	return arrival.get<1> ().find (hash_a) != arrival.get<1> ().end ();
}

rai::online_reps::online_reps (rai::node & node) :
node (node)
{
}

void rai::online_reps::vote (std::shared_ptr<rai::vote> const & vote_a)
{
	auto rep (vote_a->account);
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto transaction (node.store.tx_begin_read ());
	auto current (reps.begin ());
	while (current != reps.end () && current->last_heard + std::chrono::seconds (rai::node::cutoff) < now)
	{
		auto old_stake (online_stake_total);
		online_stake_total -= node.ledger.weight (transaction, current->representative);
		if (online_stake_total > old_stake)
		{
			// underflow
			online_stake_total = 0;
		}
		current = reps.erase (current);
	}
	auto rep_it (reps.get<1> ().find (rep));
	auto info (rai::rep_last_heard_info{ now, rep });
	if (rep_it == reps.get<1> ().end ())
	{
		auto old_stake (online_stake_total);
		online_stake_total += node.ledger.weight (transaction, rep);
		if (online_stake_total < old_stake)
		{
			// overflow
			online_stake_total = std::numeric_limits<rai::uint128_t>::max ();
		}
		reps.insert (info);
	}
	else
	{
		reps.get<1> ().replace (rep_it, info);
	}
}

void rai::online_reps::recalculate_stake ()
{
	std::lock_guard<std::mutex> lock (mutex);
	online_stake_total = 0;
	auto transaction (node.store.tx_begin_read ());
	for (auto it : reps)
	{
		online_stake_total += node.ledger.weight (transaction, it.representative);
	}
	auto now (std::chrono::steady_clock::now ());
	std::weak_ptr<rai::node> node_w (node.shared ());
	node.alarm.add (now + std::chrono::minutes (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->online_reps.recalculate_stake ();
		}
	});
}

rai::uint128_t rai::online_reps::online_stake ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return std::max (online_stake_total, node.config.online_weight_minimum.number ());
}

std::vector<rai::account> rai::online_reps::list ()
{
	std::vector<rai::account> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (reps.begin ()), n (reps.end ()); i != n; ++i)
	{
		result.push_back (i->representative);
	}
	return result;
}

namespace
{
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long address_a)
{
	return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}
}

bool rai::reserved_address (rai::endpoint const & endpoint_a, bool blacklist_loopback)
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
	else if (rai::rai_network == rai::rai_networks::rai_live_network)
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

void rai::network::send_buffer (uint8_t const * data_a, size_t size_a, rai::endpoint const & endpoint_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	std::unique_lock<std::mutex> lock (socket_mutex);
	if (node.config.logging.network_packet_logging ())
	{
		BOOST_LOG (node.log) << "Sending packet";
	}
	socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint_a, [this, callback_a](boost::system::error_code const & ec, size_t size_a) {
		callback_a (ec, size_a);
		this->node.stats.add (rai::stat::type::traffic, rai::stat::dir::out, size_a);
		if (ec == boost::system::errc::host_unreachable)
		{
			this->node.stats.inc (rai::stat::type::error, rai::stat::detail::unreachable_host, rai::stat::dir::out);
		}
		if (this->node.config.logging.network_packet_logging ())
		{
			BOOST_LOG (this->node.log) << "Packet send complete";
		}
	});
}

std::shared_ptr<rai::node> rai::node::shared ()
{
	return shared_from_this ();
}

rai::election_vote_result::election_vote_result () :
replay (false),
processed (false)
{
}

rai::election_vote_result::election_vote_result (bool replay_a, bool processed_a)
{
	replay = replay_a;
	processed = processed_a;
}

rai::election::election (rai::node & node_a, std::shared_ptr<rai::block> block_a, std::function<void(std::shared_ptr<rai::block>)> const & confirmation_action_a) :
confirmation_action (confirmation_action_a),
node (node_a),
root (block_a->root ()),
election_start (std::chrono::steady_clock::now ()),
status ({ block_a, 0 }),
confirmed (false),
stopped (false),
announcements (0)
{
	last_votes.insert (std::make_pair (rai::not_an_account, rai::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () }));
	blocks.insert (std::make_pair (block_a->hash (), block_a));
}

void rai::election::compute_rep_votes (rai::transaction const & transaction_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &transaction_a](rai::public_key const & pub_a, rai::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, status.winner));
			this->node.vote_processor.vote (vote, this->node.network.endpoint ());
		});
	}
}

void rai::election::confirm_once (rai::transaction const & transaction_a)
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
		confirm_back (transaction_a);
	}
}

void rai::election::confirm_back (rai::transaction const & transaction_a)
{
	std::vector<rai::block_hash> hashes = { status.winner->previous (), status.winner->source (), status.winner->link () };
	for (auto & hash : hashes)
	{
		if (!hash.is_zero () && !node.ledger.is_epoch_link (hash))
		{
			auto existing (node.active.blocks.find (hash));
			if (existing != node.active.blocks.end () && !existing->second->confirmed && !existing->second->stopped && existing->second->blocks.size () == 1)
			{
				existing->second->confirm_once (transaction_a);
			}
		}
	}
}

void rai::election::stop ()
{
	stopped = true;
}

bool rai::election::have_quorum (rai::tally_t const & tally_a, rai::uint128_t tally_sum)
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

rai::tally_t rai::election::tally (rai::transaction const & transaction_a)
{
	std::unordered_map<rai::block_hash, rai::uint128_t> block_weights;
	for (auto vote_info : last_votes)
	{
		block_weights[vote_info.second.hash] += node.ledger.weight (transaction_a, vote_info.first);
	}
	last_tally = block_weights;
	rai::tally_t result;
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

void rai::election::confirm_if_quorum (rai::transaction const & transaction_a)
{
	auto tally_l (tally (transaction_a));
	assert (tally_l.size () > 0);
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	status.tally = winner->first;
	rai::uint128_t sum (0);
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

void rai::election::log_votes (rai::tally_t const & tally_a)
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

rai::election_vote_result rai::election::vote (rai::account rep, uint64_t sequence, rai::block_hash block_hash)
{
	// see republish_vote documentation for an explanation of these rules
	auto transaction (node.store.tx_begin_read ());
	auto replay (false);
	auto supply (node.online_reps.online_stake ());
	auto weight (node.ledger.weight (transaction, rep));
	auto should_process (false);
	if (rai::rai_network == rai::rai_networks::rai_test_network || weight > supply / 1000) // 0.1% or above
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
	return rai::election_vote_result (replay, should_process);
}

bool rai::node::validate_block_by_previous (rai::transaction const & transaction, std::shared_ptr<rai::block> block_a)
{
	bool result (false);
	rai::account account;
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
	if (!result && block_a->type () == rai::block_type::state)
	{
		std::shared_ptr<rai::state_block> block_l (std::static_pointer_cast<rai::state_block> (block_a));
		rai::amount prev_balance (0);
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
	if (!result && (account.is_zero () || rai::validate_message (account, block_a->hash (), block_a->block_signature ())))
	{
		result = true;
	}
	return result;
}

bool rai::election::publish (std::shared_ptr<rai::block> block_a)
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

void rai::active_transactions::announce_votes (std::unique_lock<std::mutex> & lock_a)
{
	std::unordered_set<rai::block_hash> inactive;
	auto transaction (node.store.tx_begin_read ());
	unsigned unconfirmed_count (0);
	unsigned unconfirmed_announcements (0);
	std::deque<std::shared_ptr<rai::block>> rebroadcast_bundle;
	std::deque<std::pair<std::shared_ptr<rai::block>, std::shared_ptr<std::vector<rai::peer_information>>>> confirm_req_bundle;

	auto roots_size (roots.size ());
	for (auto i (roots.get<1> ().begin ()), n (roots.get<1> ().end ()); i != n; ++i)
	{
		lock_a.unlock ();
		auto election_l (i->election);
		if ((election_l->confirmed || election_l->stopped) && i->election->announcements >= announcement_min - 1)
		{
			if (election_l->confirmed)
			{
				confirmed.push_back (i->election->status);
				if (confirmed.size () > election_history_size)
				{
					confirmed.pop_front ();
				}
			}
			inactive.insert (election_l->root);
		}
		else
		{
			if (i->election->announcements > announcement_long)
			{
				++unconfirmed_count;
				unconfirmed_announcements += i->election->announcements;
				// Log votes for very long unconfirmed elections
				if (i->election->announcements % 50 == 1)
				{
					auto tally_l (election_l->tally (transaction));
					election_l->log_votes (tally_l);
				}
				/* Escalation for long unconfirmed elections
				Start new elections for previous block & source
				if there are less than 100 active elections */
				if (i->election->announcements % announcement_long == 1 && roots_size < 100 && rai::rai_network != rai::rai_networks::rai_test_network)
				{
					std::shared_ptr<rai::block> previous;
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
			if (i->election->announcements < announcement_long || i->election->announcements % announcement_long == 1)
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
					if (i->election->announcements != 0)
					{
						election_l->stop ();
					}
				}
			}
			if (i->election->announcements % 4 == 1)
			{
				auto reps (std::make_shared<std::vector<rai::peer_information>> (node.peers.representatives (std::numeric_limits<size_t>::max ())));
				std::unordered_set<rai::account> probable_reps;
				rai::uint128_t total_weight (0);
				for (auto j (reps->begin ()), m (reps->end ()); j != m;)
				{
					auto & rep_votes (i->election->last_votes);
					auto rep_acct (j->probable_rep_account);
					// Calculate if representative isn't recorded for several IP addresses
					if (probable_reps.find (rep_acct) == probable_reps.end ())
					{
						total_weight = total_weight + j->rep_weight.number ();
						probable_reps.insert (rep_acct);
					}
					if (rep_votes.find (rep_acct) != rep_votes.end ())
					{
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
					if (confirm_req_bundle.size () < max_broadcast_queue)
					{
						confirm_req_bundle.push_back (std::make_pair (i->election->status.winner, reps));
					}
				}
				else
				{
					// broadcast request to all peers
					confirm_req_bundle.push_back (std::make_pair (i->election->status.winner, std::make_shared<std::vector<rai::peer_information>> (node.peers.list_vector (100))));
				}
			}
		}
		++election_l->announcements;
		lock_a.lock ();
	}
	// Rebroadcast unconfirmed blocks
	if (!rebroadcast_bundle.empty ())
	{
		node.network.republish_block_batch (rebroadcast_bundle);
	}
	//confirm_req broadcast
	if (!confirm_req_bundle.empty ())
	{
		node.network.broadcast_confirm_req_batch (confirm_req_bundle);
	}
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

void rai::active_transactions::announce_loop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	started = true;

	lock.unlock ();
	condition.notify_all ();
	lock.lock ();

	while (!stopped)
	{
		announce_votes (lock);
		unsigned extra_delay (std::min (roots.size (), max_broadcast_queue) * node.network.broadcast_interval_ms * 2);
		condition.wait_for (lock, std::chrono::milliseconds (announce_interval_ms + extra_delay));
	}
}

void rai::active_transactions::stop ()
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

bool rai::active_transactions::start (std::shared_ptr<rai::block> block_a, std::function<void(std::shared_ptr<rai::block>)> const & confirmation_action_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return add (block_a, confirmation_action_a);
}

bool rai::active_transactions::add (std::shared_ptr<rai::block> block_a, std::function<void(std::shared_ptr<rai::block>)> const & confirmation_action_a)
{
	auto error (true);
	if (!stopped)
	{
		auto root (block_a->root ());
		auto existing (roots.find (root));
		if (existing == roots.end ())
		{
			auto election (std::make_shared<rai::election> (node, block_a, confirmation_action_a));
			uint64_t difficulty (0);
			auto error (rai::work_validate (*block_a, &difficulty));
			release_assert (!error);
			roots.insert (rai::conflict_info{ root, difficulty, election });
			blocks.insert (std::make_pair (block_a->hash (), election));
		}
		error = existing != roots.end ();
	}
	return error;
}

// Validate a vote and apply it to the current election if one exists
bool rai::active_transactions::vote (std::shared_ptr<rai::vote> vote_a, bool single_lock)
{
	std::shared_ptr<rai::election> election;
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
			rai::election_vote_result result;
			if (vote_block.which ())
			{
				auto block_hash (boost::get<rai::block_hash> (vote_block));
				auto existing (blocks.find (block_hash));
				if (existing != blocks.end ())
				{
					result = existing->second->vote (vote_a->account, vote_a->sequence, block_hash);
				}
			}
			else
			{
				auto block (boost::get<std::shared_ptr<rai::block>> (vote_block));
				auto existing (roots.find (block->root ()));
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

bool rai::active_transactions::active (rai::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.find (block_a.root ()) != roots.end ();
}

void rai::active_transactions::update_difficulty (rai::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (block_a.root ()));
	if (existing != roots.end ())
	{
		uint64_t difficulty;
		auto error (rai::work_validate (block_a, &difficulty));
		assert (!error);
		roots.modify (existing, [difficulty](rai::conflict_info & info_a) {
			info_a.difficulty = difficulty;
		});
	}
}

// List of active blocks in elections
std::deque<std::shared_ptr<rai::block>> rai::active_transactions::list_blocks (bool single_lock)
{
	std::deque<std::shared_ptr<rai::block>> result;
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

void rai::active_transactions::erase (rai::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	if (roots.find (block_a.root ()) != roots.end ())
	{
		roots.erase (block_a.root ());
		BOOST_LOG (node.log) << boost::str (boost::format ("Election erased for block block %1% root %2%") % block_a.hash ().to_string () % block_a.root ().to_string ());
	}
}

rai::active_transactions::active_transactions (rai::node & node_a) :
node (node_a),
started (false),
stopped (false),
thread ([this]() {
	rai::thread_role::set (rai::thread_role::name::announce_loop);
	announce_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

rai::active_transactions::~active_transactions ()
{
	stop ();
}

bool rai::active_transactions::publish (std::shared_ptr<rai::block> block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (block_a->root ()));
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

int rai::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version_get (transaction);
}

rai::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned service_threads_a)
{
	boost::thread::attributes attrs;
	rai::thread_attributes::set (attrs);
	for (auto i (0); i < service_threads_a; ++i)
	{
		threads.push_back (boost::thread (attrs, [&io_ctx_a]() {
			rai::thread_role::set (rai::thread_role::name::io);
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

rai::thread_runner::~thread_runner ()
{
	join ();
}

void rai::thread_runner::join ()
{
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

rai::inactive_node::inactive_node (boost::filesystem::path const & path) :
path (path),
io_context (std::make_shared<boost::asio::io_context> ()),
alarm (*io_context),
work (1, nullptr)
{
	boost::system::error_code error_chmod;

	/*
	 * @warning May throw a filesystem exception
	 */
	boost::filesystem::create_directories (path);
	rai::set_secure_perm_directory (path, error_chmod);
	logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
	logging.init (path);
	node = std::make_shared<rai::node> (init, *io_context, 24000, path, alarm, logging, work);
}

rai::inactive_node::~inactive_node ()
{
	node->stop ();
}

rai::udp_buffer::udp_buffer (rai::stat & stats, size_t size, size_t count) :
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
		*entry_data = { slab_data + i * size, 0, rai::endpoint () };
		free.push_back (entry_data);
	}
}
rai::udp_data * rai::udp_buffer::allocate ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && free.empty () && full.empty ())
	{
		stats.inc (rai::stat::type::udp, rai::stat::detail::blocking, rai::stat::dir::in);
		condition.wait (lock);
	}
	rai::udp_data * result (nullptr);
	if (!free.empty ())
	{
		result = free.front ();
		free.pop_front ();
	}
	if (result == nullptr)
	{
		result = full.front ();
		full.pop_front ();
		stats.inc (rai::stat::type::udp, rai::stat::detail::overflow, rai::stat::dir::in);
	}
	return result;
}
void rai::udp_buffer::enqueue (rai::udp_data * data_a)
{
	assert (data_a != nullptr);
	{
		std::lock_guard<std::mutex> lock (mutex);
		full.push_back (data_a);
	}
	condition.notify_one ();
}
rai::udp_data * rai::udp_buffer::dequeue ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && full.empty ())
	{
		condition.wait (lock);
	}
	rai::udp_data * result (nullptr);
	if (!full.empty ())
	{
		result = full.front ();
		full.pop_front ();
	}
	return result;
}
void rai::udp_buffer::release (rai::udp_data * data_a)
{
	assert (data_a != nullptr);
	{
		std::lock_guard<std::mutex> lock (mutex);
		free.push_back (data_a);
	}
	condition.notify_one ();
}
void rai::udp_buffer::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
}
