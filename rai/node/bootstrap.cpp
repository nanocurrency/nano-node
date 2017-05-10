#include <rai/node/bootstrap.hpp>

#include <rai/node/common.hpp>
#include <rai/node/node.hpp>

#include <boost/log/trivial.hpp>

rai::block_synchronization::block_synchronization (boost::log::sources::logger_mt & log_a) :
log (log_a)
{
}

rai::block_synchronization::~block_synchronization ()
{
}

namespace {
class add_dependency_visitor : public rai::block_visitor
{
public:
    add_dependency_visitor (MDB_txn * transaction_a, rai::block_synchronization & sync_a) :
	transaction (transaction_a),
    sync (sync_a),
    complete (true)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
        if (complete)
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
        if (!sync.synchronized (transaction, hash_a) && sync.retrieve (transaction, hash_a) != nullptr)
        {
            complete = false;
            sync.blocks.push_back (hash_a);
        }
		else
		{
			// Block is already synchronized, normal
		}
    }
	MDB_txn * transaction;
    rai::block_synchronization & sync;
    bool complete;
};
}

bool rai::block_synchronization::add_dependency (MDB_txn * transaction_a, rai::block const & block_a)
{
    add_dependency_visitor visitor (transaction_a, *this);
    block_a.visit (visitor);
    return visitor.complete;
}

void rai::block_synchronization::fill_dependencies (MDB_txn * transaction_a)
{
    auto done (false);
    while (!done)
    {
		auto hash (blocks.back ());
        auto block (retrieve (transaction_a, hash));
        if (block != nullptr)
        {
            done = add_dependency (transaction_a, *block);
        }
        else
        {
            done = true;
        }
    }
}

rai::sync_result rai::block_synchronization::synchronize_one (MDB_txn * transaction_a)
{
	// Blocks that depend on multiple paths e.g. receive_blocks, need to have their dependencies recalculated each time
    fill_dependencies (transaction_a);
	rai::sync_result result (rai::sync_result::success);
	auto hash (blocks.back ());
	blocks.pop_back ();
	auto block (retrieve (transaction_a, hash));
	if (block != nullptr)
	{
		result = target (transaction_a, *block);
	}
	else
	{
		// A block that can be the dependency of more than one other block, e.g. send blocks, can be added to the dependency list more than once.  Subsequent retrievals won't find the block but this isn't an error
	}
    return result;
}

rai::sync_result rai::block_synchronization::synchronize (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
    auto result (rai::sync_result::success);
	blocks.clear ();
    blocks.push_back (hash_a);
	unsigned block_count (rai::blocks_per_transaction);
    while (block_count > 0 && result != rai::sync_result::fork && !blocks.empty ())
    {
        result = synchronize_one (transaction_a);
		--block_count;
    }
    return result;
}

rai::pull_synchronization::pull_synchronization (rai::node & node_a, std::shared_ptr <rai::bootstrap_attempt> attempt_a) :
block_synchronization (node_a.log),
node (node_a),
attempt (attempt_a)
{
}

std::unique_ptr <rai::block> rai::pull_synchronization::retrieve (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
    return node.store.unchecked_get (transaction_a, hash_a);
}

rai::sync_result rai::pull_synchronization::target (MDB_txn * transaction_a, rai::block const & block_a)
{
	auto result (rai::sync_result::error);
	node.process_receive_many (transaction_a, block_a, [this, transaction_a, &result] (rai::process_return result_a, rai::block const & block_a)
	{
		this->node.store.unchecked_del (transaction_a, block_a.hash ());
		switch (result_a.code)
		{
			case rai::process_result::progress:
			case rai::process_result::old:
				result = rai::sync_result::success;
				break;
			case rai::process_result::fork:
			{
				result = rai::sync_result::fork;
				auto node_l (this->node.shared ());
				auto block (node_l->ledger.forked_block (transaction_a, block_a));
				auto attempt_l (attempt);
				node_l->active.start (transaction_a, *block, [node_l, attempt_l] (rai::block & block_a)
				{
					node_l->process_confirmed (block_a);
					// Resume synchronizing after fork resolution
					assert (node_l->bootstrap_initiator.in_progress ());
					node_l->process_unchecked (attempt_l);
				});
				this->node.network.broadcast_confirm_req (block_a);
				this->node.network.broadcast_confirm_req (*block);
				BOOST_LOG (log) << boost::str (boost::format ("Fork received in bootstrap between: %1% and %2% root %3%") % block_a.hash ().to_string () % block->hash ().to_string () % block_a.root ().to_string ());
				break;
			}
			case rai::process_result::gap_previous:
			case rai::process_result::gap_source:
				result = rai::sync_result::error;
				if (this->node.config.logging.bulk_pull_logging ())
				{
					// Any activity while bootstrapping can cause gaps so these aren't as noteworthy
					BOOST_LOG (log) << boost::str (boost::format ("Gap received in bootstrap for block: %1%") % block_a.hash ().to_string ());
				}
				break;
			default:
				result = rai::sync_result::error;
				BOOST_LOG (log) << boost::str (boost::format ("Error inserting block in bootstrap: %1%") % block_a.hash ().to_string ());
				break;
		}
	});
	return result;
}

bool rai::pull_synchronization::synchronized (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
    return node.store.block_exists (transaction_a, hash_a);
}

rai::push_synchronization::push_synchronization (rai::node & node_a, std::function <rai::sync_result (MDB_txn *, rai::block const &)> const & target_a) :
block_synchronization (node_a.log),
target_m (target_a),
node (node_a)
{
}

bool rai::push_synchronization::synchronized (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
    auto result (!node.store.unsynced_exists (transaction_a, hash_a));
	if (!result)
	{
		node.store.unsynced_del (transaction_a, hash_a);
	}
	return result;
}

std::unique_ptr <rai::block> rai::push_synchronization::retrieve (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
    return node.store.block_get (transaction_a, hash_a);
}

rai::sync_result rai::push_synchronization::target (MDB_txn * transaction_a, rai::block const & block_a)
{
	return target_m (transaction_a, block_a);
}

rai::bootstrap_client::bootstrap_client (std::shared_ptr <rai::node> node_a, std::shared_ptr <rai::bootstrap_attempt> attempt_a, rai::tcp_endpoint const & endpoint_a) :
node (node_a),
attempt (attempt_a),
socket (node_a->network.service),
connected (false),
pull_client (*this),
endpoint (endpoint_a)
{
}

rai::bootstrap_client::~bootstrap_client ()
{
	if (node->config.logging.network_logging ())
	{
		BOOST_LOG (node->log) << boost::str (boost::format ("Exiting bootstrap client to %1%") % endpoint);
	}
	attempt->connection_ending (this);
}

void rai::bootstrap_client::run ()
{
    if (node->config.logging.network_logging ())
    {
        BOOST_LOG (node->log) << boost::str (boost::format ("Initiating bootstrap connection to %1%") % endpoint);
    }
    auto this_l (shared_from_this ());
    socket.async_connect (endpoint, [this_l] (boost::system::error_code const & ec)
    {
		if (!ec)
		{
			BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Connection established to %1%") % this_l->endpoint);
			this_l->connected = true;
			this_l->attempt->pool_connection (this_l);
		}
		else
		{
			if (this_l->node->config.logging.network_logging ())
			{
				BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error initiating bootstrap connection to %2%: %1%") % ec.message () % this_l->endpoint);
			}
		}
    });
	std::weak_ptr <rai::bootstrap_client> this_w (this_l);
	node->alarm.add (std::chrono::system_clock::now () + std::chrono::seconds(5), [this_w] ()
	{
		auto this_l (this_w.lock ());
		if (this_l != nullptr)
		{
			if (!this_l->connected)
			{
				BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Bootstrap disconnecting from: %1% because because of connection timeout") % this_l->endpoint);
				this_l->socket.close ();
			}
		}
	});
}

void rai::bootstrap_client::frontier_request ()
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

std::shared_ptr <rai::bootstrap_client> rai::bootstrap_client::shared ()
{
	return shared_from_this ();
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
    boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->received_frontier (ec, size_a);
    });
}

void rai::frontier_req_client::request_account (rai::account const & account_a, rai::block_hash const & latest_a)
{
    // Account they know about and we don't.
    connection->attempt->pulls.push_back (rai::pull_info (account_a, latest_a, rai::block_hash (0)));
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
        rai::bufferstream account_stream (connection->receive_buffer.data (), sizeof (rai::uint256_union));
        auto error1 (rai::read (account_stream, account));
        assert (!error1);
        rai::block_hash latest;
        rai::bufferstream latest_stream (connection->receive_buffer.data () + sizeof (rai::uint256_union), sizeof (rai::uint256_union));
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
							connection->attempt->pulls.push_back (rai::pull_info (account, latest, info.head));
						}
					}
					next ();
                }
                else
                {
                    assert (account < current);
                    request_account (account, latest);
                }
            }
            else
            {
                request_account (account, latest);
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
            connection->attempt->completed_requests (connection);
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

void rai::bulk_pull_client::request (rai::pull_info const & pull_a)
{
	pull = pull_a;
	expected = pull_a.head;
	rai::bulk_pull req;
	req.start = pull_a.account;
	req.end = pull_a.end;
	auto buffer (std::make_shared <std::vector <uint8_t>> ());
	{
		rai::vectorstream stream (*buffer);
		req.serialize (stream);
	}
	if (connection.node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (connection.node->log) << boost::str (boost::format ("Requesting account %1% down to %2% from %3%") % req.start.to_account () % req.end.to_string () % connection.endpoint);
	}
	else if (connection.node->config.logging.network_logging () && account_count % 256 == 0)
	{
		BOOST_LOG (connection.node->log) << boost::str (boost::format ("Requesting account %1% down to %2% from %3%") % req.start.to_account () % req.end.to_string () % connection.endpoint);
	}
	++account_count;
	auto connection_l (connection.shared ());
	boost::asio::async_write (connection.socket, boost::asio::buffer (buffer->data (), buffer->size ()), [connection_l, buffer] (boost::system::error_code const & ec, size_t size_a)
	{
		if (!ec)
		{
			connection_l->pull_client.receive_block ();
		}
		else
		{
			BOOST_LOG (connection_l->node->log) << boost::str (boost::format ("Error sending bulk pull request %1% to %2%") % ec.message () % connection_l->endpoint);
		}
	});
}

void rai::bulk_pull_client::receive_block ()
{
    auto connection_l (connection.shared ());
    boost::asio::async_read (connection.socket, boost::asio::buffer (connection.receive_buffer.data (), 1), [connection_l] (boost::system::error_code const & ec, size_t size_a)
    {
        if (!ec)
        {
            connection_l->pull_client.received_type ();
        }
        else
        {
            BOOST_LOG (connection_l->node->log) << boost::str (boost::format ("Error receiving block type %1%") % ec.message ());
        }
    });
}

void rai::bulk_pull_client::received_type ()
{
    auto connection_l (connection.shared ());
    rai::block_type type (static_cast <rai::block_type> (connection.receive_buffer [0]));
    switch (type)
    {
        case rai::block_type::send:
        {
            boost::asio::async_read (connection.socket, boost::asio::buffer (connection.receive_buffer.data () + 1, rai::send_block::size), [connection_l] (boost::system::error_code const & ec, size_t size_a)
            {
                connection_l->pull_client.received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::receive:
        {
            boost::asio::async_read (connection.socket, boost::asio::buffer (connection.receive_buffer.data () + 1, rai::receive_block::size), [connection_l] (boost::system::error_code const & ec, size_t size_a)
            {
                connection_l->pull_client.received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::open:
        {
            boost::asio::async_read (connection.socket, boost::asio::buffer (connection.receive_buffer.data () + 1, rai::open_block::size), [connection_l] (boost::system::error_code const & ec, size_t size_a)
            {
                connection_l->pull_client.received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::change:
        {
            boost::asio::async_read (connection.socket, boost::asio::buffer (connection.receive_buffer.data () + 1, rai::change_block::size), [connection_l] (boost::system::error_code const & ec, size_t size_a)
            {
                connection_l->pull_client.received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::not_a_block:
        {
            connection.attempt->completed_pull (connection.shared ());
            break;
        }
        default:
        {
            BOOST_LOG (connection.node->log) << boost::str (boost::format ("Unknown type received as block type: %1%") % static_cast <int> (type));
            break;
        }
    }
}

void rai::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		rai::bufferstream stream (connection.receive_buffer.data (), 1 + size_a);
		auto block (rai::deserialize_block (stream));
		if (block != nullptr)
		{
            auto hash (block->hash ());
            if (connection.node->config.logging.bulk_pull_logging ())
            {
                std::string block_l;
                block->serialize_json (block_l);
                BOOST_LOG (connection.node->log) << boost::str (boost::format ("Pulled block %1% %2%") % hash.to_string () % block_l);
            }
			if (hash == expected)
			{
				expected = block->previous ();
			}
			auto already_exists (false);
			{
				rai::transaction transaction (connection.node->store.environment, nullptr, false);
				already_exists = connection.node->store.block_exists (transaction, hash);
			}
			if (!already_exists)
			{
				connection.attempt->cache.add_block (std::move (block));
			}
            receive_block ();
		}
        else
        {
            BOOST_LOG (connection.node->log) << "Error deserializing block received from pull request";
        }
	}
	else
	{
		BOOST_LOG (connection.node->log) << boost::str (boost::format ("Error bulk receiving block: %1%") % ec.message ());
	}
}

rai::bulk_pull_client::bulk_pull_client (rai::bootstrap_client & connection_a) :
connection (connection_a),
account_count (0)
{
}

rai::bulk_pull_client::~bulk_pull_client ()
{
}

rai::bulk_push_client::bulk_push_client (std::shared_ptr <rai::bootstrap_client> const & connection_a) :
connection (connection_a),
synchronization (*connection->node, [this] (MDB_txn * transaction_a, rai::block const & block_a)
{
    push_block (block_a);
	return rai::sync_result::success;
})
{
}

rai::bulk_push_client::~bulk_push_client ()
{
    if (connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->node->log) << "Exiting bulk push client";
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
    boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer] (boost::system::error_code const & ec, size_t size_a)
	{
		rai::transaction transaction (this_l->connection->node->store.environment, nullptr, true);
		if (!ec)
		{
			this_l->push (transaction);
		}
		else
		{
			BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Unable to send bulk_push request %1%") % ec.message ());
		}
	});
}

void rai::bulk_push_client::push (MDB_txn * transaction_a)
{
	auto finished (false);
	{
		auto first (connection->node->store.unsynced_begin (transaction_a));
		if (first != rai::store_iterator (nullptr))
		{
			rai::block_hash hash (first->first);
			if (!hash.is_zero ())
			{
				connection->node->store.unsynced_del (transaction_a, hash);
				synchronization.blocks.push_back (hash);
				synchronization.synchronize_one (transaction_a);
			}
			else
			{
				finished = true;
			}
		}
		else
		{
			finished = true;
		}
	}
	if (finished)
    {
        send_finished ();
    }
}

void rai::bulk_push_client::send_finished ()
{
    auto buffer (std::make_shared <std::vector <uint8_t>> ());
    buffer->push_back (static_cast <uint8_t> (rai::block_type::not_a_block));
    if (connection->node->config.logging.network_logging ())
    {
        BOOST_LOG (connection->node->log) << "Bulk push finished";
    }
    auto this_l (shared_from_this ());
    async_write (connection->socket, boost::asio::buffer (buffer->data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
	{
		this_l->connection->attempt->completed_pushes (this_l->connection);
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
    boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer] (boost::system::error_code const & ec, size_t size_a)
	{
		if (!ec)
		{
			rai::transaction transaction (this_l->connection->node->store.environment, nullptr, true);
			if (!this_l->synchronization.blocks.empty ())
			{
				this_l->synchronization.synchronize_one (transaction);
			}
			else
			{
				this_l->push (transaction);
			}
		}
		else
		{
			BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error sending block during bulk push %1%") % ec.message ());
		}
	});
}

rai::bootstrap_pull_cache::bootstrap_pull_cache (rai::bootstrap_attempt & attempt_a) :
attempt (attempt_a)
{
}

void rai::bootstrap_pull_cache::add_block (std::unique_ptr <rai::block> block_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	blocks.emplace_back (std::move (block_a));
}

void rai::bootstrap_pull_cache::flush (size_t minimum_a)
{
	decltype (blocks) blocks_l;
	{
		std::lock_guard <std::mutex> lock (mutex);
		if (blocks.size () > minimum_a)
		{
			blocks.swap (blocks_l);
		}
	}
	if (!blocks_l.empty ())
	{
		while (!blocks_l.empty ())
		{
			auto count (0);
			while (!blocks_l.empty () && count < rai::blocks_per_transaction)
			{
				rai::transaction transaction (attempt.node->store.environment, nullptr, true);
				auto & front (blocks_l.front ());
				attempt.node->store.unchecked_put (transaction, front->hash(), *front);
				blocks_l.pop_front ();
				++count;
			}
		}
	}
}

rai::pull_info::pull_info () :
account (0),
end (0),
attempts (0)
{
}

rai::pull_info::pull_info (rai::account const & account_a, rai::block_hash const & head_a, rai::block_hash const & end_a) :
account (account_a),
head (head_a),
end (end_a),
attempts (0)
{
}

rai::bootstrap_attempt::bootstrap_attempt (std::shared_ptr <rai::node> node_a) :
node (node_a),
cache (*this),
state (rai::attempt_state::starting)
{
}

rai::bootstrap_attempt::~bootstrap_attempt ()
{
	node->bootstrap_initiator.notify_listeners ();
	BOOST_LOG (node->log) << "Exiting bootstrap attempt";
}

void rai::bootstrap_attempt::populate_connections ()
{
	std::weak_ptr <rai::bootstrap_attempt> this_w (shared_from_this ());
	std::shared_ptr <rai::bootstrap_client> client;
	{
		std::lock_guard <std::mutex> lock (mutex);
		if (connecting.size () + active.size () + idle.size () < node->config.bootstrap_connections)
		{
			auto peer (node->peers.bootstrap_peer ());
			if (peer != rai::endpoint ())
			{
				client = start_connection (peer);
			}
		}
		switch (state)
		{
			case rai::attempt_state::starting:
			case rai::attempt_state::requesting_frontiers:
			case rai::attempt_state::requesting_pulls:
				node->alarm.add (std::chrono::system_clock::now () + std::chrono::seconds (1), [this_w] ()
				{
					if (auto this_l = this_w.lock ())
					{
						this_l->populate_connections ();
					}
				});
				break;
			default:
				break;
		}
	}
}

void rai::bootstrap_attempt::add_connection (rai::endpoint const & endpoint_a)
{
	std::shared_ptr <rai::bootstrap_client> client;
	{
		std::lock_guard <std::mutex> lock (mutex);
		client = start_connection (endpoint_a);
	}
}

std::shared_ptr <rai::bootstrap_client> rai::bootstrap_attempt::start_connection (rai::endpoint const & endpoint_a)
{
	assert (!mutex.try_lock ());
	std::shared_ptr <rai::bootstrap_client> client;
	if (attempted.find (endpoint_a) == attempted.end ())
	{
		attempted.insert (endpoint_a);
		auto node_l (node->shared ());
		client = std::make_shared <rai::bootstrap_client> (node_l, shared_from_this (), rai::tcp_endpoint (endpoint_a.address (), endpoint_a.port ()));
		connecting [client.get ()] = client;
		client->run ();
	}
	return client;
}

void rai::bootstrap_attempt::stop ()
{
	std::lock_guard <std::mutex> lock (mutex);
	state = rai::attempt_state::complete;
	for (auto i: connecting)
	{
		auto attempt (i.second.lock ());
		if (attempt != nullptr)
		{
			attempt->socket.close ();
		}
	}
	for (auto i: active)
	{
		auto attempt (i.second.lock ());
		if (attempt != nullptr)
		{
			attempt->socket.close ();
		}
	}
	idle.clear ();
}

void rai::bootstrap_attempt::pool_connection (std::shared_ptr <rai::bootstrap_client> client_a)
{
	{
		std::lock_guard <std::mutex> lock (mutex);
		auto erased_active (active.erase (client_a.get ()));
		auto erased_connecting (connecting.erase (client_a.get ()));
		assert (erased_active == 1 || erased_connecting == 1);
		idle.push_back (client_a);
	}
	dispatch_work ();
}

void rai::bootstrap_attempt::connection_ending (rai::bootstrap_client * client_a)
{
	if (state != rai::attempt_state::complete)
	{
		std::lock_guard <std::mutex> lock (mutex);
		if (!client_a->pull_client.pull.account.is_zero ())
		{
			// If this connection is ending and request_account hasn't been cleared it didn't finish, requeue
			requeue_pull (client_a->pull_client.pull);
		}
		auto erased_connecting (connecting.erase (client_a));
		auto erased_active (active.erase (client_a));
	}
}

void rai::bootstrap_attempt::completed_requests (std::shared_ptr <rai::bootstrap_client> client_a)
{
	{
		std::lock_guard <std::mutex> lock (mutex);
		if (node->config.logging.network_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Completed frontier request, %1% out of sync accounts according to %2%") % pulls.size () % client_a->endpoint);
		}
		state = rai::attempt_state::requesting_pulls;
	}
	pool_connection (client_a);
}

void rai::bootstrap_attempt::completed_pull (std::shared_ptr <rai::bootstrap_client> client_a)
{
	auto repool (true);
	{
		std::lock_guard <std::mutex> lock (mutex);
		if (client_a->pull_client.expected != client_a->pull_client.pull.end)
		{
			requeue_pull (client_a->pull_client.pull);
			BOOST_LOG (node->log) << boost::str (boost::format ("Disconnecting from %1% because it didn't give us what we requested") % client_a->endpoint);
			repool = false;
		}
		client_a->pull_client.pull = rai::pull_info ();
	}
	if (repool)
	{
		pool_connection (client_a);
	}
	cache.flush (cache.block_count);
}

void rai::bootstrap_attempt::completed_pulls (std::shared_ptr <rai::bootstrap_client> client_a)
{
	BOOST_LOG (node->log) << "Completed pulls";
	assert (node->bootstrap_initiator.in_progress ());
	cache.flush (0);
	node->process_unchecked (shared_from_this ());
    auto pushes (std::make_shared <rai::bulk_push_client> (client_a));
    pushes->start ();
}

void rai::bootstrap_attempt::completed_pushes (std::shared_ptr <rai::bootstrap_client> client_a)
{
	std::vector <std::shared_ptr <rai::bootstrap_client>> discard;
	std::lock_guard <std::mutex> lock (mutex);
	state = rai::attempt_state::complete;
	discard.swap (idle);
}

void rai::bootstrap_attempt::dispatch_work ()
{
	std::function <void ()> action;
	{
		std::lock_guard <std::mutex> lock (mutex);
		if (!idle.empty ())
		{
			// We have a connection we could do something with
			auto connection (idle.back ());
			switch (state)
			{
				case rai::attempt_state::starting:
					state = rai::attempt_state::requesting_frontiers;
					action = [connection, this] ()
					{
						if (this->node->config.logging.network_logging ())
						{
							BOOST_LOG (this->node->log) << boost::str (boost::format ("Initiating frontier request"));
						}
						connection->frontier_request ();
					};
					break;
				case rai::attempt_state::requesting_frontiers:
					break;
				case rai::attempt_state::requesting_pulls:
					if (!pulls.empty ())
					{
						// There are more things to pull
						auto pull (pulls.back ());
						pulls.pop_back ();
						action = [connection, pull] ()
						{
							connection->pull_client.request (pull);
						};
					}
					else
					{
						if (active.empty ())
						{
							// No one else is still running, we're done with pulls
							state = rai::attempt_state::pushing;
							action = [this, connection] ()
							{
								completed_pulls (connection);
							};
						}
						else
						{
							// Drop this connection
							break;
						}
					}
					break;
				case rai::attempt_state::pushing:
				case rai::attempt_state::complete:
					// Drop this connection
					break;
			};
			if (action)
			{
				// If there's an action, move the connection from idle to active.
				active [connection.get ()] = connection;
				idle.pop_back ();
			}
		}
	}
	if (action)
	{
		action ();
		dispatch_work ();
	}
}

void rai::bootstrap_attempt::requeue_pull (rai::pull_info const & pull_a)
{
	auto pull (pull_a);
	if (++pull.attempts < 16)
	{
		pulls.push_front (pull);
	}
	else
	{
		BOOST_LOG (node->log) << boost::str (boost::format ("Failed to pull account %1% down to %2% after %3% attempts") % pull.account.to_account () % pull.end.to_string () % pull.attempts);
	}
}

rai::bootstrap_initiator::bootstrap_initiator (rai::node & node_a) :
node (node_a),
stopped (false)
{
}

void rai::bootstrap_initiator::bootstrap ()
{
	std::lock_guard <std::mutex> lock (mutex);
	if (attempt.lock () == nullptr && !stopped)
	{
		auto attempt_l (std::make_shared <rai::bootstrap_attempt> (node.shared ()));
		attempt = attempt_l;
		attempt_l->populate_connections ();
	}
}

void rai::bootstrap_initiator::bootstrap (rai::endpoint const & endpoint_a)
{
	bootstrap ();
	std::lock_guard <std::mutex> lock (mutex);
	if (auto attempt_l = attempt.lock ())
	{
		if (!stopped)
		{
			attempt_l->add_connection (endpoint_a);
		}
	}
}

void rai::bootstrap_initiator::add_observer (std::function <void (bool)> const & observer_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	observers.push_back (observer_a);
}

bool rai::bootstrap_initiator::in_progress ()
{
	return attempt.lock () != nullptr;
}

void rai::bootstrap_initiator::stop ()
{
	std::lock_guard <std::mutex> lock (mutex);
	stopped = true;
	auto attempt_l (attempt.lock ());
	if (attempt_l != nullptr)
	{
		attempt_l->stop ();
	}
}

void rai::bootstrap_initiator::notify_listeners ()
{
	auto in_progress_l (in_progress ());
	for (auto & i: observers)
	{
		i (in_progress_l);
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
            if (node->config.logging.bulk_pull_logging ())
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
			if (node->config.logging.bulk_pull_logging ())
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
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Request for unknown account: %1%") % request->start.to_account ());
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
    if (connection->node->config.logging.bulk_pull_logging ())
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

rai::bulk_pull_server::bulk_pull_server (std::shared_ptr <rai::bootstrap_server> const & connection_a, std::unique_ptr <rai::bulk_pull> request_a) :
connection (connection_a),
request (std::move (request_a))
{
    set_current_end ();
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
			if (!connection->node->bootstrap_initiator.in_progress ())
			{
				connection->node->process_receive_republish (std::move (block));
			}
            receive ();
        }
        else
        {
            BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
        }
    }
}

rai::frontier_req_server::frontier_req_server (std::shared_ptr <rai::bootstrap_server> const & connection_a, std::unique_ptr <rai::frontier_req> request_a) :
connection (connection_a),
current (request_a->start.number () - 1),
info (0, 0, 0, 0, 0, 0),
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
        if (connection->node->config.logging.bulk_pull_logging ())
        {
            BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending frontier for %1% %2%") % current.to_account () % info.head.to_string ());
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
