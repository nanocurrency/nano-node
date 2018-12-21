#include <rai/node/bootstrap.hpp>

#include <rai/node/common.hpp>
#include <rai/node/node.hpp>

#include <boost/log/trivial.hpp>

constexpr double bootstrap_connection_scale_target_blocks = 50000.0;
constexpr double bootstrap_connection_warmup_time_sec = 5.0;
constexpr double bootstrap_minimum_blocks_per_sec = 10.0;
constexpr double bootstrap_minimum_frontier_blocks_per_sec = 1000.0;
constexpr unsigned bootstrap_frontier_retry_limit = 16;
constexpr double bootstrap_minimum_termination_time_sec = 30.0;
constexpr unsigned bootstrap_max_new_connections = 10;
constexpr unsigned bulk_push_cost_limit = 200;

rai::socket::socket (std::shared_ptr<rai::node> node_a) :
socket_m (node_a->io_ctx),
cutoff (std::numeric_limits<uint64_t>::max ()),
node (node_a)
{
}

void rai::socket::async_connect (rai::tcp_endpoint const & endpoint_a, std::function<void(boost::system::error_code const &)> callback_a)
{
	checkup ();
	auto this_l (shared_from_this ());
	start ();
	socket_m.async_connect (endpoint_a, [this_l, callback_a](boost::system::error_code const & ec) {
		this_l->stop ();
		callback_a (ec);
	});
}

void rai::socket::async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	assert (size_a <= buffer_a->size ());
	auto this_l (shared_from_this ());
	start ();
	boost::asio::async_read (socket_m, boost::asio::buffer (buffer_a->data (), size_a), [this_l, callback_a](boost::system::error_code const & ec, size_t size_a) {
		this_l->node->stats.add (rai::stat::type::traffic_bootstrap, rai::stat::dir::in, size_a);
		this_l->stop ();
		callback_a (ec, size_a);
	});
}

void rai::socket::async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	auto this_l (shared_from_this ());
	start ();
	boost::asio::async_write (socket_m, boost::asio::buffer (buffer_a->data (), buffer_a->size ()), [this_l, callback_a, buffer_a](boost::system::error_code const & ec, size_t size_a) {
		this_l->node->stats.add (rai::stat::type::traffic_bootstrap, rai::stat::dir::out, size_a);
		this_l->stop ();
		callback_a (ec, size_a);
	});
}

void rai::socket::start (std::chrono::steady_clock::time_point timeout_a)
{
	cutoff = timeout_a.time_since_epoch ().count ();
}

void rai::socket::stop ()
{
	cutoff = std::numeric_limits<uint64_t>::max ();
}

void rai::socket::close ()
{
	if (socket_m.is_open ())
	{
		try
		{
			socket_m.shutdown (boost::asio::ip::tcp::socket::shutdown_both);
		}
		catch (...)
		{
			/* Ignore spurious exceptions; shutdown is best effort. */
		}
		socket_m.close ();
	}
}

void rai::socket::checkup ()
{
	std::weak_ptr<rai::socket> this_w (shared_from_this ());
	node->alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (10), [this_w]() {
		if (auto this_l = this_w.lock ())
		{
			if (this_l->cutoff != std::numeric_limits<uint64_t>::max () && this_l->cutoff < std::chrono::steady_clock::now ().time_since_epoch ().count ())
			{
				if (this_l->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Disconnecting from %1% due to timeout") % this_l->remote_endpoint ());
				}
				this_l->close ();
			}
			else
			{
				this_l->checkup ();
			}
		}
	});
}

rai::tcp_endpoint rai::socket::remote_endpoint ()
{
	rai::tcp_endpoint endpoint;

	if (socket_m.is_open ())
	{
		boost::system::error_code remote_endpoint_error;

		endpoint = socket_m.remote_endpoint (remote_endpoint_error);
	}

	return endpoint;
}

rai::bootstrap_client::bootstrap_client (std::shared_ptr<rai::node> node_a, std::shared_ptr<rai::bootstrap_attempt> attempt_a, rai::tcp_endpoint const & endpoint_a) :
node (node_a),
attempt (attempt_a),
socket (std::make_shared<rai::socket> (node_a)),
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
endpoint (endpoint_a),
start_time (std::chrono::steady_clock::now ()),
block_count (0),
pending_stop (false),
hard_stop (false)
{
	++attempt->connections;
	receive_buffer->resize (256);
}

rai::bootstrap_client::~bootstrap_client ()
{
	--attempt->connections;
}

double rai::bootstrap_client::block_rate () const
{
	auto elapsed = elapsed_seconds ();
	return elapsed > 0.0 ? (double)block_count.load () / elapsed : 0.0;
}

double rai::bootstrap_client::elapsed_seconds () const
{
	return std::chrono::duration_cast<std::chrono::duration<double>> (std::chrono::steady_clock::now () - start_time).count ();
}

void rai::bootstrap_client::stop (bool force)
{
	pending_stop = true;
	if (force)
	{
		hard_stop = true;
	}
}

void rai::bootstrap_client::run ()
{
	auto this_l (shared_from_this ());
	socket->async_connect (endpoint, [this_l](boost::system::error_code const & ec) {
		if (!ec)
		{
			if (this_l->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Connection established to %1%") % this_l->endpoint);
			}
			this_l->attempt->pool_connection (this_l->shared_from_this ());
		}
		else
		{
			if (this_l->node->config.logging.network_logging ())
			{
				switch (ec.value ())
				{
					default:
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error initiating bootstrap connection to %1%: %2%") % this_l->endpoint % ec.message ());
						break;
					case boost::system::errc::connection_refused:
					case boost::system::errc::operation_canceled:
					case boost::system::errc::timed_out:
					case 995: //Windows The I/O operation has been aborted because of either a thread exit or an application request
					case 10061: //Windows No connection could be made because the target machine actively refused it
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
	request->count = std::numeric_limits<decltype (request->count)>::max ();
	auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		rai::vectorstream stream (*send_buffer);
		request->serialize (stream);
	}
	auto this_l (shared_from_this ());
	connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
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
bulk_push_cost (0)
{
	auto transaction (connection->node->store.tx_begin_read ());
	next (transaction);
}

rai::frontier_req_client::~frontier_req_client ()
{
}

void rai::frontier_req_client::receive_frontier ()
{
	auto this_l (shared_from_this ());
	size_t size_l (sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
	connection->socket->async_read (connection->receive_buffer, size_l, [this_l, size_l](boost::system::error_code const & ec, size_t size_a) {
		// An issue with asio is that sometimes, instead of reporting a bad file descriptor during disconnect,
		// we simply get a size of 0.
		if (size_a == size_l)
		{
			this_l->received_frontier (ec, size_a);
		}
		else
		{
			if (this_l->connection->node->config.logging.network_message_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Invalid size: expected %1%, got %2%") % size_l % size_a);
			}
		}
	});
}

void rai::frontier_req_client::unsynced (rai::block_hash const & head, rai::block_hash const & end)
{
	if (bulk_push_cost < bulk_push_cost_limit)
	{
		connection->attempt->add_bulk_push_target (head, end);
		if (end.is_zero ())
		{
			bulk_push_cost += 2;
		}
		else
		{
			bulk_push_cost += 1;
		}
	}
}

void rai::frontier_req_client::received_frontier (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		assert (size_a == sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
		rai::account account;
		rai::bufferstream account_stream (connection->receive_buffer->data (), sizeof (rai::uint256_union));
		auto error1 (rai::read (account_stream, account));
		assert (!error1);
		rai::block_hash latest;
		rai::bufferstream latest_stream (connection->receive_buffer->data () + sizeof (rai::uint256_union), sizeof (rai::uint256_union));
		auto error2 (rai::read (latest_stream, latest));
		assert (!error2);
		if (count == 0)
		{
			start_time = std::chrono::steady_clock::now ();
		}
		++count;
		std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>> (std::chrono::steady_clock::now () - start_time);
		double elapsed_sec = time_span.count ();
		double blocks_per_sec = (double)count / elapsed_sec;
		if (elapsed_sec > bootstrap_connection_warmup_time_sec && blocks_per_sec < bootstrap_minimum_frontier_blocks_per_sec)
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Aborting frontier req because it was too slow"));
			promise.set_value (true);
			return;
		}
		if (connection->attempt->should_log ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Received %1% frontiers from %2%") % std::to_string (count) % connection->socket->remote_endpoint ());
		}
		auto transaction (connection->node->store.tx_begin_read ());
		if (!account.is_zero ())
		{
			while (!current.is_zero () && current < account)
			{
				// We know about an account they don't.
				unsynced (frontier, 0);
				next (transaction);
			}
			if (!current.is_zero ())
			{
				if (account == current)
				{
					if (latest == frontier)
					{
						// In sync
					}
					else
					{
						if (connection->node->store.block_exists (transaction, latest))
						{
							// We know about a block they don't.
							unsynced (frontier, latest);
						}
						else
						{
							connection->attempt->add_pull (rai::pull_info (account, latest, frontier));
							// Either we're behind or there's a fork we differ on
							// Either way, bulk pushing will probably not be effective
							bulk_push_cost += 5;
						}
					}
					next (transaction);
				}
				else
				{
					assert (account < current);
					connection->attempt->add_pull (rai::pull_info (account, latest, rai::block_hash (0)));
				}
			}
			else
			{
				connection->attempt->add_pull (rai::pull_info (account, latest, rai::block_hash (0)));
			}
			receive_frontier ();
		}
		else
		{
			while (!current.is_zero ())
			{
				// We know about an account they don't.
				unsynced (frontier, 0);
				next (transaction);
			}
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (connection->node->log) << "Bulk push cost: " << bulk_push_cost;
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

void rai::frontier_req_client::next (rai::transaction const & transaction_a)
{
	// Filling accounts deque to prevent often read transactions
	if (accounts.empty ())
	{
		size_t max_size (128);
		for (auto i (connection->node->store.latest_begin (transaction_a, current.number () + 1)), n (connection->node->store.latest_end ()); i != n && accounts.size () != max_size; ++i)
		{
			rai::account_info info (i->second);
			accounts.push_back (std::make_pair (rai::account (i->first), info.head));
		}
		/* If loop breaks before max_size, then latest_end () is reached
		Add empty record to finish frontier_req_server */
		if (accounts.size () != max_size)
		{
			accounts.push_back (std::make_pair (rai::account (0), rai::block_hash (0)));
		}
	}
	// Retrieving accounts from deque
	auto account_pair (accounts.front ());
	accounts.pop_front ();
	current = account_pair.first;
	frontier = account_pair.second;
}

rai::bulk_pull_client::bulk_pull_client (std::shared_ptr<rai::bootstrap_client> connection_a, rai::pull_info const & pull_a) :
connection (connection_a),
pull (pull_a),
total_blocks (0)
{
	std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
	connection->attempt->condition.notify_all ();
}

rai::bulk_pull_client::~bulk_pull_client ()
{
	// If received end block is not expected end block
	if (expected != pull.end)
	{
		pull.head = expected;
		if (connection->attempt->lazy_mode)
		{
			pull.account = expected;
		}
		connection->attempt->requeue_pull (pull);
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Bulk pull end block is not expected %1% for account %2%") % pull.end.to_string () % pull.account.to_account ());
		}
	}
	{
		std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
		--connection->attempt->pulling;
	}
	connection->attempt->condition.notify_all ();
}

void rai::bulk_pull_client::request ()
{
	expected = pull.head;
	rai::bulk_pull req;
	req.start = pull.account;
	req.end = pull.end;
	req.count = pull.count;
	req.set_count_present (pull.count != 0);

	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		rai::vectorstream stream (*buffer);
		req.serialize (stream);
	}
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		std::unique_lock<std::mutex> lock (connection->attempt->mutex);
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("Requesting account %1% from %2%. %3% accounts in queue") % req.start.to_account () % connection->endpoint % connection->attempt->pulls.size ());
	}
	else if (connection->node->config.logging.network_logging () && connection->attempt->should_log ())
	{
		std::unique_lock<std::mutex> lock (connection->attempt->mutex);
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("%1% accounts in pull queue") % connection->attempt->pulls.size ());
	}
	auto this_l (shared_from_this ());
	connection->socket->async_write (buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
			this_l->receive_block ();
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error sending bulk pull request to %1%: to %2%") % ec.message () % this_l->connection->endpoint);
			}
		}
	});
}

void rai::bulk_pull_client::receive_block ()
{
	auto this_l (shared_from_this ());
	connection->socket->async_read (connection->receive_buffer, 1, [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
			this_l->received_type ();
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
			}
		}
	});
}

void rai::bulk_pull_client::received_type ()
{
	auto this_l (shared_from_this ());
	rai::block_type type (static_cast<rai::block_type> (connection->receive_buffer->data ()[0]));
	switch (type)
	{
		case rai::block_type::send:
		{
			connection->socket->async_read (connection->receive_buffer, rai::send_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::receive:
		{
			connection->socket->async_read (connection->receive_buffer, rai::receive_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::open:
		{
			connection->socket->async_read (connection->receive_buffer, rai::open_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::change:
		{
			connection->socket->async_read (connection->receive_buffer, rai::change_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::state:
		{
			connection->socket->async_read (connection->receive_buffer, rai::state_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::not_a_block:
		{
			// Avoid re-using slow peers, or peers that sent the wrong blocks.
			if (!connection->pending_stop && expected == pull.end)
			{
				connection->attempt->pool_connection (connection);
			}
			break;
		}
		default:
		{
			if (connection->node->config.logging.network_packet_logging ())
			{
				BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unknown type received as block type: %1%") % static_cast<int> (type));
			}
			break;
		}
	}
}

void rai::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a, rai::block_type type_a)
{
	if (!ec)
	{
		rai::bufferstream stream (connection->receive_buffer->data (), size_a);
		std::shared_ptr<rai::block> block (rai::deserialize_block (stream, type_a));
		if (block != nullptr && !rai::work_validate (*block))
		{
			auto hash (block->hash ());
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				std::string block_l;
				block->serialize_json (block_l);
				BOOST_LOG (connection->node->log) << boost::str (boost::format ("Pulled block %1% %2%") % hash.to_string () % block_l);
			}
			bool block_expected (false);
			if (hash == expected)
			{
				expected = block->previous ();
				block_expected = true;
			}
			if (connection->block_count++ == 0)
			{
				connection->start_time = std::chrono::steady_clock::now ();
			}
			connection->attempt->total_blocks++;
			total_blocks++;
			bool stop_pull (connection->attempt->process_block (block, total_blocks, block_expected));
			if (!stop_pull && !connection->hard_stop.load ())
			{
				receive_block ();
			}
			else if (stop_pull && block_expected)
			{
				expected = pull.end;
				connection->attempt->pool_connection (connection);
			}
			if (stop_pull)
			{
				connection->attempt->lazy_stopped++;
			}
		}
		else
		{
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
			}
		}
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error bulk receiving block: %1%") % ec.message ());
		}
	}
}

rai::bulk_push_client::bulk_push_client (std::shared_ptr<rai::bootstrap_client> const & connection_a) :
connection (connection_a)
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
	connection->socket->async_write (buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
		auto transaction (this_l->connection->node->store.tx_begin_read ());
		if (!ec)
		{
			this_l->push (transaction);
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Unable to send bulk_push request: %1%") % ec.message ());
			}
		}
	});
}

void rai::bulk_push_client::push (rai::transaction const & transaction_a)
{
	std::shared_ptr<rai::block> block;
	bool finished (false);
	while (block == nullptr && !finished)
	{
		if (current_target.first.is_zero () || current_target.first == current_target.second)
		{
			std::lock_guard<std::mutex> guard (connection->attempt->mutex);
			if (!connection->attempt->bulk_push_targets.empty ())
			{
				current_target = connection->attempt->bulk_push_targets.back ();
				connection->attempt->bulk_push_targets.pop_back ();
			}
			else
			{
				finished = true;
			}
		}
		if (!finished)
		{
			block = connection->node->store.block_get (transaction_a, current_target.first);
			if (block == nullptr)
			{
				current_target.first = rai::block_hash (0);
			}
			else
			{
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (connection->node->log) << "Bulk pushing range " << current_target.first.to_string () << " down to " << current_target.second.to_string ();
				}
			}
		}
	}
	if (finished)
	{
		send_finished ();
	}
	else
	{
		current_target.first = block->previous ();
		push_block (*block);
	}
}

void rai::bulk_push_client::send_finished ()
{
	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	buffer->push_back (static_cast<uint8_t> (rai::block_type::not_a_block));
	connection->node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::bulk_push, rai::stat::dir::out);
	if (connection->node->config.logging.network_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk push finished";
	}
	auto this_l (shared_from_this ());
	connection->socket->async_write (buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
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
	connection->socket->async_write (buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
			auto transaction (this_l->connection->node->store.tx_begin_read ());
			this_l->push (transaction);
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error sending block during bulk push: %1%") % ec.message ());
			}
		}
	});
}

rai::pull_info::pull_info () :
account (0),
end (0),
count (0),
attempts (0)
{
}

rai::pull_info::pull_info (rai::account const & account_a, rai::block_hash const & head_a, rai::block_hash const & end_a, count_t count_a) :
account (account_a),
head (head_a),
end (end_a),
count (count_a),
attempts (0)
{
}

rai::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<rai::node> node_a) :
next_log (std::chrono::steady_clock::now ()),
connections (0),
pulling (0),
node (node_a),
account_count (0),
total_blocks (0),
stopped (false),
lazy_mode (false),
lazy_stopped (0)
{
	BOOST_LOG (node->log) << "Starting bootstrap attempt";
	node->bootstrap_initiator.notify_listeners (true);
}

rai::bootstrap_attempt::~bootstrap_attempt ()
{
	BOOST_LOG (node->log) << "Exiting bootstrap attempt";
	node->bootstrap_initiator.notify_listeners (false);
}

bool rai::bootstrap_attempt::should_log ()
{
	std::lock_guard<std::mutex> lock (mutex);
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		result = true;
		next_log = now + std::chrono::seconds (15);
	}
	return result;
}

bool rai::bootstrap_attempt::request_frontier (std::unique_lock<std::mutex> & lock_a)
{
	auto result (true);
	auto connection_l (connection (lock_a));
	connection_frontier_request = connection_l;
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
		result = consume_future (future); // This is out of scope of `client' so when the last reference via boost::asio::io_context is lost and the client is destroyed, the future throws an exception.
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
		if (lazy_mode)
		{
			// Check if pull is obsolete (head was processed)
			std::unique_lock<std::mutex> lock (lazy_mutex);
			while (!pulls.empty () && !pull.head.is_zero () && lazy_blocks.find (pull.head) != lazy_blocks.end ())
			{
				pull = pulls.front ();
				pulls.pop_front ();
			}
		}
		++pulling;
		// The bulk_pull_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
		// Dispatch request in an external thread in case it needs to be destroyed
		node->background ([connection_l, pull]() {
			auto client (std::make_shared<rai::bulk_pull_client> (connection_l, pull));
			client->request ();
		});
	}
}

void rai::bootstrap_attempt::request_push (std::unique_lock<std::mutex> & lock_a)
{
	bool error (false);
	if (auto connection_shared = connection_frontier_request.lock ())
	{
		std::future<bool> future;
		{
			auto client (std::make_shared<rai::bulk_push_client> (connection_shared));
			client->start ();
			push = client;
			future = client->promise.get_future ();
		}
		lock_a.unlock ();
		error = consume_future (future); // This is out of scope of `client' so when the last reference via boost::asio::io_context is lost and the client is destroyed, the future throws an exception.
		lock_a.lock ();
	}
	if (node->config.logging.network_logging ())
	{
		BOOST_LOG (node->log) << "Exiting bulk push client";
		if (error)
		{
			BOOST_LOG (node->log) << "Bulk push client failed";
		}
	}
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
	// Shuffle pulls.
	for (int i = pulls.size () - 1; i > 0; i--)
	{
		auto k = rai::random_pool.GenerateWord32 (0, i);
		std::swap (pulls[i], pulls[k]);
	}
	while (still_pulling ())
	{
		while (still_pulling ())
		{
			if (!pulls.empty ())
			{
				if (!node->block_processor.full ())
				{
					request_pull (lock);
				}
				else
				{
					condition.wait_for (lock, std::chrono::seconds (15));
				}
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
		request_push (lock);
		// Start lazy bootstrap if some lazy keys were inserted
		if (!lazy_keys.empty () && !node->flags.disable_lazy_bootstrap)
		{
			lock.unlock ();
			lazy_mode = true;
			lazy_run ();
			lock.lock ();
		}
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

struct block_rate_cmp
{
	bool operator() (const std::shared_ptr<rai::bootstrap_client> & lhs, const std::shared_ptr<rai::bootstrap_client> & rhs) const
	{
		return lhs->block_rate () > rhs->block_rate ();
	}
};

unsigned rai::bootstrap_attempt::target_connections (size_t pulls_remaining)
{
	if (node->config.bootstrap_connections >= node->config.bootstrap_connections_max)
	{
		return std::max (1U, node->config.bootstrap_connections_max);
	}

	// Only scale up to bootstrap_connections_max for large pulls.
	double step = std::min (1.0, std::max (0.0, (double)pulls_remaining / bootstrap_connection_scale_target_blocks));
	double target = (double)node->config.bootstrap_connections + (double)(node->config.bootstrap_connections_max - node->config.bootstrap_connections) * step;
	return std::max (1U, (unsigned)(target + 0.5f));
}

void rai::bootstrap_attempt::populate_connections ()
{
	double rate_sum = 0.0;
	size_t num_pulls = 0;
	std::priority_queue<std::shared_ptr<rai::bootstrap_client>, std::vector<std::shared_ptr<rai::bootstrap_client>>, block_rate_cmp> sorted_connections;
	{
		std::unique_lock<std::mutex> lock (mutex);
		num_pulls = pulls.size ();
		for (auto & c : clients)
		{
			if (auto client = c.lock ())
			{
				double elapsed_sec = client->elapsed_seconds ();
				auto blocks_per_sec = client->block_rate ();
				rate_sum += blocks_per_sec;
				if (client->elapsed_seconds () > bootstrap_connection_warmup_time_sec && client->block_count > 0)
				{
					sorted_connections.push (client);
				}
				// Force-stop the slowest peers, since they can take the whole bootstrap hostage by dribbling out blocks on the last remaining pull.
				// This is ~1.5kilobits/sec.
				if (elapsed_sec > bootstrap_minimum_termination_time_sec && blocks_per_sec < bootstrap_minimum_blocks_per_sec)
				{
					if (node->config.logging.bulk_pull_logging ())
					{
						BOOST_LOG (node->log) << boost::str (boost::format ("Stopping slow peer %1% (elapsed sec %2%s > %3%s and %4% blocks per second < %5%)") % client->endpoint.address ().to_string () % elapsed_sec % bootstrap_minimum_termination_time_sec % blocks_per_sec % bootstrap_minimum_blocks_per_sec);
					}

					client->stop (true);
				}
			}
		}
	}

	auto target = target_connections (num_pulls);

	// We only want to drop slow peers when more than 2/3 are active. 2/3 because 1/2 is too aggressive, and 100% rarely happens.
	// Probably needs more tuning.
	if (sorted_connections.size () >= (target * 2) / 3 && target >= 4)
	{
		// 4 -> 1, 8 -> 2, 16 -> 4, arbitrary, but seems to work well.
		auto drop = (int)roundf (sqrtf ((float)target - 2.0f));

		if (node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Dropping %1% bulk pull peers, target connections %2%") % drop % target);
		}

		for (int i = 0; i < drop; i++)
		{
			auto client = sorted_connections.top ();

			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Dropping peer with block rate %1%, block count %2% (%3%) ") % client->block_rate () % client->block_count % client->endpoint.address ().to_string ());
			}

			client->stop (false);
			sorted_connections.pop ();
		}
	}

	if (node->config.logging.bulk_pull_logging ())
	{
		std::unique_lock<std::mutex> lock (mutex);
		BOOST_LOG (node->log) << boost::str (boost::format ("Bulk pull connections: %1%, rate: %2% blocks/sec, remaining account pulls: %3%, total blocks: %4%") % connections.load () % (int)rate_sum % pulls.size () % (int)total_blocks.load ());
	}

	if (connections < target)
	{
		auto delta = std::min ((target - connections) * 2, bootstrap_max_new_connections);
		// TODO - tune this better
		// Not many peers respond, need to try to make more connections than we need.
		for (int i = 0; i < delta; i++)
		{
			auto peer (node->peers.bootstrap_peer ());
			if (peer != rai::endpoint (boost::asio::ip::address_v6::any (), 0))
			{
				auto client (std::make_shared<rai::bootstrap_client> (node, shared_from_this (), rai::tcp_endpoint (peer.address (), peer.port ())));
				client->run ();
				std::lock_guard<std::mutex> lock (mutex);
				clients.push_back (client);
			}
			else if (connections == 0)
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Bootstrap stopped because there are no peers"));
				stopped = true;
				condition.notify_all ();
			}
		}
	}
	if (!stopped)
	{
		std::weak_ptr<rai::bootstrap_attempt> this_w (shared_from_this ());
		node->alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_w]() {
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
	{
		std::lock_guard<std::mutex> lock (mutex);
		idle.push_front (client_a);
	}
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
			client->socket->close ();
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

void rai::bootstrap_attempt::add_pull (rai::pull_info const & pull)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		pulls.push_back (pull);
	}
	condition.notify_all ();
}

void rai::bootstrap_attempt::requeue_pull (rai::pull_info const & pull_a)
{
	auto pull (pull_a);
	if (++pull.attempts < bootstrap_frontier_retry_limit)
	{
		std::lock_guard<std::mutex> lock (mutex);
		pulls.push_front (pull);
		condition.notify_all ();
	}
	else if (lazy_mode)
	{
		{
			// Retry for lazy pulls (not weak state block link assumptions)
			std::lock_guard<std::mutex> lock (mutex);
			pull.attempts++;
			pulls.push_back (pull);
		}
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

void rai::bootstrap_attempt::add_bulk_push_target (rai::block_hash const & head, rai::block_hash const & end)
{
	std::lock_guard<std::mutex> lock (mutex);
	bulk_push_targets.push_back (std::make_pair (head, end));
}

void rai::bootstrap_attempt::lazy_start (rai::block_hash const & hash_a)
{
	std::unique_lock<std::mutex> lock (lazy_mutex);
	// Add start blocks, limit 1024 (32k with disabled legacy bootstrap)
	size_t max_keys (node->flags.disable_legacy_bootstrap ? 32 * 1024 : 1024);
	if (lazy_keys.size () < max_keys && lazy_keys.find (hash_a) == lazy_keys.end () && lazy_blocks.find (hash_a) == lazy_blocks.end ())
	{
		lazy_keys.insert (hash_a);
		lazy_pulls.push_back (hash_a);
	}
}

void rai::bootstrap_attempt::lazy_add (rai::block_hash const & hash_a)
{
	// Add only unknown blocks
	assert (!lazy_mutex.try_lock ());
	if (lazy_blocks.find (hash_a) == lazy_blocks.end ())
	{
		lazy_pulls.push_back (hash_a);
	}
}

void rai::bootstrap_attempt::lazy_pull_flush ()
{
	std::unique_lock<std::mutex> lock (lazy_mutex);
	for (auto & pull_start : lazy_pulls)
	{
		// Recheck if block was already processed
		if (lazy_blocks.find (pull_start) == lazy_blocks.end ())
		{
			add_pull (rai::pull_info (pull_start, pull_start, rai::block_hash (0), lazy_max_pull_blocks));
		}
	}
	lazy_pulls.clear ();
}

bool rai::bootstrap_attempt::lazy_finished ()
{
	bool result (true);
	auto transaction (node->store.tx_begin_read ());
	std::unique_lock<std::mutex> lock (lazy_mutex);
	for (auto it (lazy_keys.begin ()), end (lazy_keys.end ()); it != end && !stopped;)
	{
		if (node->store.block_exists (transaction, *it))
		{
			// Could be not safe enough
			it = lazy_keys.erase (it);
		}
		else
		{
			result = false;
			break;
			// No need to increment `it` as we break above.
		}
	}
	// Finish lazy bootstrap without lazy pulls (in combination with still_pulling ())
	if (!result && lazy_pulls.empty ())
	{
		result = true;
	}
	return result;
}

void rai::bootstrap_attempt::lazy_run ()
{
	populate_connections ();
	auto start_time (std::chrono::steady_clock::now ());
	auto max_time (std::chrono::minutes (node->flags.disable_legacy_bootstrap ? 48 * 60 : 30));
	std::unique_lock<std::mutex> lock (mutex);
	while ((still_pulling () || !lazy_finished ()) && lazy_stopped < lazy_max_stopped && std::chrono::steady_clock::now () - start_time < max_time)
	{
		unsigned iterations (0);
		while (still_pulling () && lazy_stopped < lazy_max_stopped && std::chrono::steady_clock::now () - start_time < max_time)
		{
			if (!pulls.empty ())
			{
				if (!node->block_processor.full ())
				{
					request_pull (lock);
				}
				else
				{
					condition.wait_for (lock, std::chrono::seconds (15));
				}
			}
			else
			{
				condition.wait (lock);
			}
			++iterations;
			// Flushing lazy pulls
			if (iterations % 100 == 0)
			{
				lock.unlock ();
				lazy_pull_flush ();
				lock.lock ();
			}
		}
		// Flushing may resolve forks which can add more pulls
		// Flushing lazy pulls
		lock.unlock ();
		node->block_processor.flush ();
		lazy_pull_flush ();
		lock.lock ();
	}
	if (!stopped)
	{
		BOOST_LOG (node->log) << "Completed lazy pulls";
		// Fallback to legacy bootstrap
		std::unique_lock<std::mutex> lazy_lock (lazy_mutex);
		if (!lazy_keys.empty () && !node->flags.disable_legacy_bootstrap)
		{
			pulls.clear ();
			lock.unlock ();
			lazy_blocks.clear ();
			lazy_keys.clear ();
			lazy_pulls.clear ();
			lazy_state_unknown.clear ();
			lazy_balances.clear ();
			lazy_stopped = 0;
			lazy_mode = false;
			lazy_lock.unlock ();
			run ();
			lock.lock ();
		}
	}
	stopped = true;
	condition.notify_all ();
	idle.clear ();
}

bool rai::bootstrap_attempt::process_block (std::shared_ptr<rai::block> block_a, uint64_t total_blocks, bool block_expected)
{
	bool stop_pull (false);
	if (lazy_mode && block_expected)
	{
		auto hash (block_a->hash ());
		std::unique_lock<std::mutex> lock (lazy_mutex);
		// Processing new blocks
		if (lazy_blocks.find (hash) == lazy_blocks.end ())
		{
			// Search block in ledger (old)
			auto transaction (node->store.tx_begin_read ());
			if (!node->store.block_exists (transaction, block_a->type (), hash))
			{
				rai::uint128_t balance (std::numeric_limits<rai::uint128_t>::max ());
				node->block_processor.add (block_a, std::chrono::steady_clock::time_point ());
				// Search for new dependencies
				if (!block_a->source ().is_zero () && !node->store.block_exists (transaction, block_a->source ()))
				{
					lazy_add (block_a->source ());
				}
				else if (block_a->type () == rai::block_type::send)
				{
					// Calculate balance for legacy send blocks
					std::shared_ptr<rai::send_block> block_l (std::static_pointer_cast<rai::send_block> (block_a));
					if (block_l != nullptr)
					{
						balance = block_l->hashables.balance.number ();
					}
				}
				else if (block_a->type () == rai::block_type::state)
				{
					std::shared_ptr<rai::state_block> block_l (std::static_pointer_cast<rai::state_block> (block_a));
					if (block_l != nullptr)
					{
						balance = block_l->hashables.balance.number ();
						rai::block_hash link (block_l->hashables.link);
						// If link is not epoch link or 0. And if block from link unknown
						if (!link.is_zero () && link != node->ledger.epoch_link && lazy_blocks.find (link) == lazy_blocks.end () && !node->store.block_exists (transaction, link))
						{
							rai::block_hash previous (block_l->hashables.previous);
							// If state block previous is 0 then source block required
							if (previous.is_zero ())
							{
								lazy_add (link);
							}
							// In other cases previous block balance required to find out subtype of state block
							else if (node->store.block_exists (transaction, previous))
							{
								rai::amount prev_balance (node->ledger.balance (transaction, previous));
								if (prev_balance.number () <= balance)
								{
									lazy_add (link);
								}
							}
							// Search balance of already processed previous blocks
							else if (lazy_blocks.find (previous) != lazy_blocks.end ())
							{
								auto previous_balance (lazy_balances.find (previous));
								if (previous_balance != lazy_balances.end ())
								{
									if (previous_balance->second <= balance)
									{
										lazy_add (link);
									}
									lazy_balances.erase (previous_balance);
								}
							}
							// Insert in unknown state blocks if previous wasn't already processed
							else
							{
								lazy_state_unknown.insert (std::make_pair (previous, std::make_pair (link, balance)));
							}
						}
					}
				}
				lazy_blocks.insert (hash);
				// Adding lazy balances
				if (total_blocks == 0)
				{
					lazy_balances.insert (std::make_pair (hash, balance));
				}
				// Removing lazy balances
				if (!block_a->previous ().is_zero () && lazy_balances.find (block_a->previous ()) != lazy_balances.end ())
				{
					lazy_balances.erase (block_a->previous ());
				}
			}
			// Drop bulk_pull if block is already known (ledger)
			else
			{
				// Disabled until server rewrite
				// stop_pull = true;
				// Force drop lazy bootstrap connection for long bulk_pull
				if (total_blocks > lazy_max_pull_blocks)
				{
					stop_pull = true;
				}
			}
			//Search unknown state blocks balances
			auto find_state (lazy_state_unknown.find (hash));
			if (find_state != lazy_state_unknown.end ())
			{
				auto next_block (find_state->second);
				lazy_state_unknown.erase (hash);
				// Retrieve balance for previous state blocks
				if (block_a->type () == rai::block_type::state)
				{
					std::shared_ptr<rai::state_block> block_l (std::static_pointer_cast<rai::state_block> (block_a));
					if (block_l->hashables.balance.number () <= next_block.second)
					{
						lazy_add (next_block.first);
					}
				}
				// Retrieve balance for previous legacy send blocks
				else if (block_a->type () == rai::block_type::send)
				{
					std::shared_ptr<rai::send_block> block_l (std::static_pointer_cast<rai::send_block> (block_a));
					if (block_l->hashables.balance.number () <= next_block.second)
					{
						lazy_add (next_block.first);
					}
				}
				// Weak assumption for other legacy block types
				else
				{
					// Disabled
				}
			}
		}
		// Drop bulk_pull if block is already known (processed set)
		else
		{
			// Disabled until server rewrite
			// stop_pull = true;
			// Force drop lazy bootstrap connection for long bulk_pull
			if (total_blocks > lazy_max_pull_blocks)
			{
				stop_pull = true;
			}
		}
	}
	else if (lazy_mode)
	{
		// Drop connection with unexpected block for lazy bootstrap
		stop_pull = true;
	}
	else
	{
		node->block_processor.add (block_a, std::chrono::steady_clock::time_point ());
	}
	return stop_pull;
}

rai::bootstrap_initiator::bootstrap_initiator (rai::node & node_a) :
node (node_a),
stopped (false),
thread ([this]() {
	rai::thread_role::set (rai::thread_role::name::bootstrap_initiator);
	run_bootstrap ();
})
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
		node.stats.inc (rai::stat::type::bootstrap, rai::stat::detail::initiate, rai::stat::dir::out);
		attempt = std::make_shared<rai::bootstrap_attempt> (node.shared ());
		condition.notify_all ();
	}
}

void rai::bootstrap_initiator::bootstrap (rai::endpoint const & endpoint_a, bool add_to_peers)
{
	if (add_to_peers)
	{
		node.peers.insert (rai::map_endpoint_to_v6 (endpoint_a), rai::protocol_version);
	}
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		while (attempt != nullptr)
		{
			attempt->stop ();
			condition.wait (lock);
		}
		node.stats.inc (rai::stat::type::bootstrap, rai::stat::detail::initiate, rai::stat::dir::out);
		attempt = std::make_shared<rai::bootstrap_attempt> (node.shared ());
		attempt->add_connection (endpoint_a);
		condition.notify_all ();
	}
}

void rai::bootstrap_initiator::bootstrap_lazy (rai::block_hash const & hash_a, bool force)
{
	{
		std::unique_lock<std::mutex> lock (mutex);
		if (force)
		{
			while (attempt != nullptr)
			{
				attempt->stop ();
				condition.wait (lock);
			}
		}
		node.stats.inc (rai::stat::type::bootstrap, rai::stat::detail::initiate_lazy, rai::stat::dir::out);
		if (attempt == nullptr)
		{
			attempt = std::make_shared<rai::bootstrap_attempt> (node.shared ());
			attempt->lazy_mode = true;
		}
		attempt->lazy_start (hash_a);
	}
	condition.notify_all ();
}

void rai::bootstrap_initiator::run_bootstrap ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (attempt != nullptr)
		{
			lock.unlock ();
			if (!attempt->lazy_mode)
			{
				attempt->run ();
			}
			else
			{
				attempt->lazy_run ();
			}
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
	return current_attempt () != nullptr;
}

std::shared_ptr<rai::bootstrap_attempt> rai::bootstrap_initiator::current_attempt ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return attempt;
}

void rai::bootstrap_initiator::stop ()
{
	{
		std::unique_lock<std::mutex> lock (mutex);
		stopped = true;
		if (attempt != nullptr)
		{
			attempt->stop ();
		}
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

rai::bootstrap_listener::bootstrap_listener (boost::asio::io_context & io_ctx_a, uint16_t port_a, rai::node & node_a) :
acceptor (io_ctx_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::any (), port_a)),
io_ctx (io_ctx_a),
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
	decltype (connections) connections_l;
	{
		std::lock_guard<std::mutex> lock (mutex);
		on = false;
		connections_l.swap (connections);
	}
	acceptor.close ();
	for (auto & i : connections_l)
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
	auto socket (std::make_shared<rai::socket> (node.shared ()));
	acceptor.async_accept (socket->socket_m, [this, socket](boost::system::error_code const & ec) {
		accept_action (ec, socket);
	});
}

void rai::bootstrap_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr<rai::socket> socket_a)
{
	if (!ec)
	{
		accept_connection ();
		auto connection (std::make_shared<rai::bootstrap_server> (socket_a, node.shared ()));
		{
			std::lock_guard<std::mutex> lock (mutex);
			if (connections.size () < node.config.bootstrap_connections_max && acceptor.is_open ())
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

rai::bootstrap_server::bootstrap_server (std::shared_ptr<rai::socket> socket_a, std::shared_ptr<rai::node> node_a) :
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
socket (socket_a),
node (node_a)
{
	receive_buffer->resize (128);
}

void rai::bootstrap_server::receive ()
{
	auto this_l (shared_from_this ());
	socket->async_read (receive_buffer, 8, [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->receive_header_action (ec, size_a);
	});
}

void rai::bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		assert (size_a == 8);
		rai::bufferstream type_stream (receive_buffer->data (), size_a);
		auto error (false);
		rai::message_header header (error, type_stream);
		if (!error)
		{
			switch (header.type)
			{
				case rai::message_type::bulk_pull:
				{
					uint32_t extended_size;
					node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::bulk_pull, rai::stat::dir::in);

					if (header.bulk_pull_is_count_present ())
					{
						extended_size = rai::bulk_pull::extended_parameters_size;
					}
					else
					{
						extended_size = 0;
					}

					auto this_l (shared_from_this ());
					socket->async_read (receive_buffer, sizeof (rai::uint256_union) + sizeof (rai::uint256_union) + extended_size, [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_action (ec, size_a, header);
					});
					break;
				}
				case rai::message_type::bulk_pull_account:
				{
					node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::bulk_pull_account, rai::stat::dir::in);
					auto this_l (shared_from_this ());
					socket->async_read (receive_buffer, sizeof (rai::uint256_union) + sizeof (rai::uint128_union) + sizeof (uint8_t), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_account_action (ec, size_a, header);
					});
					break;
				}
				case rai::message_type::bulk_pull_blocks:
				{
					if (node->config.logging.network_logging ())
					{
						BOOST_LOG (node->log) << boost::str (boost::format ("Received deprecated \"bulk_pull_block\" from bootstrap connection %1%") % static_cast<uint8_t> (header.type));
					}

					auto this_l (shared_from_this ());
					socket->async_read (receive_buffer, sizeof (rai::uint256_union) + sizeof (rai::uint256_union) + sizeof (bulk_pull_blocks_mode) + sizeof (uint32_t), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_blocks_action (ec, size_a, header);
					});
					break;
				}
				case rai::message_type::frontier_req:
				{
					node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::frontier_req, rai::stat::dir::in);
					auto this_l (shared_from_this ());
					socket->async_read (receive_buffer, sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_frontier_req_action (ec, size_a, header);
					});
					break;
				}
				case rai::message_type::bulk_push:
				{
					node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::bulk_push, rai::stat::dir::in);
					add_request (std::unique_ptr<rai::message> (new rai::bulk_push (header)));
					break;
				}
				default:
				{
					if (node->config.logging.network_logging ())
					{
						BOOST_LOG (node->log) << boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast<uint8_t> (header.type));
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
			BOOST_LOG (node->log) << boost::str (boost::format ("Error while receiving type: %1%") % ec.message ());
		}
	}
}

void rai::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a, rai::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		rai::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<rai::bulk_pull> request (new rai::bulk_pull (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received bulk pull for %1% down to %2%, maximum of %3%") % request->start.to_string () % request->end.to_string () % (request->count ? request->count : std::numeric_limits<double>::infinity ()));
			}
			add_request (std::unique_ptr<rai::message> (request.release ()));
			receive ();
		}
	}
}

void rai::bootstrap_server::receive_bulk_pull_account_action (boost::system::error_code const & ec, size_t size_a, rai::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		assert (size_a == (sizeof (rai::uint256_union) + sizeof (rai::uint128_union) + sizeof (uint8_t)));
		rai::bufferstream stream (receive_buffer->data (), size_a);
		std::unique_ptr<rai::bulk_pull_account> request (new rai::bulk_pull_account (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received bulk pull account for %1% with a minimum amount of %2%") % request->account.to_account () % rai::amount (request->minimum_amount).format_balance (rai::Mxrb_ratio, 10, true));
			}
			add_request (std::unique_ptr<rai::message> (request.release ()));
			receive ();
		}
	}
}

void rai::bootstrap_server::receive_bulk_pull_blocks_action (boost::system::error_code const & ec, size_t size_a, rai::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		rai::bufferstream stream (receive_buffer->data (), sizeof (rai::uint256_union) + sizeof (rai::uint256_union) + sizeof (bulk_pull_blocks_mode) + sizeof (uint32_t));
		std::unique_ptr<rai::bulk_pull_blocks> request (new rai::bulk_pull_blocks (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received deprecated bulk pull blocks for %1% to %2%") % request->min_hash.to_string () % request->max_hash.to_string ());
			}
			add_request (std::unique_ptr<rai::message> (request.release ()));
			receive ();
		}
	}
}

void rai::bootstrap_server::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a, rai::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		rai::bufferstream stream (receive_buffer->data (), sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t));
		std::unique_ptr<rai::frontier_req> request (new rai::frontier_req (error, stream, header_a));
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
			BOOST_LOG (node->log) << boost::str (boost::format ("Error sending receiving frontier request: %1%") % ec.message ());
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
	virtual ~request_response_visitor () = default;
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
	void bulk_pull_account (rai::bulk_pull_account const &) override
	{
		auto response (std::make_shared<rai::bulk_pull_account_server> (connection, std::unique_ptr<rai::bulk_pull_account> (static_cast<rai::bulk_pull_account *> (connection->requests.front ().release ()))));
		response->send_frontier ();
	}
	void bulk_pull_blocks (rai::bulk_pull_blocks const &) override
	{
		auto response (std::make_shared<rai::bulk_pull_blocks_server> (connection, std::unique_ptr<rai::bulk_pull_blocks> (static_cast<rai::bulk_pull_blocks *> (connection->requests.front ().release ()))));
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
	void node_id_handshake (rai::node_id_handshake const &) override
	{
		assert (false);
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

/**
 * Handle a request for the pull of all blocks associated with an account
 * The account is supplied as the "start" member, and the final block to
 * send is the "end" member.  The "start" member may also be a block
 * hash, in which case the that hash is used as the start of a chain
 * to send.  To determine if "start" is interpretted as an account or
 * hash, the ledger is checked to see if the block specified exists,
 * if not then it is interpretted as an account.
 *
 * Additionally, if "start" is specified as a block hash the range
 * is inclusive of that block hash, that is the range will be:
 * [start, end); In the case that a block hash is not specified the
 * range will be exclusive of the frontier for that account with
 * a range of (frontier, end)
 */
void rai::bulk_pull_server::set_current_end ()
{
	include_start = false;
	assert (request != nullptr);
	auto transaction (connection->node->store.tx_begin_read ());
	if (!connection->node->store.block_exists (transaction, request->end))
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Bulk pull end block doesn't exist: %1%, sending everything") % request->end.to_string ());
		}
		request->end.clear ();
	}

	if (connection->node->store.block_exists (transaction, request->start))
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Bulk pull request for block hash: %1%") % request->start.to_string ());
		}

		current = request->start;
		include_start = true;
	}
	else
	{
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
			current = info.head;
			if (!request->end.is_zero ())
			{
				auto account (connection->node->ledger.account (transaction, request->end));
				if (account != request->start)
				{
					if (connection->node->config.logging.bulk_pull_logging ())
					{
						BOOST_LOG (connection->node->log) << boost::str (boost::format ("Request for block that is not on account chain: %1% not on %2%") % request->end.to_string () % request->start.to_account ());
					}
					current = request->end;
				}
			}
		}
	}

	sent_count = 0;
	if (request->is_count_present ())
	{
		max_count = request->count;
	}
	else
	{
		max_count = 0;
	}
}

void rai::bulk_pull_server::send_next ()
{
	auto block (get_next ());
	if (block != nullptr)
	{
		{
			send_buffer->clear ();
			rai::vectorstream stream (*send_buffer);
			rai::serialize_block (stream, *block);
		}
		auto this_l (shared_from_this ());
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % block->hash ().to_string ());
		}
		connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		send_finished ();
	}
}

std::shared_ptr<rai::block> rai::bulk_pull_server::get_next ()
{
	std::shared_ptr<rai::block> result;
	bool send_current = false, set_current_to_end = false;

	/*
	 * Determine if we should reply with a block
	 *
	 * If our cursor is on the final block, we should signal that we
	 * are done by returning a null result.
	 *
	 * Unless we are including the "start" member and this is the
	 * start member, then include it anyway.
	 */
	if (current != request->end)
	{
		send_current = true;
	}
	else if (current == request->end && include_start == true)
	{
		send_current = true;

		/*
		 * We also need to ensure that the next time
		 * are invoked that we return a null result
		 */
		set_current_to_end = true;
	}

	/*
	 * Account for how many blocks we have provided.  If this
	 * exceeds the requested maximum, return an empty object
	 * to signal the end of results
	 */
	if (max_count != 0 && sent_count >= max_count)
	{
		send_current = false;
	}

	if (send_current)
	{
		auto transaction (connection->node->store.tx_begin_read ());
		result = connection->node->store.block_get (transaction, current);
		if (result != nullptr && set_current_to_end == false)
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

		sent_count++;
	}

	/*
	 * Once we have processed "get_next()" once our cursor is no longer on
	 * the "start" member, so this flag is not relevant is always false.
	 */
	include_start = false;

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
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ());
		}
	}
}

void rai::bulk_pull_server::send_finished ()
{
	send_buffer->clear ();
	send_buffer->push_back (static_cast<uint8_t> (rai::block_type::not_a_block));
	auto this_l (shared_from_this ());
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk sending finished";
	}
	connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
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
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << "Unable to send not-a-block";
		}
	}
}

rai::bulk_pull_server::bulk_pull_server (std::shared_ptr<rai::bootstrap_server> const & connection_a, std::unique_ptr<rai::bulk_pull> request_a) :
connection (connection_a),
request (std::move (request_a)),
send_buffer (std::make_shared<std::vector<uint8_t>> ())
{
	set_current_end ();
}

/**
 * Bulk pull blocks related to an account
 */
void rai::bulk_pull_account_server::set_params ()
{
	assert (request != nullptr);

	/*
	 * Parse the flags
	 */
	invalid_request = false;
	pending_include_address = false;
	pending_address_only = false;
	if (request->flags == rai::bulk_pull_account_flags::pending_address_only)
	{
		pending_address_only = true;
	}
	else if (request->flags == rai::bulk_pull_account_flags::pending_hash_amount_and_address)
	{
		/**
		 ** This is the same as "pending_hash_and_amount" but with the
		 ** sending address appended, for UI purposes mainly.
		 **/
		pending_include_address = true;
	}
	else if (request->flags == rai::bulk_pull_account_flags::pending_hash_and_amount)
	{
		/** The defaults are set above **/
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Invalid bulk_pull_account flags supplied %1%") % static_cast<uint8_t> (request->flags));
		}

		invalid_request = true;

		return;
	}

	/*
	 * Initialize the current item from the requested account
	 */
	current_key.account = request->account;
	current_key.hash = 0;
}

void rai::bulk_pull_account_server::send_frontier ()
{
	/*
	 * This function is really the entry point into this class,
	 * so handle the invalid_request case by terminating the
	 * request without any response
	 */
	if (invalid_request)
	{
		connection->finish_request ();

		return;
	}

	/*
	 * Supply the account frontier
	 */
	/**
	 ** Establish a database transaction
	 **/
	auto stream_transaction (connection->node->store.tx_begin_read ());

	/**
	 ** Get account balance and frontier block hash
	 **/
	auto account_frontier_hash (connection->node->ledger.latest (stream_transaction, request->account));
	auto account_frontier_balance_int (connection->node->ledger.account_balance (stream_transaction, request->account));
	rai::uint128_union account_frontier_balance (account_frontier_balance_int);

	/**
	 ** Write the frontier block hash and balance into a buffer
	 **/
	send_buffer->clear ();
	{
		rai::vectorstream output_stream (*send_buffer);

		write (output_stream, account_frontier_hash.bytes);
		write (output_stream, account_frontier_balance.bytes);
	}

	/**
	 ** Send the buffer to the requestor
	 **/
	auto this_l (shared_from_this ());
	connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->sent_action (ec, size_a);
	});
}

void rai::bulk_pull_account_server::send_next_block ()
{
	/*
	 * Get the next item from the queue, it is a tuple with the key (which
	 * contains the account and hash) and data (which contains the amount)
	 */
	auto block_data (get_next ());
	auto block_info_key (block_data.first.get ());
	auto block_info (block_data.second.get ());

	if (block_info_key != nullptr)
	{
		/*
		 * If we have a new item, emit it to the socket
		 */
		send_buffer->clear ();

		if (pending_address_only)
		{
			rai::vectorstream output_stream (*send_buffer);

			if (connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending address: %1%") % block_info->source.to_string ());
			}

			write (output_stream, block_info->source.bytes);
		}
		else
		{
			rai::vectorstream output_stream (*send_buffer);

			if (connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % block_info_key->hash.to_string ());
			}

			write (output_stream, block_info_key->hash.bytes);
			write (output_stream, block_info->amount.bytes);

			if (pending_include_address)
			{
				/**
				 ** Write the source address as well, if requested
				 **/
				write (output_stream, block_info->source.bytes);
			}
		}

		auto this_l (shared_from_this ());
		connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		/*
		 * Otherwise, finalize the connection
		 */
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Done sending blocks"));
		}

		send_finished ();
	}
}

std::pair<std::unique_ptr<rai::pending_key>, std::unique_ptr<rai::pending_info>> rai::bulk_pull_account_server::get_next ()
{
	std::pair<std::unique_ptr<rai::pending_key>, std::unique_ptr<rai::pending_info>> result;

	while (true)
	{
		/*
		 * For each iteration of this loop, establish and then
		 * destroy a database transaction, to avoid locking the
		 * database for a prolonged period.
		 */
		auto stream_transaction (connection->node->store.tx_begin_read ());
		auto stream (connection->node->store.pending_begin (stream_transaction, current_key));

		if (stream == rai::store_iterator<rai::pending_key, rai::pending_info> (nullptr))
		{
			break;
		}

		rai::pending_key key (stream->first);
		rai::pending_info info (stream->second);

		/*
		 * Get the key for the next value, to use in the next call or iteration
		 */
		current_key.account = key.account;
		current_key.hash = key.hash.number () + 1;

		/*
		 * Finish up if the response is for a different account
		 */
		if (key.account != request->account)
		{
			break;
		}

		/*
		 * Skip entries where the amount is less than the requested
		 * minimum
		 */
		if (info.amount < request->minimum_amount)
		{
			continue;
		}

		/*
		 * If the pending_address_only flag is set, de-duplicate the
		 * responses.  The responses are the address of the sender,
		 * so they are are part of the pending table's information
		 * and not key, so we have to de-duplicate them manually.
		 */
		if (pending_address_only)
		{
			if (deduplication.count (info.source) != 0)
			{
				/*
				 * If the deduplication map gets too
				 * large, clear it out.  This may
				 * result in some duplicates getting
				 * sent to the client, but we do not
				 * want to commit too much memory
				 */
				if (deduplication.size () > 4096)
				{
					deduplication.clear ();
				}
				continue;
			}

			deduplication.insert ({ info.source, true });
		}

		result.first = std::unique_ptr<rai::pending_key> (new rai::pending_key (key));
		result.second = std::unique_ptr<rai::pending_info> (new rai::pending_info (info));

		break;
	}

	return result;
}

void rai::bulk_pull_account_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		send_next_block ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ());
		}
	}
}

void rai::bulk_pull_account_server::send_finished ()
{
	/*
	 * The "bulk_pull_account" final sequence is a final block of all
	 * zeros.  If we are sending only account public keys (with the
	 * "pending_address_only" flag) then it will be 256-bits of zeros,
	 * otherwise it will be either 384-bits of zeros (if the
	 * "pending_include_address" flag is not set) or 640-bits of zeros
	 * (if that flag is set).
	 */
	send_buffer->clear ();

	{
		rai::vectorstream output_stream (*send_buffer);
		rai::uint256_union account_zero (0);
		rai::uint128_union balance_zero (0);

		write (output_stream, account_zero.bytes);

		if (!pending_address_only)
		{
			write (output_stream, balance_zero.bytes);
			if (pending_include_address)
			{
				write (output_stream, account_zero.bytes);
			}
		}
	}

	auto this_l (shared_from_this ());

	if (connection->node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk sending for an account finished";
	}

	connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->complete (ec, size_a);
	});
}

void rai::bulk_pull_account_server::complete (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		if (pending_address_only)
		{
			assert (size_a == 32);
		}
		else
		{
			if (pending_include_address)
			{
				assert (size_a == 80);
			}
			else
			{
				assert (size_a == 48);
			}
		}

		connection->finish_request ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << "Unable to pending-as-zero";
		}
	}
}

rai::bulk_pull_account_server::bulk_pull_account_server (std::shared_ptr<rai::bootstrap_server> const & connection_a, std::unique_ptr<rai::bulk_pull_account> request_a) :
connection (connection_a),
request (std::move (request_a)),
send_buffer (std::make_shared<std::vector<uint8_t>> ()),
current_key (0, 0)
{
	/*
	 * Setup the streaming response for the first call to "send_frontier" and  "send_next_block"
	 */
	set_params ();
}

/**
 * DEPRECATED
 */
void rai::bulk_pull_blocks_server::set_params ()
{
	assert (request != nullptr);
}

void rai::bulk_pull_blocks_server::send_next ()
{
	send_finished ();
}

void rai::bulk_pull_blocks_server::send_finished ()
{
	send_buffer->clear ();
	send_buffer->push_back (static_cast<uint8_t> (rai::block_type::not_a_block));
	auto this_l (shared_from_this ());
	connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void rai::bulk_pull_blocks_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		assert (size_a == 1);
		connection->finish_request ();
	}
}

rai::bulk_pull_blocks_server::bulk_pull_blocks_server (std::shared_ptr<rai::bootstrap_server> const & connection_a, std::unique_ptr<rai::bulk_pull_blocks> request_a) :
connection (connection_a),
request (std::move (request_a)),
send_buffer (std::make_shared<std::vector<uint8_t>> ())
{
	set_params ();
}

rai::bulk_push_server::bulk_push_server (std::shared_ptr<rai::bootstrap_server> const & connection_a) :
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
connection (connection_a)
{
	receive_buffer->resize (256);
}

void rai::bulk_push_server::receive ()
{
	if (connection->node->bootstrap_initiator.in_progress ())
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << "Aborting bulk_push because a bootstrap attempt is in progress";
		}
	}
	else
	{
		auto this_l (shared_from_this ());
		connection->socket->async_read (receive_buffer, 1, [this_l](boost::system::error_code const & ec, size_t size_a) {
			if (!ec)
			{
				this_l->received_type ();
			}
			else
			{
				if (this_l->connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
				}
			}
		});
	}
}

void rai::bulk_push_server::received_type ()
{
	auto this_l (shared_from_this ());
	rai::block_type type (static_cast<rai::block_type> (receive_buffer->data ()[0]));
	switch (type)
	{
		case rai::block_type::send:
		{
			connection->node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::send, rai::stat::dir::in);
			connection->socket->async_read (receive_buffer, rai::send_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::receive:
		{
			connection->node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::receive, rai::stat::dir::in);
			connection->socket->async_read (receive_buffer, rai::receive_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::open:
		{
			connection->node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::open, rai::stat::dir::in);
			connection->socket->async_read (receive_buffer, rai::open_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::change:
		{
			connection->node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::change, rai::stat::dir::in);
			connection->socket->async_read (receive_buffer, rai::change_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case rai::block_type::state:
		{
			connection->node->stats.inc (rai::stat::type::bootstrap, rai::stat::detail::state_block, rai::stat::dir::in);
			connection->socket->async_read (receive_buffer, rai::state_block::size, [this_l, type](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a, type);
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
			if (connection->node->config.logging.network_packet_logging ())
			{
				BOOST_LOG (connection->node->log) << "Unknown type received as block type";
			}
			break;
		}
	}
}

void rai::bulk_push_server::received_block (boost::system::error_code const & ec, size_t size_a, rai::block_type type_a)
{
	if (!ec)
	{
		rai::bufferstream stream (receive_buffer->data (), size_a);
		auto block (rai::deserialize_block (stream, type_a));
		if (block != nullptr && !rai::work_validate (*block))
		{
			connection->node->process_active (std::move (block));
			receive ();
		}
		else
		{
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
			}
		}
	}
}

rai::frontier_req_server::frontier_req_server (std::shared_ptr<rai::bootstrap_server> const & connection_a, std::unique_ptr<rai::frontier_req> request_a) :
connection (connection_a),
current (request_a->start.number () - 1),
frontier (0),
request (std::move (request_a)),
send_buffer (std::make_shared<std::vector<uint8_t>> ()),
count (0)
{
	next ();
}

void rai::frontier_req_server::send_next ()
{
	if (!current.is_zero () && count <= request->count)
	{
		{
			send_buffer->clear ();
			rai::vectorstream stream (*send_buffer);
			write (stream, current.bytes);
			write (stream, frontier.bytes);
		}
		auto this_l (shared_from_this ());
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending frontier for %1% %2%") % current.to_account () % frontier.to_string ());
		}
		next ();
		connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
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
		send_buffer->clear ();
		rai::vectorstream stream (*send_buffer);
		rai::uint256_union zero (0);
		write (stream, zero.bytes);
		write (stream, zero.bytes);
	}
	auto this_l (shared_from_this ());
	if (connection->node->config.logging.network_logging ())
	{
		BOOST_LOG (connection->node->log) << "Frontier sending finished";
	}
	connection->socket->async_write (send_buffer, [this_l](boost::system::error_code const & ec, size_t size_a) {
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
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending frontier finish: %1%") % ec.message ());
		}
	}
}

void rai::frontier_req_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		count++;
		send_next ();
	}
	else
	{
		if (connection->node->config.logging.network_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending frontier pair: %1%") % ec.message ());
		}
	}
}

void rai::frontier_req_server::next ()
{
	// Filling accounts deque to prevent often read transactions
	if (accounts.empty ())
	{
		auto now (rai::seconds_since_epoch ());
		bool skip_old (request->age != std::numeric_limits<decltype (request->age)>::max ());
		size_t max_size (128);
		auto transaction (connection->node->store.tx_begin_read ());
		for (auto i (connection->node->store.latest_begin (transaction, current.number () + 1)), n (connection->node->store.latest_end ()); i != n && accounts.size () != max_size; ++i)
		{
			rai::account_info info (i->second);
			if (!skip_old || (now - info.modified) <= request->age)
			{
				accounts.push_back (std::make_pair (rai::account (i->first), info.head));
			}
		}
		/* If loop breaks before max_size, then latest_end () is reached
		Add empty record to finish frontier_req_server */
		if (accounts.size () != max_size)
		{
			accounts.push_back (std::make_pair (rai::account (0), rai::block_hash (0)));
		}
	}
	// Retrieving accounts from deque
	auto account_pair (accounts.front ());
	accounts.pop_front ();
	current = account_pair.first;
	frontier = account_pair.second;
}
