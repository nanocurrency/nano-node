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

namespace
{
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
	auto cutoff (std::chrono::system_clock::now () + rai::transaction_timeout);
	while (std::chrono::system_clock::now () < cutoff && result != rai::sync_result::fork && !blocks.empty ())
	{
		result = synchronize_one (transaction_a);
	}
	return result;
}

rai::push_synchronization::push_synchronization (rai::node & node_a, std::function<rai::sync_result (MDB_txn *, rai::block const &)> const & target_a) :
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

std::unique_ptr<rai::block> rai::push_synchronization::retrieve (MDB_txn * transaction_a, rai::block_hash const & hash_a)
{
	return node.store.block_get (transaction_a, hash_a);
}

rai::sync_result rai::push_synchronization::target (MDB_txn * transaction_a, rai::block const & block_a)
{
	return target_m (transaction_a, block_a);
}

rai::bootstrap_client::bootstrap_client (std::shared_ptr<rai::node> node_a, std::shared_ptr<rai::bootstrap_attempt> attempt_a, rai::tcp_endpoint const & endpoint_a) :
node (node_a),
attempt (attempt_a),
socket (node_a->service),
endpoint (endpoint_a),
timeout (node_a->service)
{
	++attempt->connections;
}

rai::bootstrap_client::~bootstrap_client ()
{
	--attempt->connections;
}

void rai::bootstrap_client::start_timeout ()
{
	timeout.expires_from_now (boost::posix_time::seconds (15));
	std::weak_ptr<rai::bootstrap_client> this_w (shared ());
	timeout.async_wait ([this_w](boost::system::error_code const & ec) {
		if (ec != boost::asio::error::operation_aborted)
		{
			auto this_l (this_w.lock ());
			if (this_l != nullptr)
			{
				this_l->socket.close ();
				if (this_l->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Disconnecting from %1% due to timeout") % this_l->endpoint);
				}
			}
		}
	});
}

void rai::bootstrap_client::stop_timeout ()
{
	size_t killed (timeout.cancel ());
	(void)killed;
}

void rai::bootstrap_client::run ()
{
	auto this_l (shared_from_this ());
	start_timeout ();
	socket.async_connect (endpoint, [this_l](boost::system::error_code const & ec) {
		this_l->stop_timeout ();
		if (!ec)
		{
			BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Connection established to %1%") % this_l->endpoint);
			this_l->attempt->pool_connection (this_l->shared_from_this ());
		}
		else
		{
			if (this_l->node->config.logging.network_logging ())
			{
				switch (ec.value ())
				{
					default:
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error initiating bootstrap connection to %2%: %1%") % ec.message () % this_l->endpoint);
						break;
					case boost::system::errc::connection_refused:
					case boost::system::errc::operation_canceled:
					case boost::system::errc::timed_out:
						break;
				}
			}
		}
	});
}

void rai::frontier_req_client::run ()
{
	std::unique_ptr<rai::frontier_req> request (new rai::frontier_req);
	request->start.clear ();
	request->age = std::numeric_limits<decltype (request->age)>::max ();
	request->count = std::numeric_limits<decltype (request->age)>::max ();
	auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		rai::vectorstream stream (*send_buffer);
		request->serialize (stream);
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l, send_buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		if (!ec)
		{
			this_l->receive_frontier ();
		}
		else
		{
			if (this_l->connection->node->config.logging.network_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ());
			}
		}
	});
}

std::shared_ptr<rai::bootstrap_client> rai::bootstrap_client::shared ()
{
	return shared_from_this ();
}

rai::frontier_req_client::frontier_req_client (std::shared_ptr<rai::bootstrap_client> connection_a) :
connection (connection_a),
current (0),
count (0),
landing ("059F68AAB29DE0D3A27443625C7EA9CDDB6517A8B76FE37727EF6A4D76832AD5"),
faucet ("8E319CE6F3025E5B2DF66DA7AB1467FE48F1679C13DD43BFDB29FA2E9FC40D3B"),
next_report (std::chrono::system_clock::now () + std::chrono::seconds (15))
{
	rai::transaction transaction (connection->node->store.environment, nullptr, false);
	next (transaction);
}

rai::frontier_req_client::~frontier_req_client ()
{
}

void rai::frontier_req_client::receive_frontier ()
{
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		this_l->received_frontier (ec, size_a);
	});
}

void rai::frontier_req_client::request_account (rai::account const & account_a, rai::block_hash const & latest_a)
{
	// Account they know about and we don't.
	rai::account account_1 ("6B31E80CABDD2FEE6F54A7BDBF91B666010418F4438EF0B48168F93CD79DBC85"); // xrb_1tsjx18cqqbhxsqobbxxqyauesi31iehaiwgy4ta4t9s9mdsuh671npo1st9
	rai::account account_2 ("FD6EE9E0E107A6A8584DB94A3F154799DD5C2A7D6ABED0889DA3B837B0E61663"); // xrb_3zdgx9ig43x8o3e6ugcc9wcnh8gxdio9ttoyt46buaxr8yrge7m5331qdwhk
	if (account_a != landing && account_a != faucet && account_a != account_1 && account_a != account_2)
	{
		insert_pull (rai::pull_info (account_a, latest_a, rai::block_hash (0)));
	}
	else
	{
		connection->attempt->pulls.push_front (rai::pull_info (account_a, latest_a, rai::block_hash (0)));
	}
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
		++count;
		auto now (std::chrono::system_clock::now ());
		if (next_report < now)
		{
			next_report = now + std::chrono::seconds (15);
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Received %1% frontiers from %2%") % std::to_string (count) % connection->socket.remote_endpoint ());
		}
		if (!account.is_zero ())
		{
			while (!current.is_zero () && current < account)
			{
				// We know about an account they don't.
				rai::transaction transaction (connection->node->store.environment, nullptr, true);
				if (connection->node->wallets.exists (transaction, current))
				{
					unsynced (transaction, info.head, 0);
				}
				next (transaction);
			}
			if (!current.is_zero ())
			{
				if (account == current)
				{
					rai::transaction transaction (connection->node->store.environment, nullptr, true);
					if (latest == info.head)
					{
						// In sync
					}
					else
					{
						if (connection->node->store.block_exists (transaction, latest))
						{
							// We know about a block they don't.
							if (connection->node->wallets.exists (transaction, current))
							{
								unsynced (transaction, info.head, latest);
							}
						}
						else
						{
							// They know about a block we don't.
							if (account != rai::genesis_account && account != landing && account != faucet)
							{
								insert_pull (rai::pull_info (account, latest, info.head));
							}
							else
							{
								connection->attempt->pulls.push_front (rai::pull_info (account, latest, info.head));
							}
						}
					}
					next (transaction);
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
					if (connection->node->wallets.exists (transaction, current))
					{
						unsynced (transaction, info.head, 0);
					}
					next (transaction);
				}
			}
			{
				try
				{
					promise.set_value (false);
				}
				catch (std::future_error &)
				{
				}
				connection->attempt->pool_connection (connection);
			}
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

void rai::frontier_req_client::insert_pull (rai::pull_info const & pull_a)
{
	connection->attempt->pulls.insert (connection->attempt->pulls.begin () + rai::random_pool.GenerateWord32 (0, connection->attempt->pulls.size ()), pull_a);
}

void rai::frontier_req_client::next (MDB_txn * transaction_a)
{
	auto iterator (connection->node->store.latest_begin (transaction_a, rai::uint256_union (current.number () + 1)));
	if (iterator != connection->node->store.latest_end ())
	{
		current = rai::account (iterator->first.uint256 ());
		info = rai::account_info (iterator->second);
	}
	else
	{
		current.clear ();
	}
}

rai::bulk_pull_client::bulk_pull_client (std::shared_ptr<rai::bootstrap_client> connection_a) :
connection (connection_a)
{
	assert (!connection->attempt->mutex.try_lock ());
	++connection->attempt->pulling;
	connection->attempt->condition.notify_all ();
}

rai::bulk_pull_client::~bulk_pull_client ()
{
	{
		std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
		--connection->attempt->pulling;
		connection->attempt->condition.notify_all ();
	}
	if (!pull.account.is_zero ())
	{
		connection->attempt->requeue_pull (pull);
	}
}

void rai::bulk_pull_client::request (rai::pull_info const & pull_a)
{
	pull = pull_a;
	expected = pull_a.head;
	rai::bulk_pull req;
	req.start = pull_a.account;
	req.end = pull_a.end;
	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		rai::vectorstream stream (*buffer);
		req.serialize (stream);
	}
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("Requesting account %1% from %2%") % req.start.to_account () % connection->endpoint);
	}
	else if (connection->node->config.logging.network_logging () && connection->attempt->account_count++ % 256 == 0)
	{
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("Requesting account %1% from %2%") % req.start.to_account () % connection->endpoint);
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		if (!ec)
		{
			this_l->receive_block ();
		}
		else
		{
			BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error sending bulk pull request %1% to %2%") % ec.message () % this_l->connection->endpoint);
		}
	});
}

void rai::bulk_pull_client::receive_block ()
{
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
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

void rai::bulk_pull_client::received_type ()
{
	auto this_l (shared_from_this ());
	rai::block_type type (static_cast<rai::block_type> (connection->receive_buffer[0]));
	switch (type)
	{
		case rai::block_type::send:
		{
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data () + 1, rai::send_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->connection->stop_timeout ();
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case rai::block_type::receive:
		{
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data () + 1, rai::receive_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->connection->stop_timeout ();
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case rai::block_type::open:
		{
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data () + 1, rai::open_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->connection->stop_timeout ();
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case rai::block_type::change:
		{
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data () + 1, rai::change_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->connection->stop_timeout ();
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case rai::block_type::not_a_block:
		{
			connection->attempt->pool_connection (connection);
			if (expected == pull.end)
			{
				pull = rai::pull_info ();
			}
			break;
		}
		default:
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unknown type received as block type: %1%") % static_cast<int> (type));
			break;
		}
	}
}

void rai::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		rai::bufferstream stream (connection->receive_buffer.data (), 1 + size_a);
		std::shared_ptr<rai::block> block (rai::deserialize_block (stream));
		if (block != nullptr)
		{
			auto hash (block->hash ());
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				std::string block_l;
				block->serialize_json (block_l);
				BOOST_LOG (connection->node->log) << boost::str (boost::format ("Pulled block %1% %2%") % hash.to_string () % block_l);
			}
			if (hash == expected)
			{
				expected = block->previous ();
			}
			auto attempt_l (connection->attempt);
			auto pull_l (pull);
			attempt_l->node->block_processor.add (rai::block_processor_item (block, [attempt_l, pull_l](MDB_txn * transaction_a, rai::process_return result_a, std::shared_ptr<rai::block> block_a) {
				switch (result_a.code)
				{
					case rai::process_result::progress:
					case rai::process_result::old:
						break;
					case rai::process_result::fork:
					{
						auto node_l (attempt_l->node);
						std::shared_ptr<rai::block> block (node_l->ledger.forked_block (transaction_a, *block_a));
						if (!node_l->active.start (transaction_a, block))
						{
							node_l->network.broadcast_confirm_req (block_a);
							node_l->network.broadcast_confirm_req (block);
							auto hash (block_a->hash ());
							attempt_l->requeue_pull (rai::pull_info (pull_l.account, hash, hash));
							BOOST_LOG (node_l->log) << boost::str (boost::format ("While bootstrappping, fork between our block: %2% and block %1% both with root %3%") % block_a->hash ().to_string () % block->hash ().to_string () % block_a->root ().to_string ());
						}
						break;
					}
					default:
						break;
				}
			}));
			receive_block ();
		}
		else
		{
			BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
		}
	}
	else
	{
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error bulk receiving block: %1%") % ec.message ());
	}
}

rai::bulk_push_client::bulk_push_client (std::shared_ptr<rai::bootstrap_client> const & connection_a) :
connection (connection_a),
synchronization (*connection->node, [this](MDB_txn * transaction_a, rai::block const & block_a) {
	push_block (block_a);
	return rai::sync_result::success;
})
{
}

rai::bulk_push_client::~bulk_push_client ()
{
}

void rai::bulk_push_client::start ()
{
	rai::bulk_push message;
	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		rai::vectorstream stream (*buffer);
		message.serialize (stream);
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
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
			rai::block_hash hash (first->first.uint256 ());
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
	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	buffer->push_back (static_cast<uint8_t> (rai::block_type::not_a_block));
	if (connection->node->config.logging.network_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk push finished";
	}
	auto this_l (shared_from_this ());
	async_write (connection->socket, boost::asio::buffer (buffer->data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		try
		{
			this_l->promise.set_value (false);
		}
		catch (std::future_error &)
		{
		}
	});
}

void rai::bulk_push_client::push_block (rai::block const & block_a)
{
	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		rai::vectorstream stream (*buffer);
		rai::serialize_block (stream, block_a);
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
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

rai::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<rai::node> node_a) :
connections (0),
pulling (0),
node (node_a),
account_count (0),
stopped (false)
{
	BOOST_LOG (node->log) << "Starting bootstrap attempt";
	node->bootstrap_initiator.notify_listeners (true);
}

rai::bootstrap_attempt::~bootstrap_attempt ()
{
	BOOST_LOG (node->log) << "Exiting bootstrap attempt";
	node->bootstrap_initiator.notify_listeners (false);
}

bool rai::bootstrap_attempt::request_frontier (std::unique_lock<std::mutex> & lock_a)
{
	auto result (true);
	auto connection_l (connection (lock_a));
	if (connection_l)
	{
		std::future<bool> future;
		{
			auto client (std::make_shared<rai::frontier_req_client> (connection_l));
			client->run ();
			frontiers = client;
			future = client->promise.get_future ();
		}
		lock_a.unlock ();
		result = consume_future (future);
		lock_a.lock ();
		if (result)
		{
			pulls.clear ();
		}
		if (node->config.logging.network_logging ())
		{
			if (!result)
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Completed frontier request, %1% out of sync accounts according to %2%") % pulls.size () % connection_l->endpoint);
			}
			else
			{
				BOOST_LOG (node->log) << "frontier_req failed, reattempting";
			}
		}
	}
	return result;
}

void rai::bootstrap_attempt::request_pull (std::unique_lock<std::mutex> & lock_a)
{
	auto connection_l (connection (lock_a));
	if (connection_l)
	{
		auto pull (pulls.front ());
		pulls.pop_front ();
		auto client (std::make_shared<rai::bulk_pull_client> (connection_l));
		// The bulk_pull_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
		// Dispatch request in an external thread in case it needs to be destroyed
		node->background ([client, pull]() {
			client->request (pull);
		});
	}
}

bool rai::bootstrap_attempt::request_push (std::unique_lock<std::mutex> & lock_a)
{
	auto result (true);
	auto connection_l (connection (lock_a));
	if (connection_l)
	{
		std::future<bool> future;
		{
			auto client (std::make_shared<rai::bulk_push_client> (connection_l));
			client->start ();
			push = client;
			future = client->promise.get_future ();
		}
		lock_a.unlock ();
		result = consume_future (future);
		lock_a.lock ();
		if (node->config.logging.network_logging ())
		{
			BOOST_LOG (node->log) << "Exiting bulk push client";
			if (result)
			{
				BOOST_LOG (node->log) << "Bulk push client failed";
			}
		}
	}
	return result;
}

bool rai::bootstrap_attempt::still_pulling ()
{
	assert (!mutex.try_lock ());
	auto running (!stopped);
	auto more_pulls (!pulls.empty ());
	auto still_pulling (pulling > 0);
	return running && (more_pulls || still_pulling);
}

void rai::bootstrap_attempt::run ()
{
	populate_connections ();
	std::unique_lock<std::mutex> lock (mutex);
	auto frontier_failure (true);
	while (!stopped && frontier_failure)
	{
		frontier_failure = request_frontier (lock);
	}
	while (still_pulling ())
	{
		while (still_pulling ())
		{
			if (!pulls.empty ())
			{
				request_pull (lock);
			}
			else
			{
				condition.wait (lock);
			}
		}
		// Flushing may resolve forks which can add more pulls
		BOOST_LOG (node->log) << "Flushing unchecked blocks";
		lock.unlock ();
		node->block_processor.flush ();
		lock.lock ();
		BOOST_LOG (node->log) << "Finished flushing unchecked blocks";
	}
	if (!stopped)
	{
		BOOST_LOG (node->log) << "Completed pulls";
	}
	auto push_failure (true);
	while (!stopped && push_failure)
	{
		push_failure = request_push (lock);
	}
	stopped = true;
	condition.notify_all ();
	idle.clear ();
}

std::shared_ptr<rai::bootstrap_client> rai::bootstrap_attempt::connection (std::unique_lock<std::mutex> & lock_a)
{
	while (!stopped && idle.empty ())
	{
		condition.wait (lock_a);
	}
	std::shared_ptr<rai::bootstrap_client> result;
	if (!idle.empty ())
	{
		result = idle.back ();
		idle.pop_back ();
	}
	return result;
}

bool rai::bootstrap_attempt::consume_future (std::future<bool> & future_a)
{
	bool result;
	try
	{
		result = future_a.get ();
	}
	catch (std::future_error &)
	{
		result = true;
	}
	return result;
}

void rai::bootstrap_attempt::populate_connections ()
{
	if (connections < node->config.bootstrap_connections)
	{
		auto peer (node->peers.bootstrap_peer ());
		if (peer != rai::endpoint (boost::asio::ip::address_v6::any (), 0))
		{
			auto client (std::make_shared<rai::bootstrap_client> (node, shared_from_this (), rai::tcp_endpoint (peer.address (), peer.port ())));
			client->run ();
			std::lock_guard<std::mutex> lock (mutex);
			clients.push_back (client);
		}
		else
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Bootstrap stopped because there are no peers"));
			stopped = true;
			condition.notify_all ();
		}
	}
	if (!stopped)
	{
		std::weak_ptr<rai::bootstrap_attempt> this_w (shared_from_this ());
		node->alarm.add (std::chrono::system_clock::now () + std::chrono::seconds (5), [this_w]() {
			if (auto this_l = this_w.lock ())
			{
				this_l->populate_connections ();
			}
		});
	}
}

void rai::bootstrap_attempt::add_connection (rai::endpoint const & endpoint_a)
{
	auto client (std::make_shared<rai::bootstrap_client> (node, shared_from_this (), rai::tcp_endpoint (endpoint_a.address (), endpoint_a.port ())));
	client->run ();
}

void rai::bootstrap_attempt::pool_connection (std::shared_ptr<rai::bootstrap_client> client_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	idle.push_back (client_a);
	condition.notify_all ();
}

void rai::bootstrap_attempt::stop ()
{
	std::lock_guard<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
	for (auto i : clients)
	{
		if (auto client = i.lock ())
		{
			client->socket.close ();
		}
	}
	if (auto i = frontiers.lock ())
	{
		try
		{
			i->promise.set_value (true);
		}
		catch (std::future_error &)
		{
		}
	}
	if (auto i = push.lock ())
	{
		try
		{
			i->promise.set_value (true);
		}
		catch (std::future_error &)
		{
		}
	}
}

void rai::bootstrap_attempt::requeue_pull (rai::pull_info const & pull_a)
{
	auto pull (pull_a);
	if (++pull.attempts < 4)
	{
		std::lock_guard<std::mutex> lock (mutex);
		pulls.push_front (pull);
		condition.notify_all ();
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Failed to pull account %1% down to %2% after %3% attempts") % pull.account.to_account () % pull.end.to_string () % pull.attempts);
		}
	}
}

rai::bootstrap_initiator::bootstrap_initiator (rai::node & node_a) :
node (node_a),
stopped (false),
thread ([this] () {run_bootstrap ();})
{
}

rai::bootstrap_initiator::~bootstrap_initiator ()
{
	stop ();
	thread.join ();
}

void rai::bootstrap_initiator::bootstrap ()
{
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped && attempt == nullptr)
	{
		attempt = std::make_shared<rai::bootstrap_attempt> (node.shared ());
		condition.notify_all ();
	}
}

void rai::bootstrap_initiator::bootstrap (rai::endpoint const & endpoint_a)
{
	node.peers.insert (endpoint_a, 0x5);
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		while (attempt != nullptr)
		{
			attempt->stop ();
			condition.wait (lock);
		}
		attempt = std::make_shared<rai::bootstrap_attempt> (node.shared ());
		attempt->add_connection (endpoint_a);
		condition.notify_all ();
	}
}

void rai::bootstrap_initiator::run_bootstrap ()
{
	std::unique_lock <std::mutex> lock (mutex);
	while (!stopped)
	{
		if (attempt != nullptr)
		{
			lock.unlock ();
			attempt->run ();
			lock.lock ();
			attempt = nullptr;
			condition.notify_all ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void rai::bootstrap_initiator::add_observer (std::function<void(bool)> const & observer_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	observers.push_back (observer_a);
}

bool rai::bootstrap_initiator::in_progress ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return attempt != nullptr;
}

void rai::bootstrap_initiator::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	stopped = true;
	if (attempt != nullptr)
	{
		attempt->stop ();
	}
	condition.notify_all ();
}

void rai::bootstrap_initiator::notify_listeners (bool in_progress_a)
{
	for (auto & i : observers)
	{
		i (in_progress_a);
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

	boost::system::error_code ec;
	acceptor.bind (local, ec);
	if (ec)
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Error while binding for bootstrap on port %1%: %2%") % local.port () % ec.message ());
		throw std::runtime_error (ec.message ());
	}

	acceptor.listen ();
	accept_connection ();
}

void rai::bootstrap_listener::stop ()
{
	on = false;
	std::lock_guard<std::mutex> lock (mutex);
	acceptor.close ();
	for (auto & i : connections)
	{
		auto connection (i.second.lock ());
		if (connection)
		{
			connection->socket->close ();
		}
	}
}

void rai::bootstrap_listener::accept_connection ()
{
	auto socket (std::make_shared<boost::asio::ip::tcp::socket> (service));
	acceptor.async_accept (*socket, [this, socket](boost::system::error_code const & ec) {
		accept_action (ec, socket);
	});
}

void rai::bootstrap_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr<boost::asio::ip::tcp::socket> socket_a)
{
	if (!ec)
	{
		accept_connection ();
		auto connection (std::make_shared<rai::bootstrap_server> (socket_a, node.shared ()));
		{
			std::lock_guard<std::mutex> lock (mutex);
			if (acceptor.is_open ())
			{
				connections[connection.get ()] = connection;
				connection->receive ();
			}
		}
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
	if (node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (node->log) << "Exiting bootstrap server";
	}
	std::lock_guard<std::mutex> lock (node->bootstrap.mutex);
	node->bootstrap.connections.erase (this);
}

rai::bootstrap_server::bootstrap_server (std::shared_ptr<boost::asio::ip::tcp::socket> socket_a, std::shared_ptr<rai::node> node_a) :
socket (socket_a),
node (node_a)
{
}

void rai::bootstrap_server::receive ()
{
	auto this_l (shared_from_this ());
	boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), 8), [this_l](boost::system::error_code const & ec, size_t size_a) {
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
		std::bitset<16> extensions;
		if (!rai::message::read_header (type_stream, version_max, version_using, version_min, type, extensions))
		{
			switch (type)
			{
				case rai::message_type::bulk_pull:
				{
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_action (ec, size_a);
					});
					break;
				}
				case rai::message_type::frontier_req:
				{
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t)), [this_l](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_frontier_req_action (ec, size_a);
					});
					break;
				}
				case rai::message_type::bulk_push:
				{
					add_request (std::unique_ptr<rai::message> (new rai::bulk_push));
					break;
				}
				default:
				{
					if (node->config.logging.network_logging ())
					{
						BOOST_LOG (node->log) << boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast<uint8_t> (type));
					}
					break;
				}
			}
		}
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Error while receiving type %1%") % ec.message ());
		}
	}
}

void rai::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		std::unique_ptr<rai::bulk_pull> request (new rai::bulk_pull);
		rai::bufferstream stream (receive_buffer.data (), 8 + sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
		auto error (request->deserialize (stream));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received bulk pull for %1% down to %2%") % request->start.to_string () % request->end.to_string ());
			}
			add_request (std::unique_ptr<rai::message> (request.release ()));
			receive ();
		}
	}
}

void rai::bootstrap_server::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		std::unique_ptr<rai::frontier_req> request (new rai::frontier_req);
		rai::bufferstream stream (receive_buffer.data (), 8 + sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age);
			}
			add_request (std::unique_ptr<rai::message> (request.release ()));
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

void rai::bootstrap_server::add_request (std::unique_ptr<rai::message> message_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next ();
	}
}

void rai::bootstrap_server::finish_request ()
{
	std::lock_guard<std::mutex> lock (mutex);
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
	request_response_visitor (std::shared_ptr<rai::bootstrap_server> connection_a) :
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
		auto response (std::make_shared<rai::bulk_pull_server> (connection, std::unique_ptr<rai::bulk_pull> (static_cast<rai::bulk_pull *> (connection->requests.front ().release ()))));
		response->send_next ();
	}
	void bulk_push (rai::bulk_push const &) override
	{
		auto response (std::make_shared<rai::bulk_push_server> (connection));
		response->receive ();
	}
	void frontier_req (rai::frontier_req const &) override
	{
		auto response (std::make_shared<rai::frontier_req_server> (connection, std::unique_ptr<rai::frontier_req> (static_cast<rai::frontier_req *> (connection->requests.front ().release ()))));
		response->send_next ();
	}
	std::shared_ptr<rai::bootstrap_server> connection;
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
	std::unique_ptr<rai::block> block (get_next ());
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
		async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		send_finished ();
	}
}

std::unique_ptr<rai::block> rai::bulk_pull_server::get_next ()
{
	std::unique_ptr<rai::block> result;
	if (current != request->end)
	{
		rai::transaction transaction (connection->node->store.environment, nullptr, false);
		result = connection->node->store.block_get (transaction, current);
		if (result != nullptr)
		{
			auto previous (result->previous ());
			if (!previous.is_zero ())
			{
				current = previous;
			}
			else
			{
				current = request->end;
			}
		}
		else
		{
			current = request->end;
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
	send_buffer.push_back (static_cast<uint8_t> (rai::block_type::not_a_block));
	auto this_l (shared_from_this ());
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk sending finished";
	}
	async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
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

rai::bulk_pull_server::bulk_pull_server (std::shared_ptr<rai::bootstrap_server> const & connection_a, std::unique_ptr<rai::bulk_pull> request_a) :
connection (connection_a),
request (std::move (request_a))
{
	set_current_end ();
}

rai::bulk_push_server::bulk_push_server (std::shared_ptr<rai::bootstrap_server> const & connection_a) :
connection (connection_a)
{
}

void rai::bulk_push_server::receive ()
{
	auto this_l (shared_from_this ());
	boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
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
	rai::block_type type (static_cast<rai::block_type> (receive_buffer[0]));
	switch (type)
	{
		case rai::block_type::send:
		{
			boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::send_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case rai::block_type::receive:
		{
			boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::receive_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case rai::block_type::open:
		{
			boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::open_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case rai::block_type::change:
		{
			boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, rai::change_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
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
				connection->node->process_active (std::move (block));
			}
			receive ();
		}
		else
		{
			BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
		}
	}
}

rai::frontier_req_server::frontier_req_server (std::shared_ptr<rai::bootstrap_server> const & connection_a, std::unique_ptr<rai::frontier_req> request_a) :
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
		async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
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
	async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
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
		current = rai::uint256_union (iterator->first.uint256 ());
		info = rai::account_info (iterator->second);
	}
	else
	{
		current.clear ();
	}
}
