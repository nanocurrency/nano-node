#include <rai/node.hpp>

#include <unordered_set>
#include <memory>
#include <sstream>

#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <thread>

rai::message_parser::message_parser (rai::message_visitor & visitor_a, rai::work_pool & pool_a) :
visitor (visitor_a),
pool (pool_a),
error (false),
insufficient_work (false)
{
}

void rai::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
    error = false;
    rai::bufferstream header_stream (buffer_a, size_a);
    uint8_t version_max;
    uint8_t version_using;
    uint8_t version_min;
    rai::message_type type;
    std::bitset <16> extensions;
    if (!rai::message::read_header (header_stream, version_max, version_using, version_min, type, extensions))
    {
        switch (type)
        {
            case rai::message_type::keepalive:
            {
                deserialize_keepalive (buffer_a, size_a);
                break;
            }
            case rai::message_type::publish:
            {
                deserialize_publish (buffer_a, size_a);
                break;
            }
            case rai::message_type::confirm_req:
            {
                deserialize_confirm_req (buffer_a, size_a);
                break;
            }
            case rai::message_type::confirm_ack:
            {
                deserialize_confirm_ack (buffer_a, size_a);
                break;
            }
            default:
            {
                error = true;
                break;
            }
        }
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_keepalive (uint8_t const * buffer_a, size_t size_a)
{
    rai::keepalive incoming;
    rai::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        visitor.keepalive (incoming);
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_publish (uint8_t const * buffer_a, size_t size_a)
{
    rai::publish incoming;
    rai::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        if (!pool.work_validate (*incoming.block))
        {
            visitor.publish (incoming);
        }
        else
        {
            insufficient_work = true;
        }
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_confirm_req (uint8_t const * buffer_a, size_t size_a)
{
    rai::confirm_req incoming;
    rai::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        if (!pool.work_validate (*incoming.block))
        {
            visitor.confirm_req (incoming);
        }
        else
        {
            insufficient_work = true;
        }
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_confirm_ack (uint8_t const * buffer_a, size_t size_a)
{
	bool error_l;
    rai::bufferstream stream (buffer_a, size_a);
    rai::confirm_ack incoming (error_l, stream);
    if (!error_l && at_end (stream))
    {
        if (!pool.work_validate (*incoming.vote.block))
        {
            visitor.confirm_ack (incoming);
        }
        else
        {
            insufficient_work = true;
        }
    }
    else
    {
        error = true;
    }
}

bool rai::message_parser::at_end (rai::bufferstream & stream_a)
{
    uint8_t junk;
    auto end (rai::read (stream_a, junk));
    return end;
}

double constexpr rai::node::price_max;
double constexpr rai::node::free_cutoff;
std::chrono::seconds constexpr rai::node::period;
std::chrono::seconds constexpr rai::node::cutoff;
std::chrono::minutes constexpr rai::node::backup_interval;
// Cutoff for receiving a block if no forks are observed.
std::chrono::milliseconds const rai::confirm_wait = rai_network == rai_networks::rai_test_network ? std::chrono::milliseconds (0) : std::chrono::milliseconds (5000);

rai::network::network (boost::asio::io_service & service_a, uint16_t port, rai::node & node_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::address_v6::any (), port)),
service (service_a),
resolver (service_a),
node (node_a),
bad_sender_count (0),
on (true),
keepalive_count (0),
publish_count (0),
confirm_req_count (0),
confirm_ack_count (0),
insufficient_work_count (0),
error_count (0)
{
}

void rai::network::receive ()
{
    if (node.config.logging.network_packet_logging ())
    {
        BOOST_LOG (node.log) << "Receiving packet";
    }
    std::unique_lock <std::mutex> lock (socket_mutex);
    socket.async_receive_from (boost::asio::buffer (buffer.data (), buffer.size ()), remote,
        [this] (boost::system::error_code const & error, size_t size_a)
        {
            receive_action (error, size_a);
        });
}

void rai::network::stop ()
{
    on = false;
    socket.close ();
    resolver.cancel ();
}

void rai::network::send_keepalive (rai::endpoint const & endpoint_a)
{
    assert (endpoint_a.address ().is_v6 ());
    rai::keepalive message;
    node.peers.random_fill (message.peers);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (node.config.logging.network_keepalive_logging ())
    {
        BOOST_LOG (node.log) << boost::str (boost::format ("Keepalive req sent from %1% to %2%") % endpoint () % endpoint_a);
    }
    auto node_l (node.shared ());
    send_buffer (bytes->data (), bytes->size (), endpoint_a, 0, [bytes, node_l, endpoint_a] (boost::system::error_code const & ec, size_t)
        {
            if (node_l->config.logging.network_logging ())
            {
                if (ec)
                {
                    BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending keepalive from %1% to %2% %3%") % node_l->network.endpoint () % endpoint_a % ec.message ());
                }
            }
        });
}

void rai::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a), boost::asio::ip::resolver_query_base::all_matching | boost::asio::ip::resolver_query_base::v4_mapped), [node_l, address_a] (boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a)
	{
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator {}); i != n; ++i)
			{
				node_l->send_keepalive (i->endpoint ());
			}
		}
		else
		{
			BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving address: %1%, %2%") % address_a % ec.message ());
		}
	});
}

void rai::network::republish_block (std::unique_ptr <rai::block> block, size_t rebroadcast_a)
{
	auto hash (block->hash ());
    auto list (node.peers.list ());
	// If we're a representative, broadcast a signed confirm, otherwise an unsigned publish
    if (!confirm_broadcast (list, block->clone (), 0, rebroadcast_a))
    {
        rai::publish message (std::move (block));
        std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
        {
            rai::vectorstream stream (*bytes);
            message.serialize (stream);
        }
        auto node_l (node.shared ());
        for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
        {
			if (!node.peers.knows_about (i->endpoint, hash))
			{
				if (node.config.logging.network_publish_logging ())
				{
					BOOST_LOG (node.log) << boost::str (boost::format ("Publish %1% to %2%") % hash.to_string () % i->endpoint);
				}
				send_buffer (bytes->data (), bytes->size (), i->endpoint, rebroadcast_a, [bytes, node_l] (boost::system::error_code const & ec, size_t size)
					{
						if (node_l->config.logging.network_logging ())
						{
							if (ec)
							{
								BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending publish: %1% from %2%") % ec.message () % node_l->network.endpoint ());
							}
						}
					});
			}
        }
		BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% was published from %2%") % hash.to_string () % endpoint ());
    }
	else
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% was confirmed from %2%") % hash.to_string () % endpoint ());
	}
}

void rai::network::broadcast_confirm_req (rai::block const & block_a)
{
	auto list (node.peers.list ());
	for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
	{
		node.network.send_confirm_req (i->endpoint, block_a);
	}
}

void rai::network::send_confirm_req (boost::asio::ip::udp::endpoint const & endpoint_a, rai::block const & block)
{
    rai::confirm_req message (block.clone ());
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (node.config.logging.network_logging ())
    {
        BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm req to %1%") % endpoint_a);
    }
    auto node_l (node.shared ());
    send_buffer (bytes->data (), bytes->size (), endpoint_a, 0, [bytes, node_l] (boost::system::error_code const & ec, size_t size)
        {
            if (node_l->config.logging.network_logging ())
            {
                if (ec)
                {
                    BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending confirm request: %1%") % ec.message ());
                }
            }
        });
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
    void keepalive (rai::keepalive const & message_a) override
    {
        if (node.config.logging.network_keepalive_logging ())
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("Received keepalive message from %1%") % sender);
        }
        ++node.network.keepalive_count;
        node.peers.contacted (sender);
        node.network.merge_peers (message_a.peers);
    }
    void publish (rai::publish const & message_a) override
    {
        if (node.config.logging.network_message_logging ())
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("Received publish message from %1%") % sender);
        }
        ++node.network.publish_count;
        node.peers.contacted (sender);
        node.peers.insert (sender, message_a.block->hash ());
        node.process_receive_republish (message_a.block->clone (), 0);
    }
    void confirm_req (rai::confirm_req const & message_a) override
    {
        if (node.config.logging.network_message_logging ())
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("Received confirm_req message from %1%") % sender);
        }
        ++node.network.confirm_req_count;
        node.peers.contacted (sender);
        node.peers.insert (sender, message_a.block->hash ());
        node.process_receive_republish (message_a.block->clone (), 0);
		bool exists;
		{
			rai::transaction transaction (node.store.environment, nullptr, false);
			exists = node.store.block_exists (transaction, message_a.block->hash ());
		}
        if (exists)
        {
            node.process_confirmation (*message_a.block, sender);
        }
    }
    void confirm_ack (rai::confirm_ack const & message_a) override
    {
        if (node.config.logging.network_message_logging ())
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("Received confirm_ack message from %1%") % sender);
        }
        ++node.network.confirm_ack_count;
        node.peers.contacted (sender);
        node.peers.insert (sender, message_a.vote.block->hash ());
        node.process_receive_republish (message_a.vote.block->clone (), 0);
        node.vote (message_a.vote);
    }
    void bulk_pull (rai::bulk_pull const &) override
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
    rai::node & node;
    rai::endpoint sender;
};
}

void rai::network::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (!rai::reserved_address (remote) && remote != endpoint ())
        {
            network_message_visitor visitor (node, remote);
            rai::message_parser parser (visitor, node.work);
            parser.deserialize_buffer (buffer.data (), size_a);
            if (parser.error)
            {
                ++error_count;
            }
            else if (parser.insufficient_work)
            {
                if (node.config.logging.insufficient_work_logging ())
                {
                    BOOST_LOG (node.log) << "Insufficient work in message";
                }
                ++insufficient_work_count;
            }
        }
        else
        {
            if (node.config.logging.network_logging ())
            {
                BOOST_LOG (node.log) << "Reserved sender";
            }
            ++bad_sender_count;
        }
        receive ();
    }
    else
    {
        if (node.config.logging.network_logging ())
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("Receive error: %1%") % error.message ());
        }
        node.service.add (std::chrono::system_clock::now () + std::chrono::seconds (5), [this] () { receive (); });
    }
}

// Send keepalives to all the peers we've been notified of
void rai::network::merge_peers (std::array <rai::endpoint, 8> const & peers_a)
{
    for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
    {
        if (!node.peers.not_a_peer (*i) && !node.peers.known_peer (*i))
        {
            send_keepalive (*i);
        }
    }
}

rai::publish::publish () :
message (rai::message_type::publish)
{
}

rai::publish::publish (std::unique_ptr <rai::block> block_a) :
message (rai::message_type::publish),
block (std::move (block_a))
{
    block_type_set (block->type ());
}

bool rai::publish::deserialize (rai::stream & stream_a)
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
	assert (type == rai::message_type::publish);
    if (!result)
    {
        block = rai::deserialize_block (stream_a, block_type ());
        result = block == nullptr;
    }
    return result;
}

void rai::publish::serialize (rai::stream & stream_a)
{
    assert (block != nullptr);
	write_header (stream_a);
    block->serialize (stream_a);
}

void rai::processor_service::run ()
{
    std::unique_lock <std::mutex> lock (mutex);
    while (!done)
    {
        if (!operations.empty ())
        {
            auto & operation_l (operations.top ());
            if (operation_l.wakeup < std::chrono::system_clock::now ())
            {
                auto operation (operation_l);
                operations.pop ();
                lock.unlock ();
                operation.function ();
                lock.lock ();
            }
            else
            {
				auto wakeup (operation_l.wakeup);
                condition.wait_until (lock, wakeup);
            }
        }
        else
        {
            condition.wait (lock);
        }
    }
}

size_t rai::processor_service::poll_one ()
{
    std::unique_lock <std::mutex> lock (mutex);
    size_t result (0);
    if (!operations.empty ())
    {
        auto & operation_l (operations.top ());
        if (operation_l.wakeup < std::chrono::system_clock::now ())
        {
            auto operation (operation_l);
            operations.pop ();
            lock.unlock ();
            operation.function ();
            result = 1;
        }
    }
    return result;
}

size_t rai::processor_service::poll ()
{
    std::unique_lock <std::mutex> lock (mutex);
    size_t result (0);
    auto done_l (false);
    while (!done_l)
    {
        if (!operations.empty ())
        {
            auto & operation_l (operations.top ());
            if (operation_l.wakeup < std::chrono::system_clock::now ())
            {
                auto operation (operation_l);
                operations.pop ();
                lock.unlock ();
                operation.function ();
                ++result;
                lock.lock ();
            }
            else
            {
                done_l = true;
            }
        }
        else
        {
            done_l = true;
        }
    }
    return result;
}

void rai::processor_service::add (std::chrono::system_clock::time_point const & wakeup_a, std::function <void ()> const & operation)
{
    std::lock_guard <std::mutex> lock (mutex);
    if (!done)
    {
        operations.push (rai::operation ({wakeup_a, operation}));
        condition.notify_all ();
    }
}

rai::processor_service::processor_service () :
done (false)
{
}

void rai::processor_service::stop ()
{
    std::lock_guard <std::mutex> lock (mutex);
    done = true;
    while (!operations.empty ())
    {
        operations.pop ();
    }
    condition.notify_all ();
}

bool rai::operation::operator > (rai::operation const & other_a) const
{
    return wakeup > other_a.wakeup;
}

rai::logging::logging () :
ledger_logging_value (false),
ledger_duplicate_logging_value (false),
network_logging_value (true),
network_message_logging_value (false),
network_publish_logging_value (false),
network_packet_logging_value (false),
network_keepalive_logging_value (false),
node_lifetime_tracing_value (false),
insufficient_work_logging_value (true),
log_rpc_value (true),
bulk_pull_logging_value (false),
work_generation_time_value (true),
log_to_cerr_value (false)
{
}

void rai::logging::serialize_json (boost::property_tree::ptree & tree_a) const
{
	tree_a.put ("ledger", ledger_logging_value);
	tree_a.put ("ledger_duplicate", ledger_duplicate_logging_value);
	tree_a.put ("network", network_logging_value);
	tree_a.put ("network_message", network_message_logging_value);
	tree_a.put ("network_publish", network_publish_logging_value);
	tree_a.put ("network_packet", network_packet_logging_value);
	tree_a.put ("network_keepalive", network_keepalive_logging_value);
	tree_a.put ("node_lifetime_tracing", node_lifetime_tracing_value);
	tree_a.put ("insufficient_work", insufficient_work_logging_value);
	tree_a.put ("log_rpc", log_rpc_value);
	tree_a.put ("bulk_pull", bulk_pull_logging_value);
	tree_a.put ("work_generation_time", work_generation_time_value);
	tree_a.put ("log_to_cerr", log_to_cerr_value);
}

bool rai::logging::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		ledger_logging_value = tree_a.get <bool> ("ledger");
		ledger_duplicate_logging_value = tree_a.get <bool> ("ledger_duplicate");
		network_logging_value = tree_a.get <bool> ("network");
		network_message_logging_value = tree_a.get <bool> ("network_message");
		network_publish_logging_value = tree_a.get <bool> ("network_publish");
		network_packet_logging_value = tree_a.get <bool> ("network_packet");
		network_keepalive_logging_value = tree_a.get <bool> ("network_keepalive");
		node_lifetime_tracing_value = tree_a.get <bool> ("node_lifetime_tracing");
		insufficient_work_logging_value = tree_a.get <bool> ("insufficient_work");
		log_rpc_value = tree_a.get <bool> ("log_rpc");
		bulk_pull_logging_value = tree_a.get <bool> ("bulk_pull");
		work_generation_time_value = tree_a.get <bool> ("work_generation_time");
		log_to_cerr_value = tree_a.get <bool> ("log_to_cerr");
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

bool rai::logging::ledger_logging () const
{
	return ledger_logging_value;
}

bool rai::logging::ledger_duplicate_logging () const
{
	return ledger_logging () && ledger_duplicate_logging_value;
}
bool rai::logging::network_logging () const
{
	return network_logging_value;
}

bool rai::logging::network_message_logging () const
{
	return network_logging () && network_message_logging_value;
}

bool rai::logging::network_publish_logging () const
{
	return network_logging () && network_publish_logging_value;
}

bool rai::logging::network_packet_logging () const
{
	return network_logging () && network_packet_logging_value;
}

bool rai::logging::network_keepalive_logging () const
{
	return network_logging () && network_keepalive_logging_value;
}

bool rai::logging::node_lifetime_tracing () const
{
	return node_lifetime_tracing_value;
}

bool rai::logging::insufficient_work_logging () const
{
	return network_logging () && insufficient_work_logging_value;
}

bool rai::logging::log_rpc () const
{
	return network_logging () && log_rpc_value;
}

bool rai::logging::bulk_pull_logging () const
{
	return network_logging () && bulk_pull_logging_value;
}

bool rai::logging::work_generation_time () const
{
	return work_generation_time_value;
}

bool rai::logging::log_to_cerr () const
{
	return log_to_cerr_value;
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

namespace {
class send_visitor : public rai::block_visitor
{
public:
	send_visitor (rai::node & node_a) :
	node (node_a)
	{
	}
	void send_block (rai::send_block const & block_a)
	{
		auto receive (false);
		{
			rai::transaction transaction (node.store.environment, nullptr, false);
			for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n && receive == false; ++i)
			{
				auto & wallet (*i->second);
				if (wallet.store.find (transaction, block_a.hashables.destination) != wallet.store.end ())
				{
					receive = true;
				}
			}
		}
		if (receive)
		{
			if (node.config.logging.ledger_logging ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("Starting fast confirmation of block: %1%") % block_a.hash ().to_string ());
			}
			auto node_l (node.shared ());
			node.conflicts.start (block_a, [node_l] (rai::block & block_a)
			{
				node_l->process_confirmed (block_a);
			}, false);
			auto root (block_a.root ());
			std::shared_ptr <rai::block> block_l (block_a.clone ().release ());
			node.service.add (std::chrono::system_clock::now () + rai::confirm_wait, [node_l, root, block_l] ()
			{
				if (node_l->conflicts.no_conflict (root))
				{
					node_l->process_confirmed (*block_l);
				}
				else
				{
					if (node_l->config.logging.ledger_logging ())
					{
						BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to fast-confirm block: %1% because root: %2% is in conflict") % block_l->hash ().to_string () % root.to_string ());
					}
				}
			});
        }
	}
	void receive_block (rai::receive_block const &)
	{
	}
	void open_block (rai::open_block const &)
	{
	}
	void change_block (rai::change_block const &)
	{
	}
	rai::node & node;
};
}

rai::node_config::node_config () :
peering_port (rai::network::node_port),
packet_delay_microseconds (5000),
bootstrap_fraction_numerator (1),
creation_rebroadcast (2),
rebroadcast_delay (15),
receive_minimum (rai::Mrai_ratio)
{
	preconfigured_peers.push_back ("rai.raiblocks.net");
	preconfigured_representatives.push_back (rai::genesis_account);
}

rai::node_config::node_config (uint16_t peering_port_a, rai::logging const & logging_a) :
peering_port (peering_port_a),
logging (logging_a),
packet_delay_microseconds (5000),
bootstrap_fraction_numerator (1),
creation_rebroadcast (2),
rebroadcast_delay (15),
receive_minimum (rai::Mrai_ratio)
{
	preconfigured_representatives.push_back (rai::genesis_account);
}

void rai::node_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
	tree_a.put ("peering_port", std::to_string (peering_port));
	tree_a.put ("packet_delay_microseconds", std::to_string (packet_delay_microseconds));
	tree_a.put ("bootstrap_fraction_numerator", std::to_string (bootstrap_fraction_numerator));
	tree_a.put ("creation_rebroadcast", std::to_string (creation_rebroadcast));
	tree_a.put ("rebroadcast_delay", std::to_string (rebroadcast_delay));
	tree_a.put ("receive_minimum", receive_minimum.to_string_dec ());
	boost::property_tree::ptree logging_l;
	logging.serialize_json (logging_l);
	tree_a.add_child ("logging", logging_l);
	boost::property_tree::ptree preconfigured_peers_l;
	for (auto i (preconfigured_peers.begin ()), n (preconfigured_peers.end ()); i != n; ++i)
	{
		boost::property_tree::ptree entry;
		entry.put ("", *i);
		preconfigured_peers_l.push_back (std::make_pair ("", entry));
	}
	tree_a.add_child ("preconfigured_peers", preconfigured_peers_l);
	boost::property_tree::ptree preconfigured_representatives_l;
	for (auto i (preconfigured_representatives.begin ()), n (preconfigured_representatives.end ()); i != n; ++i)
	{
		boost::property_tree::ptree entry;
		entry.put ("", i->to_base58check ());
		preconfigured_representatives_l.push_back (std::make_pair ("", entry));
	}
	tree_a.add_child ("preconfigured_representatives", preconfigured_representatives_l);
}

bool rai::node_config::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		auto peering_port_l (tree_a.get <std::string> ("peering_port"));
		auto packet_delay_microseconds_l (tree_a.get <std::string> ("packet_delay_microseconds"));
		auto bootstrap_fraction_numerator_l (tree_a.get <std::string> ("bootstrap_fraction_numerator"));
		auto creation_rebroadcast_l (tree_a.get <std::string> ("creation_rebroadcast"));
		auto rebroadcast_delay_l (tree_a.get <std::string> ("rebroadcast_delay"));
		auto receive_minimum_l (tree_a.get <std::string> ("receive_minimum"));
		auto logging_l (tree_a.get_child ("logging"));
		auto preconfigured_peers_l (tree_a.get_child ("preconfigured_peers"));
		preconfigured_peers.clear ();
		for (auto i (preconfigured_peers_l.begin ()), n (preconfigured_peers_l.end ()); i != n; ++i)
		{
			auto bootstrap_peer (i->second.get <std::string> (""));
			preconfigured_peers.push_back (bootstrap_peer);
		}
		auto preconfigured_representatives_l (tree_a.get_child ("preconfigured_representatives"));
		preconfigured_representatives.clear ();
		for (auto i (preconfigured_representatives_l.begin ()), n (preconfigured_representatives_l.end ()); i != n; ++i)
		{
			rai::account representative (0);
			result = result || representative.decode_base58check (i->second.get <std::string> (""));
			preconfigured_representatives.push_back (representative);
		}
		try
		{
			peering_port = std::stoul (peering_port_l);
			packet_delay_microseconds = std::stoul (packet_delay_microseconds_l);
			bootstrap_fraction_numerator = std::stoul (bootstrap_fraction_numerator_l);
			creation_rebroadcast = std::stoul (creation_rebroadcast_l);
			rebroadcast_delay = std::stoul (rebroadcast_delay_l);
			result = result || creation_rebroadcast > 10;
			result = result || rebroadcast_delay > 300;
			result = result || peering_port > std::numeric_limits <uint16_t>::max ();
			result = result || logging.deserialize_json (logging_l);
			result = result || receive_minimum.decode_dec (receive_minimum_l);
		}
		catch (std::logic_error const &)
		{
			result = true;
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

rai::account rai::node_config::random_representative ()
{
	assert (preconfigured_representatives.size () > 0);
	size_t index (rai::random_pool.GenerateWord32 (0, preconfigured_representatives.size () - 1));
	auto result (preconfigured_representatives [index]);
	return result;
}

rai::node::node (rai::node_init & init_a, boost::shared_ptr <boost::asio::io_service> service_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, rai::processor_service & processor_a, rai::logging const & logging_a, rai::work_pool & work_a) :
node (init_a, service_a, application_path_a, processor_a, rai::node_config (peering_port_a, logging_a), work_a)
{
}

rai::node::node (rai::node_init & init_a, boost::shared_ptr <boost::asio::io_service> service_a, boost::filesystem::path const & application_path_a, rai::processor_service & processor_a, rai::node_config const & config_a, rai::work_pool & work_a) :
config (config_a),
service (processor_a),
work (work_a),
store (init_a.block_store_init, application_path_a / "data.ldb"),
gap_cache (*this),
ledger (store),
conflicts (*this),
wallets (init_a.block_store_init, *this),
network (*service_a, config.peering_port, *this),
bootstrap_initiator (*this),
bootstrap (*service_a, config.peering_port, *this),
peers (network.endpoint ()),
application_path (application_path_a)
{
	wallets.observer = [this] (rai::account const & account_a, bool active)
	{
		for (auto i: wallet_observers)
		{
			i (account_a, active);
		}
	};
	peers.peer_observer = [this] (rai::endpoint const & endpoint_a)
	{
		for (auto i: endpoint_observers)
		{
			i (endpoint_a);
		}
	};
	peers.disconnect_observer = [this] ()
	{
		for (auto i: disconnect_observers)
		{
			i ();
		}
	};
	endpoint_observers.push_back ([this] (rai::endpoint const & endpoint_a)
	{
		network.send_keepalive (endpoint_a);
		bootstrap_initiator.warmup (endpoint_a);
	});
    vote_observers.push_back ([this] (rai::vote const & vote_a)
    {
        conflicts.update (vote_a);
    });
    vote_observers.push_back ([this] (rai::vote const & vote_a)
    {
		rai::transaction transaction (store.environment, nullptr, false);
        gap_cache.vote (transaction, vote_a);
    });
    if (config.logging.log_to_cerr ())
    {
        boost::log::add_console_log (std::cerr, boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    }
    boost::log::add_common_attributes ();
    boost::log::add_file_log (boost::log::keywords::target = application_path_a / "log", boost::log::keywords::file_name = application_path_a / "log" / "log_%Y-%m-%d_%H-%M-%S.%N.log", boost::log::keywords::rotation_size = 4 * 1024 * 1024, boost::log::keywords::auto_flush = true, boost::log::keywords::scan_method = boost::log::sinks::file::scan_method::scan_matching, boost::log::keywords::max_size = 16 * 1024 * 1024, boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    BOOST_LOG (log) << "Node starting, version: " << RAIBLOCKS_VERSION_MAJOR << "." << RAIBLOCKS_VERSION_MINOR << "." << RAIBLOCKS_VERSION_PATCH;
	observers.push_back ([this] (rai::block const & block_a, rai::account const & account_a)
    {
		send_visitor visitor (*this);
		block_a.visit (visitor);
    });
    if (!init_a.error ())
    {
        if (config.logging.node_lifetime_tracing ())
        {
            std::cerr << "Constructing node\n";
        }
		rai::transaction transaction (store.environment, nullptr, true);
        if (store.latest_begin (transaction) == store.latest_end ())
        {
            // Store was empty meaning we just created it, add the genesis block
            rai::genesis genesis;
            genesis.initialize (transaction, store);
        }
    }
}

rai::node::~node ()
{
    if (config.logging.node_lifetime_tracing ())
    {
        std::cerr << "Destructing node\n";
    }
}

void rai::node::send_keepalive (rai::endpoint const & endpoint_a)
{
    auto endpoint_l (endpoint_a);
    if (endpoint_l.address ().is_v4 ())
    {
        endpoint_l = rai::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
    }
    assert (endpoint_l.address ().is_v6 ());
    network.send_keepalive (endpoint_l);
}

void rai::node::vote (rai::vote const & vote_a)
{
    for (auto & i: vote_observers)
    {
        i (vote_a);
    }
}

rai::gap_cache::gap_cache (rai::node & node_a) :
node (node_a)
{
}

void rai::gap_cache::add (rai::block const & block_a, rai::block_hash needed_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (blocks.find (needed_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [] (rai::gap_information & info) {info.arrival = std::chrono::system_clock::now ();});
    }
    else
    {
        auto hash (block_a.hash ());
		blocks.insert ({std::chrono::system_clock::now (), needed_a, hash, std::unique_ptr <rai::votes> (new rai::votes (hash)), block_a.clone ()});
        if (blocks.size () > max)
        {
            blocks.get <1> ().erase (blocks.get <1> ().begin ());
        }
    }
}

std::unique_ptr <rai::block> rai::gap_cache::get (rai::block_hash const & hash_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    std::unique_ptr <rai::block> result;
    auto existing (blocks.find (hash_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [&] (rai::gap_information & info) {result.swap (info.block);});
        blocks.erase (existing);
    }
    return result;
}

void rai::gap_cache::vote (MDB_txn * transaction_a, rai::vote const & vote_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto hash (vote_a.block->hash ());
    auto existing (blocks.get <2> ().find (hash));
    if (existing != blocks.get <2> ().end ())
    {
        auto changed (existing->votes->vote (vote_a));
        if (changed)
        {
            auto winner (node.ledger.winner (transaction_a, *existing->votes));
            if (winner.first > bootstrap_threshold (transaction_a))
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Initiating bootstrap for confirmed gap: %1%") % hash.to_string ());
                node.bootstrap_initiator.bootstrap_any ();
            }
        }
    }
}

rai::uint128_t rai::gap_cache::bootstrap_threshold (MDB_txn * transaction_a)
{
    auto result ((node.ledger.supply (transaction_a) / 256) * node.config.bootstrap_fraction_numerator);
	return result;
}

bool rai::network::confirm_broadcast (std::vector <rai::peer_information> & list_a, std::unique_ptr <rai::block> block_a, uint64_t sequence_a, size_t rebroadcast_a)
{
    bool result (false);
	node.wallets.foreach_representative ([&result, &block_a, &list_a, this, sequence_a, rebroadcast_a] (rai::public_key const & pub_a, rai::private_key const & prv_a)
	{
		auto hash (block_a->hash ());
		for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
		{
			if (!node.peers.knows_about (j->endpoint, hash))
			{
				confirm_block (prv_a, pub_a, block_a->clone (), sequence_a, j->endpoint, rebroadcast_a);
				result = true;
			}
		}
	});
    return result;
}

void rai::network::confirm_block (rai::private_key const & prv, rai::public_key const & pub, std::unique_ptr <rai::block> block_a, uint64_t sequence_a, rai::endpoint const & endpoint_a, size_t rebroadcast_a)
{
    rai::confirm_ack confirm (pub, prv, sequence_a, std::move (block_a));
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        confirm.serialize (stream);
    }
    if (node.config.logging.network_publish_logging ())
    {
        BOOST_LOG (node.log) << boost::str (boost::format ("Sending confirm_ack for block %1% to %2%") % confirm.vote.block->hash ().to_string () % endpoint_a);
    }
    auto node_l (node.shared ());
    node.network.send_buffer (bytes->data (), bytes->size (), endpoint_a, 0, [bytes, node_l, endpoint_a] (boost::system::error_code const & ec, size_t size_a)
        {
            if (node_l->config.logging.network_logging ())
            {
                if (ec)
                {
                    BOOST_LOG (node_l->log) << boost::str (boost::format ("Error broadcasting confirm_ack to %1%: %2%") % endpoint_a % ec.message ());
                }
            }
        });
}

void rai::node::process_receive_republish (std::unique_ptr <rai::block> incoming, size_t rebroadcast_a)
{
	assert (incoming != nullptr);
    std::unique_ptr <rai::block> block (std::move (incoming));
    do
    {
        auto hash (block->hash ());
        auto process_result (process_receive (*block));
        switch (process_result.code)
        {
            case rai::process_result::progress:
            {
                network.republish_block (std::move (block), rebroadcast_a);
                break;
            }
            default:
            {
                break;
            }
        }
        block = gap_cache.get (hash);
    }
    while (block != nullptr);
}

rai::process_return rai::node::process_receive (rai::block const & block_a)
{
	rai::process_return result;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		result = ledger.process (transaction, block_a);
	}
    switch (result.code)
    {
        case rai::process_result::progress:
        {
			call_observers (block_a, result.account);
            if (config.logging.ledger_logging ())
            {
                std::string block;
                block_a.serialize_json (block);
                BOOST_LOG (log) << boost::str (boost::format ("Processing block %1% %2%") % block_a.hash ().to_string () % block);
            }
            break;
        }
        case rai::process_result::gap_previous:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Gap previous for: %1%") % block_a.hash ().to_string ());
            }
            auto previous (block_a.previous ());
            gap_cache.add (block_a, previous);
            break;
        }
        case rai::process_result::gap_source:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Gap source for: %1%") % block_a.hash ().to_string ());
            }
            auto source (block_a.source ());
            gap_cache.add (block_a, source);
            break;
        }
        case rai::process_result::old:
        {
			{
				auto root (block_a.root ());
				auto hash (block_a.hash ());
				rai::transaction transaction (store.environment, nullptr, true);
				auto existing (store.block_get (transaction, hash));
				if (existing != nullptr)
				{
					// Replace block with one that has higher work value
					if (work.work_value (root, block_a.block_work ()) > work.work_value (root, existing->block_work ()))
					{
						store.block_put (transaction, hash, block_a);
					}
				}
				else
				{
					// Could have been rolled back, maybe
				}
			}
            if (config.logging.ledger_duplicate_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Old for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::bad_signature:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Bad signature for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::overspend:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Overspend for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::unreceivable:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Unreceivable for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::not_receive_from_send:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Not receive from spend for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::fork:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Fork for: %1%") % block_a.hash ().to_string ());
            }
			std::unique_ptr <rai::block> root;
			{
				rai::transaction transaction (store.environment, nullptr, false);
				root = ledger.successor (transaction, block_a.root ());
			}
			auto node_l (shared_from_this ());
			conflicts.start (*root, [node_l] (rai::block & block_a)
			{
				node_l->process_confirmed (block_a);
			}, false);
            break;
        }
        case rai::process_result::account_mismatch:
        {
            if (config.logging.ledger_logging ())
            {
                BOOST_LOG (log) << boost::str (boost::format ("Account mismatch for: %1%") % block_a.hash ().to_string ());
            }
        }
    }
    return result;
}

rai::process_return rai::node::process (rai::block const & block_a)
{
	rai::transaction transaction (store.environment, nullptr, true);
	auto result (ledger.process (transaction, block_a));
	return result;
}

std::vector <rai::peer_information> rai::peer_container::list ()
{
    std::vector <rai::peer_information> result;
    std::lock_guard <std::mutex> lock (mutex);
    result.reserve (peers.size ());
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        result.push_back (*i);
    }
    return result;
}

std::vector <rai::peer_information> rai::peer_container::bootstrap_candidates ()
{
    std::vector <rai::peer_information> result;
    std::lock_guard <std::mutex> lock (mutex);
	auto now (std::chrono::system_clock::now ());
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
		if (now - i->last_bootstrap_failure > std::chrono::minutes (15))
		{
			result.push_back (*i);
		}
    }
    return result;
}

void rai::publish::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.publish (*this);
}

rai::keepalive::keepalive () :
message (rai::message_type::keepalive)
{
    boost::asio::ip::udp::endpoint endpoint (boost::asio::ip::address_v6 {}, 0);
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
    {
        *i = endpoint;
    }
}

void rai::keepalive::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.keepalive (*this);
}

void rai::keepalive::serialize (rai::stream & stream_a)
{
    write_header (stream_a);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        assert (i->address ().is_v6 ());
        auto bytes (i->address ().to_v6 ().to_bytes ());
        write (stream_a, bytes);
        write (stream_a, i->port ());
    }
}

bool rai::keepalive::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
    assert (type == rai::message_type::keepalive);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        std::array <uint8_t, 16> address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = rai::endpoint (boost::asio::ip::address_v6 (address), port);
    }
    return result;
}

size_t rai::processor_service::size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return operations.size ();
}

rai::system::system (uint16_t port_a, size_t count_a) :
service (new boost::asio::io_service)
{
    nodes.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        rai::node_init init;
		rai::node_config config (port_a + i, logging);
        auto node (std::make_shared <rai::node> (init, service, rai::unique_path (), processor, config, work));
        assert (!init.error ());
        node->start ();
		rai::uint256_union wallet;
		rai::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
		node->wallets.create (wallet);
        nodes.push_back (node);
    }
    for (auto i (nodes.begin ()), j (nodes.begin () + 1), n (nodes.end ()); j != n; ++i, ++j)
    {
        auto starting1 ((*i)->peers.size ());
        auto new1 (starting1);
        auto starting2 ((*j)->peers.size ());
        auto new2 (starting2);
        (*j)->network.send_keepalive ((*i)->network.endpoint ());
        do {
            poll ();
            new1 = (*i)->peers.size ();
            new2 = (*j)->peers.size ();
        } while (new1 == starting1 || new2 == starting2);
    }
	auto iterations1 (0);
	while (std::any_of (nodes.begin (), nodes.end (), [] (std::shared_ptr <rai::node> const & node_a) {return node_a->bootstrap_initiator.in_progress;}))
	{
		poll ();
		++iterations1;
		assert (iterations1 < 1000);
	}
}

rai::system::~system ()
{
    for (auto & i: nodes)
    {
        i->stop ();
    }
}

std::shared_ptr <rai::wallet> rai::system::wallet (size_t index_a)
{
    assert (nodes.size () > index_a);
	auto size (nodes [index_a]->wallets.items.size ());
    assert (size == 1);
    return nodes [index_a]->wallets.items.begin ()->second;
}

rai::account rai::system::account (MDB_txn * transaction_a, size_t index_a)
{
    auto wallet_l (wallet (index_a));
    auto keys (wallet_l->store.begin (transaction_a));
    assert (keys != wallet_l->store.end ());
    auto result (keys->first);
    assert (++keys == wallet_l->store.end ());
    return result;
}

void rai::system::poll ()
{
	auto polled1 (service->poll_one ());
	auto polled2 (processor.poll_one ());
	if (polled1 == 0 && polled2 == 0)
	{
		std::this_thread::sleep_for (std::chrono::milliseconds (50));
	}
}

void rai::node::process_confirmation (rai::block const & block_a, rai::endpoint const & sender)
{
	wallets.foreach_representative ([this, &block_a, &sender] (rai::public_key const & pub_a, rai::private_key const & prv_a)
	{
		if (config.logging.network_message_logging ())
		{
			BOOST_LOG (log) << boost::str (boost::format ("Sending confirm ack to: %1%") % sender);
		}
		network.confirm_block (prv_a, pub_a, block_a.clone (), 0, sender, 0);
	});
}

rai::confirm_ack::confirm_ack (bool & error_a, rai::stream & stream_a) :
message (error_a, stream_a),
vote (error_a, stream_a, block_type ())
{
}

rai::confirm_ack::confirm_ack (rai::account const & account_a, rai::private_key const & prv_a, uint64_t sequence_a, std::unique_ptr <rai::block> block_a) :
message (rai::message_type::confirm_ack),
vote (account_a, prv_a, sequence_a, std::move (block_a))
{
    block_type_set (vote.block->type ());
}

bool rai::confirm_ack::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
    assert (type == rai::message_type::confirm_ack);
    if (!result)
    {
        result = read (stream_a, vote.account);
        if (!result)
        {
            result = read (stream_a, vote.signature);
            if (!result)
            {
                result = read (stream_a, vote.sequence);
                if (!result)
                {
                    vote.block = rai::deserialize_block (stream_a, block_type ());
                    result = vote.block == nullptr;
                }
            }
        }
    }
    return result;
}

void rai::confirm_ack::serialize (rai::stream & stream_a)
{
    assert (block_type () == rai::block_type::send || block_type () == rai::block_type::receive || block_type () == rai::block_type::open || block_type () == rai::block_type::change);
	write_header (stream_a);
    write (stream_a, vote.account);
    write (stream_a, vote.signature);
    write (stream_a, vote.sequence);
    vote.block->serialize (stream_a);
}

bool rai::confirm_ack::operator == (rai::confirm_ack const & other_a) const
{
    auto result (vote.account == other_a.vote.account && *vote.block == *other_a.vote.block && vote.signature == other_a.vote.signature && vote.sequence == other_a.vote.sequence);
    return result;
}

void rai::confirm_ack::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_ack (*this);
}

rai::confirm_req::confirm_req () :
message (rai::message_type::confirm_req)
{
}

rai::confirm_req::confirm_req (std::unique_ptr <rai::block> block_a) :
message (rai::message_type::confirm_req),
block (std::move (block_a))
{
    block_type_set (block->type ());
}

bool rai::confirm_req::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
    assert (type == rai::message_type::confirm_req);
    if (!result)
	{
        block = rai::deserialize_block (stream_a, block_type ());
        result = block == nullptr;
    }
    return result;
}

void rai::confirm_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_req (*this);
}

void rai::confirm_req::serialize (rai::stream & stream_a)
{
    assert (block != nullptr);
	write_header (stream_a);
    block->serialize (stream_a);
}

namespace
{
class rollback_visitor : public rai::block_visitor
{
public:
    rollback_visitor (rai::ledger & ledger_a) :
    ledger (ledger_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
		auto hash (block_a.hash ());
        rai::receivable receivable;
		rai::transaction transaction (ledger.store.environment, nullptr, true);
		while (ledger.store.pending_get (transaction, hash, receivable))
		{
			ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination));
		}
        rai::account_info info;
        ledger.store.account_get (transaction, receivable.source, info);
		ledger.store.pending_del (transaction, hash);
        ledger.change_latest (transaction, receivable.source, block_a.hashables.previous, info.rep_block, ledger.balance (transaction, block_a.hashables.previous));
		ledger.store.block_del (transaction, hash);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
		rai::transaction transaction (ledger.store.environment, nullptr, true);
		auto hash (block_a.hash ());
        auto representative (ledger.representative (transaction, block_a.hashables.source));
        auto amount (ledger.amount (transaction, block_a.hashables.source));
        auto destination_account (ledger.account (transaction, hash));
		ledger.move_representation (transaction, ledger.representative (transaction, hash), representative, amount);
        ledger.change_latest (transaction, destination_account, block_a.hashables.previous, representative, ledger.balance (transaction, block_a.hashables.previous));
		ledger.store.block_del (transaction, hash);
        ledger.store.pending_put (transaction, block_a.hashables.source, {ledger.account (transaction, block_a.hashables.source), amount, destination_account});
    }
    void open_block (rai::open_block const & block_a) override
    {
		rai::transaction transaction (ledger.store.environment, nullptr, true);
		auto hash (block_a.hash ());
        auto representative (ledger.representative (transaction, block_a.hashables.source));
        auto amount (ledger.amount (transaction, block_a.hashables.source));
        auto destination_account (ledger.account (transaction, hash));
		ledger.move_representation (transaction, ledger.representative (transaction, hash), representative, amount);
        ledger.change_latest (transaction, destination_account, 0, representative, 0);
		ledger.store.block_del (transaction, hash);
        ledger.store.pending_put (transaction, block_a.hashables.source, {ledger.account (transaction, block_a.hashables.source), amount, destination_account});
    }
    void change_block (rai::change_block const & block_a) override
    {
		rai::transaction transaction (ledger.store.environment, nullptr, true);
        auto representative (ledger.representative (transaction, block_a.hashables.previous));
        auto account (ledger.account (transaction, block_a.hashables.previous));
        rai::account_info info;
        ledger.store.account_get (transaction, account, info);
		ledger.move_representation (transaction, block_a.representative (), representative, ledger.balance (transaction, block_a.hashables.previous));
		ledger.store.block_del (transaction, block_a.hash ());
        ledger.change_latest (transaction, account, block_a.hashables.previous, representative, info.balance);
    }
    rai::ledger & ledger;
};
}

bool rai::parse_port (std::string const & string_a, uint16_t & port_a)
{
	bool result;
	size_t converted;
	port_a = std::stoul (string_a, &converted);
	result = converted != string_a.size () || converted > std::numeric_limits <uint16_t>::max ();
	return result;
}
bool rai::parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
    auto result (false);
    auto port_position (string.rfind (':'));
    if (port_position != std::string::npos && port_position > 0)
    {
        std::string port_string (string.substr (port_position + 1));
        try
        {
			uint16_t port;
			result = parse_port (port_string, port);
            if (!result)
            {
                boost::system::error_code ec;
                auto address (boost::asio::ip::address_v6::from_string (string.substr (0, port_position), ec));
                if (ec == 0)
                {
                    address_a = address;
                    port_a = port;
                }
                else
                {
                    result = true;
                }
            }
            else
            {
                result = true;
            }
        }
        catch (...)
        {
            result = true;
        }
    }
    else
    {
        result = true;
    }
    return result;
}

bool rai::parse_endpoint (std::string const & string, rai::endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = rai::endpoint (address, port);
    }
    return result;
}

bool rai::parse_tcp_endpoint (std::string const & string, rai::tcp_endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = rai::tcp_endpoint (address, port);
    }
    return result;
}

rai::bulk_pull::bulk_pull () :
message (rai::message_type::bulk_pull)
{
}

void rai::bulk_pull::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.bulk_pull (*this);
}

bool rai::bulk_pull::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::bulk_pull == type);
    if (!result)
    {
        assert (type == rai::message_type::bulk_pull);
        result = read (stream_a, start);
        if (!result)
        {
            result = read (stream_a, end);
        }
    }
    return result;
}

void rai::bulk_pull::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
    write (stream_a, start);
    write (stream_a, end);
}

rai::bulk_push::bulk_push () :
message (rai::message_type::bulk_push)
{
}

bool rai::bulk_push::deserialize (rai::stream & stream_a)
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
    assert (rai::message_type::bulk_push == type);
    return result;
}

void rai::bulk_push::serialize (rai::stream & stream_a)
{
    write_header (stream_a);
}

void rai::bulk_push::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.bulk_push (*this);
}

void rai::node::start ()
{
    network.receive ();
    ongoing_keepalive ();
    bootstrap.start ();
	backup_wallet ();
}

void rai::node::stop ()
{
    BOOST_LOG (log) << "Node stopping";
	conflicts.roots.clear ();
    network.stop ();
    bootstrap.stop ();
    service.stop ();
}

void rai::node::keepalive_preconfigured (std::vector <std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, rai::network::node_port);
	}
}

rai::block_hash rai::node::latest (rai::account const & account_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	return ledger.latest (transaction, account_a);
}

rai::uint128_t rai::node::balance (rai::account const & account_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	return ledger.account_balance (transaction, account_a);
}

rai::uint128_t rai::node::weight (rai::account const & account_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	return ledger.weight (transaction, account_a);
}

rai::account rai::node::representative (rai::account const & account_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	rai::account_info info;
	rai::account result (0);
	if (!store.account_get (transaction, account_a, info))
	{
		result = info.rep_block;
	}
	return result;
}

void rai::node::call_observers (rai::block const & block_a, rai::account const & account_a)
{
	for (auto & i: observers)
	{
		i (block_a, account_a);
	}
}

void rai::node::ongoing_keepalive ()
{
    keepalive_preconfigured (config.preconfigured_peers);
    auto peers_l (peers.purge_list (std::chrono::system_clock::now () - cutoff));
    for (auto i (peers_l.begin ()), j (peers_l.end ()); i != j && std::chrono::system_clock::now () - i->last_attempt > period; ++i)
    {
        network.send_keepalive (i->endpoint);
    }
	auto node_l (shared_from_this ());
    service.add (std::chrono::system_clock::now () + period, [node_l] () { node_l->ongoing_keepalive ();});
}

void rai::node::backup_wallet ()
{
	rai::transaction transaction (store.environment, nullptr, false);
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		auto backup_path (application_path / "backup");
		boost::filesystem::create_directories (backup_path);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	service.add (std::chrono::system_clock::now () + backup_interval, [this_l] ()
	{
		this_l->backup_wallet ();
	});
}

int rai::node::price (rai::uint128_t const & balance_a, int amount_a)
{
	assert (balance_a >= amount_a * rai::Grai_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= rai::Grai_ratio;
		auto balance_scaled ((balance_l / rai::Mrai_ratio).convert_to <double> ());
		auto units (balance_scaled / 1000.0);
		auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
		result += std::min (std::max (0.0, unit_price), price_max);
	}
	return static_cast <int> (result * 100.0);
}

namespace
{
class confirmed_visitor : public rai::block_visitor
{
public:
    confirmed_visitor (rai::node & node_a) :
    node (node_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
        {
			auto wallet (i->second);
			if (wallet->exists (block_a.hashables.destination))
			{
				rai::private_key prv;
				rai::account representative;
				rai::receivable receivable;
				auto error (false);
				{
					rai::transaction transaction (node.store.environment, nullptr, false);
					error = wallet->store.fetch (transaction, block_a.hashables.destination, prv);
					error = error | node.store.pending_get (transaction, block_a.hash (), receivable);
					representative = wallet->store.representative (transaction);
				}
				if (!error)
				{
					auto block_l (std::shared_ptr <rai::send_block> (static_cast <rai::send_block *> (block_a.clone ().release ())));
					auto node_l (node.shared ());
					auto amount (receivable.amount.number ());
					node.service.add (std::chrono::system_clock::now (), [block_l, prv, representative, wallet, node_l, amount] ()
					{
						node_l->wallets.queue_wallet_action (block_l->hashables.destination, amount, [block_l, prv, representative, wallet, amount] ()
						{
							auto error (wallet->receive_action (*block_l, prv, representative, amount));
							(void)error; // Might be interesting to view during debug
						});
					});
				}
				else
				{
					BOOST_LOG (node.log) << "While confirming, unable to fetch wallet key";
				}
			}
        }
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
    rai::node & node;
};
}

void rai::node::process_confirmed (rai::block const & confirmed_a)
{
    confirmed_visitor visitor (*this);
    confirmed_a.visit (visitor);
}

void rai::node::process_message (rai::message & message_a, rai::endpoint const & sender_a)
{
	network_message_visitor visitor (*this, sender_a);
	message_a.visit (visitor);
}

rai::bootstrap_initiator::bootstrap_initiator (rai::node & node_a) :
node (node_a),
in_progress (false),
warmed_up (false)
{
}

void rai::bootstrap_initiator::warmup (rai::endpoint const & endpoint_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	if (!in_progress && warmed_up.size () < 2 && !in_progress && warmed_up.find (endpoint_a) == warmed_up.end ())
	{
		warmed_up.insert (endpoint_a);
		in_progress = true;
		notify_listeners ();
		initiate (endpoint_a);
	}
}

void rai::bootstrap_initiator::bootstrap (rai::endpoint const & endpoint_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	if (!in_progress)
	{
		in_progress = true;
		initiate (endpoint_a);
	}
}

void rai::bootstrap_initiator::bootstrap_any ()
{
    auto list (node.peers.bootstrap_candidates ());
    if (!list.empty ())
    {
        bootstrap (list [random_pool.GenerateWord32 (0, list.size () - 1)].endpoint);
    }
}

void rai::bootstrap_initiator::initiate (rai::endpoint const & endpoint_a)
{
	auto node_l (node.shared ());
    auto processor (std::make_shared <rai::bootstrap_client> (node_l, [node_l] ()
	{
		std::lock_guard <std::mutex> lock (node_l->bootstrap_initiator.mutex);
		node_l->bootstrap_initiator.in_progress = false;
		node_l->bootstrap_initiator.notify_listeners ();
	}));
    processor->run (rai::tcp_endpoint (endpoint_a.address (), endpoint_a.port ()));
}

void rai::bootstrap_initiator::notify_listeners ()
{
	for (auto & i: observers)
	{
		i (in_progress);
	}
}

rai::bootstrap_listener::bootstrap_listener (boost::asio::io_service & service_a, uint16_t port_a, rai::node & node_a) :
acceptor (service_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::any (), port_a)),
service (service_a),
node (node_a)
{
}

void rai::bootstrap_listener::start ()
{
    acceptor.open (local.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (local);
    acceptor.listen ();
    accept_connection ();
}

void rai::bootstrap_listener::stop ()
{
    on = false;
    acceptor.close ();
}

void rai::bootstrap_listener::accept_connection ()
{
    auto socket (std::make_shared <boost::asio::ip::tcp::socket> (service));
    acceptor.async_accept (*socket, [this, socket] (boost::system::error_code const & ec)
    {
        accept_action (ec, socket);
    });
}

void rai::bootstrap_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr <boost::asio::ip::tcp::socket> socket_a)
{
    if (!ec)
    {
        accept_connection ();
        auto connection (std::make_shared <rai::bootstrap_server> (socket_a, node.shared ()));
        connection->receive ();
    }
    else
    {
        BOOST_LOG (node.log) << boost::str (boost::format ("Error while accepting bootstrap connections: %1%") % ec.message ());
    }
}

rai::bootstrap_server::bootstrap_server (std::shared_ptr <boost::asio::ip::tcp::socket> socket_a, std::shared_ptr <rai::node> node_a) :
socket (socket_a),
node (node_a)
{
}

void rai::bootstrap_server::receive ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), 8), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->receive_header_action (ec, size_a);
    });
}

void rai::bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 8);
		rai::bufferstream type_stream (receive_buffer.data (), size_a);
		uint8_t version_max;
		uint8_t version_using;
		uint8_t version_min;
		rai::message_type type;
		std::bitset <16> extensions;
		if (!rai::message::read_header (type_stream, version_max, version_using, version_min, type, extensions))
		{
			switch (type)
			{
				case rai::message_type::bulk_pull:
				{
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a)
					{
						this_l->receive_bulk_pull_action (ec, size_a);
					});
					break;
				}
				case rai::message_type::frontier_req:
				{
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t)), [this_l] (boost::system::error_code const & ec, size_t size_a)
					{
						this_l->receive_frontier_req_action (ec, size_a);
					});
					break;
				}
                case rai::message_type::bulk_push:
                {
                    add_request (std::unique_ptr <rai::message> (new rai::bulk_push));
                    break;
                }
				default:
				{
					if (node->config.logging.network_logging ())
					{
						BOOST_LOG (node->log) << boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast <uint8_t> (type));
					}
					break;
				}
			}
		}
    }
    else
    {
        if (node->config.logging.network_logging ())
        {
            BOOST_LOG (node->log) << boost::str (boost::format ("Error while receiving type %1%") % ec.message ());
        }
    }
}

void rai::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        std::unique_ptr <rai::bulk_pull> request (new rai::bulk_pull);
        rai::bufferstream stream (receive_buffer.data (), 8 + sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        auto error (request->deserialize (stream));
        if (!error)
        {
            if (node->config.logging.network_logging ())
            {
                BOOST_LOG (node->log) << boost::str (boost::format ("Received bulk pull for %1% down to %2%") % request->start.to_string () % request->end.to_string ());
            }
			add_request (std::unique_ptr <rai::message> (request.release ()));
            receive ();
        }
    }
}

void rai::bootstrap_server::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
		rai::bufferstream stream (receive_buffer.data (), 8 + sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			if (node->config.logging.network_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age);
			}
			add_request (std::unique_ptr <rai::message> (request.release ()));
			receive ();
		}
	}
    else
    {
        if (node->config.logging.network_logging ())
        {
            BOOST_LOG (node->log) << boost::str (boost::format ("Error sending receiving frontier request %1%") % ec.message ());
        }
    }
}

void rai::bootstrap_server::add_request (std::unique_ptr <rai::message> message_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next ();
	}
}

void rai::bootstrap_server::finish_request ()
{
	std::lock_guard <std::mutex> lock (mutex);
	requests.pop ();
	if (!requests.empty ())
	{
		run_next ();
	}
}

namespace
{
class request_response_visitor : public rai::message_visitor
{
public:
    request_response_visitor (std::shared_ptr <rai::bootstrap_server> connection_a) :
    connection (connection_a)
    {
    }
    void keepalive (rai::keepalive const &) override
    {
        assert (false);
    }
    void publish (rai::publish const &) override
    {
        assert (false);
    }
    void confirm_req (rai::confirm_req const &) override
    {
        assert (false);
    }
    void confirm_ack (rai::confirm_ack const &) override
    {
        assert (false);
    }
    void bulk_pull (rai::bulk_pull const &) override
    {
        auto response (std::make_shared <rai::bulk_pull_server> (connection, std::unique_ptr <rai::bulk_pull> (static_cast <rai::bulk_pull *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    void bulk_push (rai::bulk_push const &) override
    {
        auto response (std::make_shared <rai::bulk_push_server> (connection));
        response->receive ();
    }
    void frontier_req (rai::frontier_req const &) override
    {
        auto response (std::make_shared <rai::frontier_req_server> (connection, std::unique_ptr <rai::frontier_req> (static_cast <rai::frontier_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    std::shared_ptr <rai::bootstrap_server> connection;
};
}

void rai::bootstrap_server::run_next ()
{
	assert (!requests.empty ());
    request_response_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void rai::bulk_pull_server::set_current_end ()
{
    assert (request != nullptr);
	rai::transaction transaction (connection->node->store.environment, nullptr, false);
	if (!connection->node->store.block_exists (transaction, request->end))
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Bulk pull end block doesn't exist: %1%, sending everything") % request->end.to_string ());
		}
		request->end.clear ();
	}
	rai::account_info info;
	auto no_address (connection->node->store.account_get (transaction, request->start, info));
	if (no_address)
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Request for unknown account: %1%") % request->start.to_base58check ());
		}
		current = request->end;
	}
	else
	{
		if (!request->end.is_zero ())
		{
			auto account (connection->node->ledger.account (transaction, request->end));
			if (account == request->start)
			{
				current = info.head;
			}
			else
			{
				current = request->end;
			}
		}
		else
		{
			current = info.head;
		}
	}
}

void rai::bulk_pull_server::send_next ()
{
    std::unique_ptr <rai::block> block (get_next ());
    if (block != nullptr)
    {
        {
            send_buffer.clear ();
            rai::vectorstream stream (send_buffer);
            rai::serialize_block (stream, *block);
        }
        auto this_l (shared_from_this ());
        if (connection->node->config.logging.bulk_pull_logging ())
        {
            BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % block->hash ().to_string ());
        }
        async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            this_l->sent_action (ec, size_a);
        });
    }
    else
    {
        send_finished ();
    }
}

std::unique_ptr <rai::block> rai::bulk_pull_server::get_next ()
{
    std::unique_ptr <rai::block> result;
    if (current != request->end)
    {
		rai::transaction transaction (connection->node->store.environment, nullptr, false);
        result = connection->node->store.block_get (transaction, current);
        assert (result != nullptr);
        auto previous (result->previous ());
        if (!previous.is_zero ())
        {
            current = previous;
        }
        else
        {
            request->end = current;
        }
    }
    return result;
}

void rai::bulk_pull_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
	else
	{
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ());
	}
}

void rai::bulk_pull_server::send_finished ()
{
    send_buffer.clear ();
    send_buffer.push_back (static_cast <uint8_t> (rai::block_type::not_a_block));
    auto this_l (shared_from_this ());
    if (connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->node->log) << "Bulk sending finished";
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->no_block_sent (ec, size_a);
    });
}

void rai::bulk_pull_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
		connection->finish_request ();
    }
	else
	{
		BOOST_LOG (connection->node->log) << "Unable to send not-a-block";
	}
}

rai::bootstrap_client::bootstrap_client (std::shared_ptr <rai::node> node_a, std::function <void ()> const & completion_action_a) :
node (node_a),
socket (node_a->network.service),
completion_action (completion_action_a)
{
}

rai::bootstrap_client::~bootstrap_client ()
{
	if (node->config.logging.network_logging ())
	{
		BOOST_LOG (node->log) << "Exiting bootstrap client";
	}
	completion_action ();
}

void rai::bootstrap_client::run (boost::asio::ip::tcp::endpoint const & endpoint_a)
{
    if (node->config.logging.network_logging ())
    {
        BOOST_LOG (node->log) << boost::str (boost::format ("Initiating bootstrap connection to %1%") % endpoint_a);
    }
    auto this_l (shared_from_this ());
    socket.async_connect (endpoint_a, [this_l, endpoint_a] (boost::system::error_code const & ec)
    {
		if (!ec)
		{
			this_l->connect_action ();
		}
		else
		{
			if (this_l->node->config.logging.network_logging ())
			{
				BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error initiating bootstrap connection %1%") % ec.message ());
			}
			this_l->node->peers.bootstrap_failed (rai::endpoint (endpoint_a.address (), endpoint_a.port ()));
		}
    });
}

void rai::bootstrap_client::connect_action ()
{
	std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
	request->start.clear ();
	request->age = std::numeric_limits <decltype (request->age)>::max ();
	request->count = std::numeric_limits <decltype (request->age)>::max ();
	auto send_buffer (std::make_shared <std::vector <uint8_t>> ());
	{
		rai::vectorstream stream (*send_buffer);
		request->serialize (stream);
	}
	if (node->config.logging.network_logging ())
	{
		BOOST_LOG (node->log) << boost::str (boost::format ("Initiating frontier request for %1% age %2% count %3%") % request->start.to_string () % request->age % request->count);
	}
	auto this_l (shared_from_this ());
	boost::asio::async_write (socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l, send_buffer] (boost::system::error_code const & ec, size_t size_a)
	{
		this_l->sent_request (ec, size_a);
	});
}

void rai::bootstrap_client::sent_request (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        auto this_l (shared_from_this ());
        auto client_l (std::make_shared <rai::frontier_req_client> (this_l));
        client_l->receive_frontier ();
    }
    else
    {
        if (node->config.logging.network_logging ())
        {
            BOOST_LOG (node->log) << boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ());
        }
    }
}

void rai::bulk_pull_client::request ()
{
    if (current != end)
    {
        rai::bulk_pull req;
        req.start = current->first;
        req.end = current->second;
        ++current;
        auto buffer (std::make_shared <std::vector <uint8_t>> ());
        {
            rai::vectorstream stream (*buffer);
            req.serialize (stream);
        }
		if (connection->connection->node->config.logging.network_logging ())
		{
			BOOST_LOG (connection->connection->node->log) << boost::str (boost::format ("Requesting account %1% down to %2%") % req.start.to_string () % req.end.to_string ());
		}
        auto this_l (shared_from_this ());
        boost::asio::async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer] (boost::system::error_code const & ec, size_t size_a)
            {
                if (!ec)
                {
                    this_l->receive_block ();
                }
                else
                {
                    BOOST_LOG (this_l->connection->connection->node->log) << boost::str (boost::format ("Error sending bulk pull request %1%") % ec.message ());
                }
            });
    }
    else
    {
        process_end ();
        connection->completed_pulls ();
    }
}

void rai::bulk_pull_client::receive_block ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        if (!ec)
        {
            this_l->received_type ();
        }
        else
        {
            BOOST_LOG (this_l->connection->connection->node->log) << boost::str (boost::format ("Error receiving block type %1%") % ec.message ());
        }
    });
}

void rai::bulk_pull_client::received_type ()
{
    auto this_l (shared_from_this ());
    rai::block_type type (static_cast <rai::block_type> (receive_buffer [0]));
    switch (type)
    {
        case rai::block_type::send:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::send_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::receive:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::receive_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::open:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::open_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::change:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::change_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::not_a_block:
        {
            request ();
            break;
        }
        default:
        {
            BOOST_LOG (connection->connection->node->log) << boost::str (boost::format ("Unknown type received as block type: %1%") % static_cast <int> (type));
            break;
        }
    }
}

rai::block_synchronization::block_synchronization (boost::log::sources::logger & log_a, std::function <void (rai::block const &)> const & target_a, rai::block_store & store_a) :
log (log_a),
target (target_a),
store (store_a)
{
}

rai::block_synchronization::~block_synchronization ()
{
}

namespace {
class add_dependency_visitor : public rai::block_visitor
{
public:
    add_dependency_visitor (rai::block_synchronization & sync_a) :
    sync (sync_a),
    result (true)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
        if (result)
        {
            add_dependency (block_a.hashables.source);
        }
    }
    void open_block (rai::open_block const & block_a) override
    {
        add_dependency (block_a.hashables.source);
    }
    void change_block (rai::change_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
    }
    void add_dependency (rai::block_hash const & hash_a)
    {
        if (!sync.synchronized (hash_a))
        {
            result = false;
            sync.blocks.push (hash_a);
        }
		else
		{
			// Block is already synchronized, normal
		}
    }
    rai::block_synchronization & sync;
    bool result;
};
}

bool rai::block_synchronization::add_dependency (rai::block const & block_a)
{
    add_dependency_visitor visitor (*this);
    block_a.visit (visitor);
    return visitor.result;
}

bool rai::block_synchronization::fill_dependencies ()
{
    auto result (false);
    auto done (false);
    while (!result && !done)
    {
		auto hash (blocks.top ());
        auto block (retrieve (hash));
        if (block != nullptr)
        {
            done = add_dependency (*block);
        }
        else
        {
			BOOST_LOG (log) << boost::str (boost::format ("Unable to retrieve block while generating dependencies %1%") % hash.to_string ());
            result = true;
        }
    }
    return result;
}

bool rai::block_synchronization::synchronize_one ()
{
    auto result (fill_dependencies ());
    if (!result)
    {
		auto hash (blocks.top ());
        auto block (retrieve (hash));
        blocks.pop ();
        if (block != nullptr)
        {
			target (*block);
		}
		else
		{
			BOOST_LOG (log) << boost::str (boost::format ("Unable to retrieve block while synchronizing %1%") % hash.to_string ());
			result = true;
		}
    }
    return result;
}

bool rai::block_synchronization::synchronize (rai::block_hash const & hash_a)
{
    auto result (false);
    blocks.push (hash_a);
    while (!result && !blocks.empty ())
    {
        result = synchronize_one ();
    }
    return result;
}

rai::pull_synchronization::pull_synchronization (boost::log::sources::logger & log_a, std::function <void (rai::block const &)> const & target_a, rai::block_store & store_a) :
block_synchronization (log_a, target_a, store_a)
{
}

std::unique_ptr <rai::block> rai::pull_synchronization::retrieve (rai::block_hash const & hash_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
    return store.unchecked_get (transaction, hash_a);
}

bool rai::pull_synchronization::synchronized (rai::block_hash const & hash_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
    return store.block_exists (transaction, hash_a);
}

rai::push_synchronization::push_synchronization (boost::log::sources::logger & log_a, std::function <void (rai::block const &)> const & target_a, rai::block_store & store_a) :
block_synchronization (log_a, target_a, store_a)
{
}

bool rai::push_synchronization::synchronized (rai::block_hash const & hash_a)
{
	rai::transaction transaction (store.environment, nullptr, true);
    auto result (!store.unsynced_exists (transaction, hash_a));
	if (!result)
	{
		store.unsynced_del (transaction, hash_a);
	}
	return result;
}

std::unique_ptr <rai::block> rai::push_synchronization::retrieve (rai::block_hash const & hash_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
    return store.block_get (transaction, hash_a);
}

rai::block_hash rai::bulk_pull_client::first ()
{
	rai::block_hash result (0);
	rai::transaction transaction (connection->connection->node->store.environment, nullptr, false);
	auto iterator (connection->connection->node->store.unchecked_begin (transaction));
	if (iterator != connection->connection->node->store.unchecked_end ())
	{
		result = rai::block_hash (iterator->first);
	}
	return result;
}

void rai::bulk_pull_client::process_end ()
{
	rai::pull_synchronization synchronization (connection->connection->node->log, [this] (rai::block const & block_a)
	{
		auto process_result (connection->connection->node->process_receive (block_a));
		switch (process_result.code)
		{
			case rai::process_result::progress:
			case rai::process_result::old:
				break;
			case rai::process_result::fork:
				connection->connection->node->network.broadcast_confirm_req (block_a);
				BOOST_LOG (connection->connection->node->log) << boost::str (boost::format ("Fork received in bootstrap for block: %1%") % block_a.hash ().to_string ());
				break;
			default:
				BOOST_LOG (connection->connection->node->log) << boost::str (boost::format ("Error inserting block in bootstrap: %1%") % block_a.hash ().to_string ());
				break;
		}
		rai::transaction transaction (connection->connection->node->store.environment, nullptr, true);
		connection->connection->node->store.unchecked_del (transaction, block_a.hash ());
	}, connection->connection->node->store);
	rai::block_hash block (first ());
    while (!block.is_zero ())
    {
		auto error (synchronization.synchronize (block));
        if (error)
        {
			// Pulling account chains isn't transactional so updates happening during the process can cause dependency failures
			// A node may also have erroneously not sent the required dependent blocks
			BOOST_LOG (connection->connection->node->log) << boost::str (boost::format ("Error synchronizing block: %1%") % block.to_string ());
			rai::transaction transaction (connection->connection->node->store.environment, nullptr, true);
            while (!synchronization.blocks.empty ())
            {
                connection->connection->node->store.unchecked_del (transaction, synchronization.blocks.top ());
                synchronization.blocks.pop ();
            }
        }
		block = first ();
    }
}

void rai::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		rai::bufferstream stream (receive_buffer.data (), 1 + size_a);
		auto block (rai::deserialize_block (stream));
		if (block != nullptr)
		{
            auto hash (block->hash ());
            if (connection->connection->node->config.logging.bulk_pull_logging ())
            {
                std::string block_l;
                block->serialize_json (block_l);
                BOOST_LOG (connection->connection->node->log) << boost::str (boost::format ("Pulled block %1% %2%") % hash.to_string () % block_l);
            }
			rai::transaction transaction (connection->connection->node->store.environment, nullptr, true);
            connection->connection->node->store.unchecked_put (transaction, hash, *block);
            receive_block ();
		}
        else
        {
            BOOST_LOG (connection->connection->node->log) << "Error deserializing block received from pull request";
        }
	}
	else
	{
		BOOST_LOG (connection->connection->node->log) << boost::str (boost::format ("Error bulk receiving block: %1%") % ec.message ());
	}
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

boost::asio::ip::tcp::endpoint rai::bootstrap_listener::endpoint ()
{
    return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), local.port ());
}

rai::bootstrap_server::~bootstrap_server ()
{
    if (node->config.logging.network_logging ())
    {
        BOOST_LOG (node->log) << "Exiting bootstrap server";
    }
}

void rai::peer_container::bootstrap_failed (rai::endpoint const & endpoint_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	auto existing (peers.find (endpoint_a));
	if (existing != peers.end ())
	{
		peers.modify (existing, [] (rai::peer_information & info_a)
		{
			info_a.last_bootstrap_failure = std::chrono::system_clock::now ();
		});
	}
}

void rai::peer_container::random_fill (std::array <rai::endpoint, 8> & target_a)
{
    auto peers (list ());
    while (peers.size () > target_a.size ())
    {
        auto index (random_pool.GenerateWord32 (0, peers.size () - 1));
        assert (index < peers.size ());
		assert (index >= 0);
		if (index != peers.size () - 1)
		{
				peers [index] = peers [peers.size () - 1];
		}
        peers.pop_back ();
    }
    assert (peers.size () <= target_a.size ());
    auto endpoint (rai::endpoint (boost::asio::ip::address_v6 {}, 0));
    assert (endpoint.address ().is_v6 ());
    std::fill (target_a.begin (), target_a.end (), endpoint);
    auto j (target_a.begin ());
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
    {
        assert (i->endpoint.address ().is_v6 ());
        assert (j < target_a.end ());
        *j = i->endpoint;
    }
}

std::vector <rai::peer_information> rai::peer_container::purge_list (std::chrono::system_clock::time_point const & cutoff)
{
	std::vector <rai::peer_information> result;
	{
		std::lock_guard <std::mutex> lock (mutex);
		auto pivot (peers.get <1> ().lower_bound (cutoff));
		result.assign (pivot, peers.get <1> ().end ());
		peers.get <1> ().erase (peers.get <1> ().begin (), pivot);
		for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
		{
			peers.modify (i, [] (rai::peer_information & info) {info.last_attempt = std::chrono::system_clock::now ();});
		}
	}
	if (result.empty ())
	{
		disconnect_observer ();
	}
    return result;
}

size_t rai::peer_container::size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return peers.size ();
}

bool rai::peer_container::empty ()
{
    return size () == 0;
}

bool rai::peer_container::not_a_peer (rai::endpoint const & endpoint_a)
{
    bool result (false);
    if (endpoint_a.address ().to_v6 ().is_unspecified ())
    {
        result = true;
    }
    else if (rai::reserved_address (endpoint_a))
    {
        result = true;
    }
    else if (endpoint_a == self)
    {
        result = true;
    }
    return result;
}

bool rai::peer_container::insert (rai::endpoint const & endpoint_a)
{
    return insert (endpoint_a, rai::block_hash (0));
}

bool rai::peer_container::knows_about (rai::endpoint const & endpoint_a, rai::block_hash const & hash_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    bool result (false);
    auto existing (peers.find (endpoint_a));
    if (existing != peers.end ())
    {
        result = existing->most_recent == hash_a;
    }
    return result;
}

bool rai::peer_container::insert (rai::endpoint const & endpoint_a, rai::block_hash const & hash_a)
{
	auto unknown (false);
    auto result (not_a_peer (endpoint_a));
    if (!result)
    {
        std::lock_guard <std::mutex> lock (mutex);
        auto existing (peers.find (endpoint_a));
        if (existing != peers.end ())
        {
            peers.modify (existing, [&hash_a] (rai::peer_information & info)
            {
                info.last_contact = std::chrono::system_clock::now ();
                info.most_recent = hash_a;
            });
            result = true;
        }
        else
        {
            peers.insert ({endpoint_a, std::chrono::system_clock::now (), std::chrono::system_clock::now (), std::chrono::system_clock::time_point (), hash_a});
			unknown = true;
        }
    }
	if (unknown)
	{
		peer_observer (endpoint_a);
	}
    return result;
}

namespace {
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long address_a)
{
    return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}
}

bool rai::reserved_address (rai::endpoint const & endpoint_a)
{
    assert (endpoint_a.address ().is_v6 ());
	auto bytes (endpoint_a.address ().to_v6 ());
	auto result (false);
    if (bytes >= mapped_from_v4_bytes (0x00000000ul) && bytes <= mapped_from_v4_bytes (0x00fffffful)) // Broadcast RFC1700
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xc0000200ul) && bytes <= mapped_from_v4_bytes (0xc00002fful)) // TEST-NET RFC5737
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xc6336400ul) && bytes <= mapped_from_v4_bytes (0xc63364fful)) // TEST-NET-2 RFC5737
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xcb007100ul) && bytes <= mapped_from_v4_bytes (0xcb0071fful)) // TEST-NET-3 RFC5737
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xe9fc0000ul) && bytes <= mapped_from_v4_bytes (0xe9fc00fful))
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xf0000000ul)) // Reserved RFC6890
	{
		result = true;
	}
	return result;
}

rai::peer_container::peer_container (rai::endpoint const & self_a) :
self (self_a),
peer_observer ([] (rai::endpoint const &) {}),
disconnect_observer ([] () {})
{
}

void rai::peer_container::contacted (rai::endpoint const & endpoint_a)
{
    auto endpoint_l (endpoint_a);
    if (endpoint_l.address ().is_v4 ())
    {
        endpoint_l = rai::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
    }
    assert (endpoint_l.address ().is_v6 ());
	insert (endpoint_l);
}

std::ostream & operator << (std::ostream & stream_a, std::chrono::system_clock::time_point const & time_a)
{
    time_t last_contact (std::chrono::system_clock::to_time_t (time_a));
    std::string string (ctime (&last_contact));
    string.pop_back ();
    stream_a << string;
    return stream_a;
}

void rai::network::initiate_send ()
{
	assert (!socket_mutex.try_lock ());
	assert (!sends.empty ());
	auto & front (sends.front ());
	if (node.config.logging.network_packet_logging ())
	{
		BOOST_LOG (node.log) << "Sending packet";
	}
	socket.async_send_to (boost::asio::buffer (front.data, front.size), front.endpoint, [this, front] (boost::system::error_code const & ec, size_t size_a)
	{
		if (front.rebroadcast > 0)
		{
			node.service.add (std::chrono::system_clock::now () + std::chrono::seconds (node.config.rebroadcast_delay), [this, front]
			{
				send_buffer (front.data, front.size, front.endpoint, front.rebroadcast - 1, front.callback);
			});
		}
		else
		{
			rai::send_info self;
			{
				std::unique_lock <std::mutex> lock (socket_mutex);
				assert (!sends.empty ());
				self = sends.front ();
			}
			self.callback (ec, size_a);
		}
		send_complete (ec, size_a);
	});
}

void rai::network::send_buffer (uint8_t const * data_a, size_t size_a, rai::endpoint const & endpoint_a, size_t rebroadcast_a, std::function <void (boost::system::error_code const &, size_t)> callback_a)
{
	std::unique_lock <std::mutex> lock (socket_mutex);
	auto initiate (sends.empty ());
	sends.push ({data_a, size_a, endpoint_a, rebroadcast_a, callback_a});
	if (initiate)
	{
		initiate_send ();
	}
}

void rai::network::send_complete (boost::system::error_code const & ec, size_t size_a)
{
    if (node.config.logging.network_packet_logging ())
    {
        BOOST_LOG (node.log) << "Packet send complete";
    }
	std::unique_lock <std::mutex> lock (socket_mutex);
	assert (!sends.empty ());
	sends.pop ();
	if (!sends.empty ())
	{
		if (node.config.logging.network_packet_logging ())
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Delaying next packet send %1% microseconds") % node.config.packet_delay_microseconds);
		}
		node.service.add (std::chrono::system_clock::now () + std::chrono::microseconds (node.config.packet_delay_microseconds), [this] ()
		{
			std::unique_lock <std::mutex> lock (socket_mutex);
			initiate_send ();
		});
	}
}

uint64_t rai::block_store::now ()
{
    boost::posix_time::ptime epoch (boost::gregorian::date (1970, 1, 1));
    auto now (boost::posix_time::second_clock::universal_time ());
    auto diff (now - epoch);
    return diff.total_seconds ();
}

rai::bulk_push_server::bulk_push_server (std::shared_ptr <rai::bootstrap_server> const & connection_a) :
connection (connection_a)
{
}

void rai::bulk_push_server::receive ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            if (!ec)
            {
                this_l->received_type ();
            }
            else
            {
                BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type %1%") % ec.message ());
            }
        });
}

void rai::bulk_push_server::received_type ()
{
    auto this_l (shared_from_this ());
    rai::block_type type (static_cast <rai::block_type> (receive_buffer [0]));
    switch (type)
    {
        case rai::block_type::send:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::send_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::receive:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::receive_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::open:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::open_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::change:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::change_block::size), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::not_a_block:
        {
            connection->finish_request ();
            break;
        }
        default:
        {
            BOOST_LOG (connection->node->log) << "Unknown type received as block type";
            break;
        }
    }
}

void rai::bulk_push_server::received_block (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        rai::bufferstream stream (receive_buffer.data (), 1 + size_a);
        auto block (rai::deserialize_block (stream));
        if (block != nullptr)
        {
            connection->node->process_receive_republish (std::move (block), 0);
            receive ();
        }
        else
        {
            BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
        }
    }
}

rai::bulk_pull_server::bulk_pull_server (std::shared_ptr <rai::bootstrap_server> const & connection_a, std::unique_ptr <rai::bulk_pull> request_a) :
connection (connection_a),
request (std::move (request_a))
{
    set_current_end ();
}

rai::frontier_req_server::frontier_req_server (std::shared_ptr <rai::bootstrap_server> const & connection_a, std::unique_ptr <rai::frontier_req> request_a) :
connection (connection_a),
current (request_a->start.number () - 1),
info (0, 0, 0, 0),
request (std::move (request_a))
{
	next ();
    skip_old ();
}

void rai::frontier_req_server::skip_old ()
{
    if (request->age != std::numeric_limits<decltype (request->age)>::max ())
    {
        auto now (connection->node->store.now ());
        while (!current.is_zero () && (now - info.modified) >= request->age)
        {
            next ();
        }
    }
}

void rai::frontier_req_server::send_next ()
{
    if (!current.is_zero ())
    {
        {
            send_buffer.clear ();
            rai::vectorstream stream (send_buffer);
            write (stream, current.bytes);
            write (stream, info.head.bytes);
        }
        auto this_l (shared_from_this ());
        if (connection->node->config.logging.network_logging ())
        {
            BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending frontier for %1% %2%") % current.to_base58check () % info.head.to_string ());
        }
		next ();
        async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            this_l->sent_action (ec, size_a);
        });
    }
    else
    {
        send_finished ();
    }
}

void rai::frontier_req_server::send_finished ()
{
    {
        send_buffer.clear ();
        rai::vectorstream stream (send_buffer);
        rai::uint256_union zero (0);
        write (stream, zero.bytes);
        write (stream, zero.bytes);
    }
    auto this_l (shared_from_this ());
    if (connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->node->log) << "Frontier sending finished";
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->no_block_sent (ec, size_a);
    });
}

void rai::frontier_req_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
		connection->finish_request ();
    }
    else
    {
        if (connection->node->config.logging.network_logging ())
        {
            BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending frontier finish %1%") % ec.message ());
        }
    }
}

void rai::frontier_req_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
    else
    {
        if (connection->node->config.logging.network_logging ())
        {
            BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending frontier pair %1%") % ec.message ());
        }
    }
}

void rai::frontier_req_server::next ()
{
	rai::transaction transaction (connection->node->store.environment, nullptr, false);
	auto iterator (connection->node->store.latest_begin (transaction, current.number () + 1));
	if (iterator != connection->node->store.latest_end ())
	{
		current = rai::uint256_union (iterator->first);
		info = rai::account_info (iterator->second);
	}
	else
	{
		current.clear ();
	}
}

rai::frontier_req::frontier_req () :
message (rai::message_type::frontier_req)
{
}

bool rai::frontier_req::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::frontier_req == type);
    if (!result)
    {
        assert (type == rai::message_type::frontier_req);
        result = read (stream_a, start.bytes);
        if (!result)
        {
            result = read (stream_a, age);
            if (!result)
            {
                result = read (stream_a, count);
            }
        }
    }
    return result;
}

void rai::frontier_req::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
    write (stream_a, start.bytes);
    write (stream_a, age);
    write (stream_a, count);
}

void rai::frontier_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.frontier_req (*this);
}

bool rai::frontier_req::operator == (rai::frontier_req const & other_a) const
{
    return start == other_a.start && age == other_a.age && count == other_a.count;
}

rai::bulk_pull_client::bulk_pull_client (std::shared_ptr <rai::frontier_req_client> const & connection_a) :
connection (connection_a),
current (connection->pulls.begin ()),
end (connection->pulls.end ())
{
}

rai::bulk_pull_client::~bulk_pull_client ()
{
    if (connection->connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->connection->node->log) << "Exiting bulk pull client";
    }
}

rai::frontier_req_client::frontier_req_client (std::shared_ptr <rai::bootstrap_client> const & connection_a) :
connection (connection_a),
current (0)
{
	next ();
}

rai::frontier_req_client::~frontier_req_client ()
{
    if (connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->node->log) << "Exiting frontier_req initiator";
    }
}

void rai::frontier_req_client::receive_frontier ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->received_frontier (ec, size_a);
    });
}

void rai::frontier_req_client::request_account (rai::account const & account_a)
{
    // Account they know about and we don't.
    pulls [account_a] = rai::block_hash (0);
}

void rai::frontier_req_client::completed_pulls ()
{
    auto this_l (shared_from_this ());
    auto pushes (std::make_shared <rai::bulk_push_client> (this_l));
    pushes->start ();
}

void rai::frontier_req_client::unsynced (MDB_txn * transaction_a, rai::block_hash const & ours_a, rai::block_hash const & theirs_a)
{
	auto current (ours_a);
	while (!current.is_zero () && current != theirs_a)
	{
		connection->node->store.unsynced_put (transaction_a, current);
		auto block (connection->node->store.block_get (transaction_a, current));
		current = block->previous ();
	}
}

void rai::frontier_req_client::received_frontier (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        rai::account account;
        rai::bufferstream account_stream (receive_buffer.data (), sizeof (rai::uint256_union));
        auto error1 (rai::read (account_stream, account));
        assert (!error1);
        rai::block_hash latest;
        rai::bufferstream latest_stream (receive_buffer.data () + sizeof (rai::uint256_union), sizeof (rai::uint256_union));
        auto error2 (rai::read (latest_stream, latest));
        assert (!error2);
        if (!account.is_zero ())
        {
            while (!current.is_zero () && current < account)
            {
				rai::transaction transaction (connection->node->store.environment, nullptr, true);
                // We know about an account they don't.
				unsynced (transaction, info.head, 0);
				next ();
            }
            if (!current.is_zero ())
            {
                if (account == current)
                {
                    if (latest == info.head)
                    {
                        // In sync
                    }
                    else
					{
						rai::transaction transaction (connection->node->store.environment, nullptr, true);
						if (connection->node->store.block_exists (transaction, latest))
						{
							// We know about a block they don't.
							unsynced (transaction, info.head, latest);
						}
						else
						{
							// They know about a block we don't.
							pulls [account] = info.head;
						}
					}
					next ();
                }
                else
                {
                    assert (account < current);
                    request_account (account);
                }
            }
            else
            {
                request_account (account);
            }
            receive_frontier ();
        }
        else
        {
			{
				rai::transaction transaction (connection->node->store.environment, nullptr, true);
				while (!current.is_zero ())
				{
					// We know about an account they don't.
					unsynced (transaction, info.head, 0);
					next ();
				}
			}
            completed_requests ();
        }
    }
    else
    {
        if (connection->node->config.logging.network_logging ())
        {
            BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error while receiving frontier %1%") % ec.message ());
        }
    }
}

void rai::frontier_req_client::next ()
{
	rai::transaction transaction (connection->node->store.environment, nullptr, false);
	auto iterator (connection->node->store.latest_begin (transaction, rai::uint256_union (current.number () + 1)));
	if (iterator != connection->node->store.latest_end ())
	{
		current = rai::account (iterator->first);
		info = rai::account_info (iterator->second);
	}
	else
	{
		current.clear ();
	}
}

void rai::frontier_req_client::completed_requests ()
{
    auto this_l (shared_from_this ());
    auto pulls (std::make_shared <rai::bulk_pull_client> (this_l));
    pulls->request ();
}

void rai::frontier_req_client::completed_pushes ()
{
}

rai::bulk_push_client::bulk_push_client (std::shared_ptr <rai::frontier_req_client> const & connection_a) :
connection (connection_a),
synchronization (connection->connection->node->log, [this] (rai::block const & block_a)
{
    push_block (block_a);
}, connection_a->connection->node->store)
{
}

rai::bulk_push_client::~bulk_push_client ()
{
    if (connection->connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->connection->node->log) << "Exiting bulk push client";
    }
}

void rai::bulk_push_client::start ()
{
    rai::bulk_push message;
    auto buffer (std::make_shared <std::vector <uint8_t>> ());
    {
        rai::vectorstream stream (*buffer);
        message.serialize (stream);
    }
    auto this_l (shared_from_this ());
    boost::asio::async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer] (boost::system::error_code const & ec, size_t size_a)
        {
            if (!ec)
            {
                this_l->push ();
            }
            else
            {
                BOOST_LOG (this_l->connection->connection->node->log) << boost::str (boost::format ("Unable to send bulk_push request %1%") % ec.message ());
            }
        });
}

void rai::bulk_push_client::push ()
{
	rai::block_hash hash (0);
	{
		rai::transaction transaction (connection->connection->node->store.environment, nullptr, true);
		auto first (connection->connection->node->store.unsynced_begin (transaction));
		if (first != rai::store_iterator (nullptr))
		{
			hash = first->first;
			connection->connection->node->store.unsynced_del (transaction, hash);
		}
	}
	if (!hash.is_zero ())
	{
		synchronization.blocks.push (hash);
        synchronization.synchronize_one ();
    }
    else
    {
        send_finished ();
    }
}

void rai::bulk_push_client::send_finished ()
{
    auto buffer (std::make_shared <std::vector <uint8_t>> ());
    buffer->push_back (static_cast <uint8_t> (rai::block_type::not_a_block));
    if (connection->connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->connection->node->log) << "Bulk push finished";
    }
    auto this_l (shared_from_this ());
    async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            this_l->connection->completed_pushes ();
        });
}

void rai::bulk_push_client::push_block (rai::block const & block_a)
{
    auto buffer (std::make_shared <std::vector <uint8_t>> ());
    {
        rai::vectorstream stream (*buffer);
        rai::serialize_block (stream, block_a);
    }
    auto this_l (shared_from_this ());
    boost::asio::async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer] (boost::system::error_code const & ec, size_t size_a)
	{
		if (!ec)
		{
			if (!this_l->synchronization.blocks.empty ())
			{
				this_l->synchronization.synchronize_one ();
			}
			else
			{
				this_l->push ();
			}
		}
		else
		{
			BOOST_LOG (this_l->connection->connection->node->log) << boost::str (boost::format ("Error sending block during bulk push %1%") % ec.message ());
		}
	});
}

bool rai::keepalive::operator == (rai::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

bool rai::peer_container::known_peer (rai::endpoint const & endpoint_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (peers.find (endpoint_a));
    return existing != peers.end () && existing->last_contact > std::chrono::system_clock::now () - rai::node::cutoff;
}

std::shared_ptr <rai::node> rai::node::shared ()
{
    return shared_from_this ();
}

namespace
{
class traffic_generator : public std::enable_shared_from_this <traffic_generator>
{
public:
    traffic_generator (uint32_t count_a, uint32_t wait_a, std::shared_ptr <rai::node> node_a, rai::system & system_a) :
    count (count_a),
    wait (wait_a),
    node (node_a),
    system (system_a)
    {
    }
    void run ()
    {
        auto count_l (count - 1);
        count = count_l - 1;
        system.generate_activity (*node);
        if (count_l > 0)
        {
            auto this_l (shared_from_this ());
            node->service.add (std::chrono::system_clock::now () + std::chrono::milliseconds (wait), [this_l] () {this_l->run ();});
        }
    }
    uint32_t count;
    uint32_t wait;
    std::shared_ptr <rai::node> node;
    rai::system & system;
};
}

void rai::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a)
{
    for (size_t i (0), n (nodes.size ()); i != n; ++i)
    {
        generate_usage_traffic (count_a, wait_a, i);
    }
}

void rai::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a, size_t index_a)
{
    assert (nodes.size () > index_a);
    assert (count_a > 0);
    auto generate (std::make_shared <traffic_generator> (count_a, wait_a, nodes [index_a], *this));
    generate->run ();
}

void rai::system::generate_activity (rai::node & node_a)
{
    auto what (random_pool.GenerateByte ());
    if (what < 0xc0)
    {
        generate_send_existing (node_a);
    }
    else
    {
        generate_send_new (node_a);
    }
}

rai::account rai::system::get_random_account (MDB_txn * transaction_a, rai::node & node_a)
{
	auto accounts (wallet (0)->store.accounts (transaction_a));
	auto index (random_pool.GenerateWord32 (0, accounts.size () - 1));
	auto result (accounts [index]);
	return result;
}

rai::uint128_t rai::system::get_random_amount (MDB_txn * transaction_a, rai::node & node_a, rai::account const & account_a)
{
    rai::uint128_t balance (node_a.ledger.account_balance (transaction_a, account_a));
    std::string balance_text (balance.convert_to <std::string> ());
    rai::uint128_union random_amount;
    random_pool.GenerateBlock (random_amount.bytes.data (), sizeof (random_amount.bytes));
    auto result (((rai::uint256_t {random_amount.number ()} * balance) / rai::uint256_t {std::numeric_limits <rai::uint128_t>::max ()}).convert_to <rai::uint128_t> ());
    std::string text (result.convert_to <std::string> ());
    return result;
}

void rai::system::generate_send_existing (rai::node & node_a)
{
	rai::uint128_t amount;
	rai::account destination;
	rai::account source;
	{
		rai::account account;
		random_pool.GenerateBlock (account.bytes.data (), sizeof (account.bytes));
		rai::transaction transaction (node_a.store.environment, nullptr, false);
		rai::store_iterator entry (node_a.store.latest_begin (transaction, account));
		if (entry == node_a.store.latest_end ())
		{
			entry = node_a.store.latest_begin (transaction);
		}
		assert (entry != node_a.store.latest_end ());
		destination = rai::account (entry->first);
		source = get_random_account (transaction, node_a);
		amount = get_random_amount (transaction, node_a, source);
	}
    wallet (0)->send_sync (source, destination, amount);
}

void rai::system::generate_send_new (rai::node & node_a)
{
    assert (node_a.wallets.items.size () == 1);
    rai::keypair key;
	rai::uint128_t amount;
	rai::account source;
	{
		rai::transaction transaction (node_a.store.environment, nullptr, false);
		source = get_random_account (transaction, node_a);
		amount = get_random_amount (transaction, node_a, source);
	}
	node_a.wallets.items.begin ()->second->insert (key.prv);
    node_a.wallets.items.begin ()->second->send_sync (source, key.pub, amount);
}

void rai::system::generate_mass_activity (uint32_t count_a, rai::node & node_a)
{
    auto previous (std::chrono::system_clock::now ());
    for (uint32_t i (0); i < count_a; ++i)
    {
        if ((i & 0x3ff) == 0)
        {
            auto now (std::chrono::system_clock::now ());
            auto ms (std::chrono::duration_cast <std::chrono::milliseconds> (now - previous).count ());
            std::cerr << boost::str (boost::format ("Mass activity iteration %1% ms %2% ms/t %3%\n") % i % ms % (ms / 256));
            previous = now;
        }
        generate_activity (node_a);
    }
}

rai::election::election (std::shared_ptr <rai::node> node_a, rai::block const & block_a, std::function <void (rai::block &)> const & confirmation_action_a) :
votes (block_a.root ()),
node (node_a),
last_vote (std::chrono::system_clock::now ()),
last_winner (block_a.clone ()),
confirmed (false),
confirmation_action (confirmation_action_a)
{
	{
		rai::transaction transaction (node_a->store.environment, nullptr, false);
		assert (node_a->store.block_exists (transaction, block_a.hash ()));
	}
    rai::keypair anonymous;
    rai::vote vote_l (anonymous.pub, anonymous.prv, 0, block_a.clone ());
    vote (vote_l);
}

void rai::election::start ()
{
	auto node_l (node.lock ());
	if (node_l != nullptr)
	{
		auto have_representative (node_l->representative_vote (*this, *last_winner));
		if (have_representative)
		{
			announce_vote ();
		}
		timeout_action ();
	}
}

void rai::election::timeout_action ()
{
	auto node_l (node.lock ());
	if (node_l != nullptr)
	{
		auto now (std::chrono::system_clock::now ());
		if (now - last_vote < std::chrono::seconds (15))
		{
			auto this_l (shared_from_this ());
			node_l->service.add (now + std::chrono::seconds (15), [this_l] () {this_l->timeout_action ();});
		}
		else
		{
			auto root_l (votes.id);
			node_l->conflicts.stop (root_l);
			if (!confirmed)
			{
				BOOST_LOG (node_l->log) << boost::str (boost::format ("Election timed out for block %1%") % last_winner->hash ().to_string ());
			}
		}
	}
}

rai::uint128_t rai::election::uncontested_threshold (MDB_txn * transaction_a, rai::ledger & ledger_a)
{
    return ledger_a.supply (transaction_a) / 2;
}

rai::uint128_t rai::election::contested_threshold (MDB_txn * transaction_a, rai::ledger & ledger_a)
{
    return (ledger_a.supply (transaction_a) / 16) * 15;
}

void rai::election::vote (rai::vote const & vote_a)
{
	auto node_l (node.lock ());
	if (node_l != nullptr)
	{
		auto changed (votes.vote (vote_a));
		std::unique_ptr <rai::block> winner;
		auto was_confirmed (confirmed);
		{
			rai::transaction transaction (node_l->store.environment, nullptr, true);
			if (!was_confirmed && changed)
			{
				auto tally_l (node_l->ledger.tally (transaction, votes));
				assert (tally_l.size () > 0);
				winner = tally_l.begin ()->second->clone ();
				if (!(*winner == *last_winner))
				{
					node_l->ledger.rollback (transaction, last_winner->hash ());
					node_l->ledger.process (transaction, *winner);
					last_winner = winner->clone ();
				}
				if (tally_l.size () == 1)
				{
					if (tally_l.begin ()->first > uncontested_threshold (transaction, node_l->ledger))
					{
						confirmed = true;
					}
				}
				else
				{
					if (tally_l.begin ()->first > contested_threshold (transaction, node_l->ledger))
					{
						confirmed = true;
					}
				}
			}
		}
		if (!was_confirmed && confirmed)
		{
			std::shared_ptr <rai::block> winner_l (winner.release ());
			auto confirmation_action_l (confirmation_action);
			node_l->service.add (std::chrono::system_clock::now (), [winner_l, confirmation_action_l] ()
			{
				confirmation_action_l (*winner_l);
			});
		}
	}
}

void rai::election::start_request (rai::block const & block_a)
{
	auto node_l (node.lock ());
	if (node_l != nullptr)
	{
		node_l->network.broadcast_confirm_req (block_a);
	}
}

void rai::election::announce_vote ()
{
	auto node_l (node.lock ());
	if (node_l != nullptr)
	{
		std::pair <rai::uint128_t, std::unique_ptr <rai::block>> winner_l;
		{
			rai::transaction transaction (node_l->store.environment, nullptr, false);
			winner_l = node_l->ledger.winner (transaction, votes);
		}
		assert (winner_l.second != nullptr);
		auto list (node_l->peers.list ());
		node_l->network.confirm_broadcast (list, std::move (winner_l.second), votes.sequence, 0);
		auto now (std::chrono::system_clock::now ());
		if (now - last_vote < std::chrono::seconds (15))
		{
			auto this_l (shared_from_this ());
			node_l->service.add (now + std::chrono::seconds (15), [this_l] () {this_l->announce_vote ();});
		}
	}
}

void rai::conflicts::start (rai::block const & block_a, std::function <void (rai::block &)> const & confirmation_action_a, bool request_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto root (block_a.root ());
    auto existing (roots.find (root));
    if (existing == roots.end ())
    {
        auto election (std::make_shared <rai::election> (node.shared (), block_a, confirmation_action_a));
		node.service.add (std::chrono::system_clock::now (), [election] () {election->start ();});
        roots.insert (std::make_pair (root, election));
        if (request_a)
        {
            election->start_request (block_a);
        }
    }
}

bool rai::conflicts::no_conflict (rai::block_hash const & hash_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto result (true);
    auto existing (roots.find (hash_a));
    if (existing != roots.end ())
    {
        auto size (existing->second->votes.rep_votes.size ());
		if (size > 1)
		{
			auto & block (existing->second->votes.rep_votes.begin ()->second.second);
			for (auto i (existing->second->votes.rep_votes.begin ()), n (existing->second->votes.rep_votes.end ()); i != n && result; ++i)
			{
				result = *block == *i->second.second;
			}
		}
    }
    return result;
}

// Validate a vote and apply it to the current election or start a new election if it doesn't exist
void rai::conflicts::update (rai::vote const & vote_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (roots.find (vote_a.block->root ()));
    if (existing != roots.end ())
    {
        existing->second->vote (vote_a);
    }
}

void rai::conflicts::stop (rai::block_hash const & root_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    assert (roots.find (root_a) != roots.end ());
    roots.erase (root_a);
}

rai::conflicts::conflicts (rai::node & node_a) :
node (node_a)
{
}

bool rai::node::representative_vote (rai::election & election_a, rai::block const & block_a)
{
    bool result (false);
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		auto is_representative (false);
		rai::vote vote_l;
		{
			rai::transaction transaction (store.environment, nullptr, false);
			is_representative = i->second->store.is_representative (transaction);
			if (is_representative)
			{
				auto representative (i->second->store.representative (transaction));
				rai::private_key prv;
				auto error (i->second->store.fetch (transaction, representative, prv));
				(void)error;
				vote_l = rai::vote (representative, prv, 0, block_a.clone ());
				prv.clear ();
				result = true;
			}
		}
		if (is_representative)
		{
			election_a.vote (vote_l);
		}
	}
    return result;
}

bool rai::confirm_req::operator == (rai::confirm_req const & other_a) const
{
    return *block == *other_a.block;
}

bool rai::publish::operator == (rai::publish const & other_a) const
{
    return *block == *other_a.block;
}

rai::fan::fan (rai::uint256_union const & key, size_t count_a)
{
    std::unique_ptr <rai::uint256_union> first (new rai::uint256_union (key));
    for (auto i (0); i != count_a; ++i)
    {
        std::unique_ptr <rai::uint256_union> entry (new rai::uint256_union);
        random_pool.GenerateBlock (entry->bytes.data (), entry->bytes.size ());
        *first ^= *entry;
        values.push_back (std::move (entry));
    }
    values.push_back (std::move (first));
}

rai::uint256_union rai::fan::value ()
{
    rai::uint256_union result;
    result.clear ();
    for (auto & i: values)
    {
        result ^= *i;
    }
    return result;
}

void rai::fan::value_set (rai::uint256_union const & value_a)
{
    auto value_l (value ());
    *(values [0]) ^= value_l;
    *(values [0]) ^= value_a;
}

std::array <uint8_t, 2> constexpr rai::message::magic_number;
size_t constexpr rai::message::ipv4_only_position;
size_t constexpr rai::message::bootstrap_server_position;
std::bitset <16> constexpr rai::message::block_type_mask;

rai::message::message (rai::message_type type_a) :
version_max (0x01),
version_using (0x01),
version_min (0x01),
type (type_a)
{
}

rai::message::message (bool & error_a, rai::stream & stream_a)
{
	error_a = read_header (stream_a, version_max, version_using, version_min, type, extensions);
}

rai::block_type rai::message::block_type () const
{
    return static_cast <rai::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void rai::message::block_type_set (rai::block_type type_a)
{
    extensions &= ~rai::message::block_type_mask;
    extensions |= std::bitset <16> (static_cast <unsigned long long> (type_a) << 8);
}

bool rai::message::ipv4_only ()
{
    return extensions.test (ipv4_only_position);
}

void rai::message::ipv4_only_set (bool value_a)
{
    extensions.set (ipv4_only_position, value_a);
}

void rai::message::write_header (rai::stream & stream_a)
{
    rai::write (stream_a, rai::message::magic_number);
    rai::write (stream_a, version_max);
    rai::write (stream_a, version_using);
    rai::write (stream_a, version_min);
    rai::write (stream_a, type);
    rai::write (stream_a, static_cast <uint16_t> (extensions.to_ullong ()));
}

bool rai::message::read_header (rai::stream & stream_a, uint8_t & version_max_a, uint8_t & version_using_a, uint8_t & version_min_a, rai::message_type & type_a, std::bitset <16> & extensions_a)
{
    std::array <uint8_t, 2> magic_number_l;
    auto result (rai::read (stream_a, magic_number_l));
    if (!result)
    {
        result = magic_number_l != magic_number;
        if (!result)
        {
            result = rai::read (stream_a, version_max_a);
            if (!result)
            {
                result = rai::read (stream_a, version_using_a);
                if (!result)
                {
                    result = rai::read (stream_a, version_min_a);
                    if (!result)
                    {
                        result = rai::read (stream_a, type_a);
						if (!result)
						{
							uint16_t extensions_l;
							result = rai::read (stream_a, extensions_l);
							if (!result)
							{
								extensions_a = extensions_l;
							}
						}
                    }
                }
            }
        }
    }
    return result;
}

rai::landing_store::landing_store ()
{
}

rai::landing_store::landing_store (rai::account const & source_a, rai::account const & destination_a, uint64_t start_a, uint64_t last_a) :
source (source_a),
destination (destination_a),
start (start_a),
last (last_a)
{
}

rai::landing_store::landing_store (bool & error_a, std::istream & stream_a)
{
	error_a = deserialize (stream_a);
}

bool rai::landing_store::deserialize (std::istream & stream_a)
{
	bool result;
	try
	{
		boost::property_tree::ptree tree;
		boost::property_tree::read_json (stream_a, tree);
		auto source_l (tree.get <std::string> ("source"));
		auto destination_l (tree.get <std::string> ("destination"));
		auto start_l (tree.get <std::string> ("start"));
		auto last_l (tree.get <std::string> ("last"));
		result = source.decode_base58check (source_l);
		if (!result)
		{
			result = destination.decode_base58check (destination_l);
			if (!result)
			{
				start = std::stoull (start_l);
				last = std::stoull (last_l);
			}
		}
	}
	catch (std::logic_error const &)
	{
		result = true;
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

void rai::landing_store::serialize (std::ostream & stream_a) const
{
	boost::property_tree::ptree tree;
	tree.put ("source", source.to_base58check ());
	tree.put ("destination", destination.to_base58check ());
	tree.put ("start", std::to_string (start));
	tree.put ("last", std::to_string (last));
	boost::property_tree::write_json (stream_a, tree);
}

bool rai::landing_store::operator == (rai::landing_store const & other_a) const
{
	return source == other_a.source && destination == other_a.destination && start == other_a.start && last == other_a.last;
}

rai::landing::landing (rai::node & node_a, std::shared_ptr <rai::wallet> wallet_a, rai::landing_store & store_a, boost::filesystem::path const & path_a) :
path (path_a),
store (store_a),
wallet (wallet_a),
node (node_a)
{
}

void rai::landing::write_store ()
{
	std::ofstream store_file;
	store_file.open (path.string ());
	if (!store_file.fail ())
	{
		store.serialize (store_file);
	}
	else
	{
		std::stringstream str;
		store.serialize (str);
		BOOST_LOG (node.log) << boost::str (boost::format ("Error writing store file %1%") % str.str ());
	}
}

rai::uint128_t rai::landing::distribution_amount (uint64_t interval)
{
	// Halfing period ~= Exponent of 2 in secounds approixmately 1 year = 2^25 = 33554432
	// Interval = Exponent of 2 in seconds approximately 1 minute = 2^6 = 64
	uint64_t intervals_per_period (1 << (25 - interval_exponent));
	rai::uint128_t result;
	if (interval < intervals_per_period * 1)
	{
		// Total supply / 2^halfing period / intervals per period
		// 2^128 / 2^1 / (2^25 / 2^6)
		result = rai::uint128_t (1) << (127 - (25 - interval_exponent)); // 50%
	}
	else if (interval < intervals_per_period * 2)
	{
		result = rai::uint128_t (1) << (126 - (25 - interval_exponent)); // 25%
	}
	else if (interval < intervals_per_period * 3)
	{
		result = rai::uint128_t (1) << (125 - (25 - interval_exponent)); // 13%
	}
	else if (interval < intervals_per_period * 4)
	{
		result = rai::uint128_t (1) << (124 - (25 - interval_exponent)); // 6.3%
	}
	else if (interval < intervals_per_period * 5)
	{
		result = rai::uint128_t (1) << (123 - (25 - interval_exponent)); // 3.1%
	}
	else if (interval < intervals_per_period * 6)
	{
		result = rai::uint128_t (1) << (122 - (25 - interval_exponent)); // 1.6%
	}
	else if (interval < intervals_per_period * 7)
	{
		result = rai::uint128_t (1) << (121 - (25 - interval_exponent)); // 0.8%
	}
	else if (interval < intervals_per_period * 8)
	{
		result = rai::uint128_t (1) << (121 - (25 - interval_exponent)); // 0.8*
	}
	else
	{
		result = 0;
	}
	return result;
}

uint64_t rai::landing::seconds_since_epoch ()
{
	return std::chrono::duration_cast <std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

void rai::landing::distribute_one ()
{
	auto now (seconds_since_epoch ());
	auto error (false);
	while (!error && store.last + distribution_interval.count () < now)
	{
		auto amount (distribution_amount ((store.last - store.start) >> 6));
		error = wallet->send_sync (store.source, store.destination, amount);
		if (!error)
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Successfully distributed %1%") % amount);
			store.last += distribution_interval.count ();
			write_store ();
		}
		else
		{
			BOOST_LOG (node.log) << "Error while sending distribution";
		}
	}
}

void rai::landing::distribute_ongoing ()
{
	distribute_one ();
	BOOST_LOG (node.log) << "Waiting for next distribution cycle";
	node.service.add (std::chrono::system_clock::now () + sleep_seconds, [this] () {distribute_ongoing ();});
}

rai::thread_runner::thread_runner (boost::asio::io_service & service_a, rai::processor_service & processor_a)
{
	auto count (std::max <unsigned> (4, std::thread::hardware_concurrency ()));
	for (auto i (0); i < count; ++i)
	{
		threads.push_back (std::thread ([&service_a] ()
		{
			try
			{
				service_a.run ();
			}
			catch (...)
			{
				assert (false && "Unhandled service exception");
			}
		}));
	}
	for (auto i (0); i < count; ++i)
	{
		threads.push_back (std::thread ([&processor_a] ()
		{
			try
			{
				processor_a.run ();
			}
			catch (...)
			{
				assert (false && "Unhandled processor exception");
			}
		}));
	}
}

void rai::thread_runner::join ()
{
	for (auto &i : threads)
	{
		i.join ();
	}
}

std::chrono::seconds constexpr rai::landing::distribution_interval;
std::chrono::seconds constexpr rai::landing::sleep_seconds;
