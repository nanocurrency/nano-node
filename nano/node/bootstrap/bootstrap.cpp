#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_bulk_push.hpp>
#include <nano/node/bootstrap/bootstrap_frontier.hpp>
#include <nano/node/common.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/transport/udp.hpp>

#include <boost/log/trivial.hpp>

#include <algorithm>

constexpr double nano::bootstrap_limits::bootstrap_connection_scale_target_blocks;
constexpr double nano::bootstrap_limits::bootstrap_minimum_blocks_per_sec;
constexpr unsigned nano::bootstrap_limits::bootstrap_frontier_retry_limit;
constexpr double nano::bootstrap_limits::bootstrap_minimum_termination_time_sec;
constexpr unsigned nano::bootstrap_limits::bootstrap_max_new_connections;
constexpr std::chrono::seconds nano::bootstrap_limits::lazy_flush_delay_sec;

nano::bootstrap_client::bootstrap_client (std::shared_ptr<nano::node> node_a, std::shared_ptr<nano::bootstrap_attempt> attempt_a, std::shared_ptr<nano::transport::channel_tcp> channel_a) :
node (node_a),
attempt (attempt_a),
channel (channel_a),
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
start_time (std::chrono::steady_clock::now ()),
block_count (0),
pending_stop (false),
hard_stop (false)
{
	++attempt->connections;
	receive_buffer->resize (256);
}

nano::bootstrap_client::~bootstrap_client ()
{
	--attempt->connections;
}

double nano::bootstrap_client::block_rate () const
{
	auto elapsed = std::max (elapsed_seconds (), nano::bootstrap_limits::bootstrap_minimum_elapsed_seconds_blockrate);
	return static_cast<double> (block_count.load () / elapsed);
}

double nano::bootstrap_client::elapsed_seconds () const
{
	return std::chrono::duration_cast<std::chrono::duration<double>> (std::chrono::steady_clock::now () - start_time).count ();
}

void nano::bootstrap_client::stop (bool force)
{
	pending_stop = true;
	if (force)
	{
		hard_stop = true;
	}
}

std::shared_ptr<nano::bootstrap_client> nano::bootstrap_client::shared ()
{
	return shared_from_this ();
}

nano::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<nano::node> node_a, nano::bootstrap_mode mode_a) :
next_log (std::chrono::steady_clock::now ()),
connections (0),
pulling (0),
node (node_a),
account_count (0),
total_blocks (0),
runs_count (0),
stopped (false),
mode (mode_a)
{
	node->logger.always_log ("Starting bootstrap attempt");
	node->bootstrap_initiator.notify_listeners (true);
}

nano::bootstrap_attempt::~bootstrap_attempt ()
{
	node->logger.always_log ("Exiting bootstrap attempt");
	node->bootstrap_initiator.notify_listeners (false);
}

bool nano::bootstrap_attempt::should_log ()
{
	nano::lock_guard<std::mutex> guard (next_log_mutex);
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		result = true;
		next_log = now + std::chrono::seconds (15);
	}
	return result;
}

bool nano::bootstrap_attempt::request_frontier (nano::unique_lock<std::mutex> & lock_a)
{
	auto result (true);
	auto connection_l (connection (lock_a));
	connection_frontier_request = connection_l;
	if (connection_l)
	{
		std::future<bool> future;
		{
			auto client (std::make_shared<nano::frontier_req_client> (connection_l));
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
				node->logger.try_log (boost::str (boost::format ("Completed frontier request, %1% out of sync accounts according to %2%") % pulls.size () % connection_l->channel->to_string ()));
			}
			else
			{
				node->stats.inc (nano::stat::type::error, nano::stat::detail::frontier_req, nano::stat::dir::out);
			}
		}
	}
	return result;
}

void nano::bootstrap_attempt::request_pull (nano::unique_lock<std::mutex> & lock_a)
{
	auto connection_l (connection (lock_a));
	if (connection_l)
	{
		auto pull (pulls.front ());
		pulls.pop_front ();
		if (mode != nano::bootstrap_mode::legacy)
		{
			// Check if pull is obsolete (head was processed)
			nano::unique_lock<std::mutex> lock (lazy_mutex);
			auto transaction (node->store.tx_begin_read ());
			while (!pulls.empty () && !pull.head.is_zero () && (lazy_blocks.find (pull.head) != lazy_blocks.end () || node->store.block_exists (transaction, pull.head)))
			{
				pull = pulls.front ();
				pulls.pop_front ();
			}
		}
		++pulling;
		// The bulk_pull_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
		// Dispatch request in an external thread in case it needs to be destroyed
		node->background ([connection_l, pull]() {
			auto client (std::make_shared<nano::bulk_pull_client> (connection_l, pull));
			client->request ();
		});
	}
}

void nano::bootstrap_attempt::request_push (nano::unique_lock<std::mutex> & lock_a)
{
	bool error (false);
	if (auto connection_shared = connection_frontier_request.lock ())
	{
		std::future<bool> future;
		{
			auto client (std::make_shared<nano::bulk_push_client> (connection_shared));
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
		node->logger.try_log ("Exiting bulk push client");
		if (error)
		{
			node->logger.try_log ("Bulk push client failed");
		}
	}
}

bool nano::bootstrap_attempt::still_pulling ()
{
	assert (!mutex.try_lock ());
	auto running (!stopped);
	auto more_pulls (!pulls.empty ());
	auto still_pulling (pulling > 0);
	return running && (more_pulls || still_pulling);
}

void nano::bootstrap_attempt::run ()
{
	assert (!node->flags.disable_legacy_bootstrap);
	populate_connections ();
	nano::unique_lock<std::mutex> lock (mutex);
	auto frontier_failure (true);
	while (!stopped && frontier_failure)
	{
		frontier_failure = request_frontier (lock);
	}
	// Shuffle pulls.
	release_assert (std::numeric_limits<CryptoPP::word32>::max () > pulls.size ());
	if (!pulls.empty ())
	{
		for (auto i = static_cast<CryptoPP::word32> (pulls.size () - 1); i > 0; --i)
		{
			auto k = nano::random_pool::generate_word32 (0, i);
			std::swap (pulls[i], pulls[k]);
		}
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
		node->logger.try_log ("Flushing unchecked blocks");
		lock.unlock ();
		node->block_processor.flush ();
		lock.lock ();
		node->logger.try_log ("Finished flushing unchecked blocks");
	}
	if (!stopped)
	{
		node->logger.try_log ("Completed pulls");
		request_push (lock);
		++runs_count;
		// Start wallet lazy bootstrap if required
		if (!wallet_accounts.empty () && !node->flags.disable_wallet_bootstrap)
		{
			lock.unlock ();
			mode = nano::bootstrap_mode::wallet_lazy;
			wallet_run ();
			lock.lock ();
		}
		// Start lazy bootstrap if some lazy keys were inserted
		else if (runs_count < 3 && !lazy_finished () && !node->flags.disable_lazy_bootstrap)
		{
			lock.unlock ();
			mode = nano::bootstrap_mode::lazy;
			lazy_run ();
			lock.lock ();
		}
		node->unchecked_cleanup ();
	}
	stopped = true;
	condition.notify_all ();
	idle.clear ();
}

std::shared_ptr<nano::bootstrap_client> nano::bootstrap_attempt::connection (nano::unique_lock<std::mutex> & lock_a)
{
	// clang-format off
	condition.wait (lock_a, [& stopped = stopped, &idle = idle] { return stopped || !idle.empty (); });
	// clang-format on
	std::shared_ptr<nano::bootstrap_client> result;
	if (!idle.empty ())
	{
		result = idle.back ();
		idle.pop_back ();
	}
	return result;
}

bool nano::bootstrap_attempt::consume_future (std::future<bool> & future_a)
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
	bool operator() (const std::shared_ptr<nano::bootstrap_client> & lhs, const std::shared_ptr<nano::bootstrap_client> & rhs) const
	{
		return lhs->block_rate () > rhs->block_rate ();
	}
};

unsigned nano::bootstrap_attempt::target_connections (size_t pulls_remaining)
{
	if (node->config.bootstrap_connections >= node->config.bootstrap_connections_max)
	{
		return std::max (1U, node->config.bootstrap_connections_max);
	}

	// Only scale up to bootstrap_connections_max for large pulls.
	double step_scale = std::min (1.0, std::max (0.0, (double)pulls_remaining / nano::bootstrap_limits::bootstrap_connection_scale_target_blocks));
	double lazy_term = (mode == nano::bootstrap_mode::lazy) ? (double)node->config.bootstrap_connections : 0.0;
	double target = (double)node->config.bootstrap_connections + (double)(node->config.bootstrap_connections_max - node->config.bootstrap_connections) * step_scale + lazy_term;
	return std::max (1U, (unsigned)(target + 0.5f));
}

void nano::bootstrap_attempt::populate_connections ()
{
	double rate_sum = 0.0;
	size_t num_pulls = 0;
	std::priority_queue<std::shared_ptr<nano::bootstrap_client>, std::vector<std::shared_ptr<nano::bootstrap_client>>, block_rate_cmp> sorted_connections;
	std::unordered_set<nano::tcp_endpoint> endpoints;
	{
		nano::unique_lock<std::mutex> lock (mutex);
		num_pulls = pulls.size ();
		std::deque<std::weak_ptr<nano::bootstrap_client>> new_clients;
		for (auto & c : clients)
		{
			if (auto client = c.lock ())
			{
				new_clients.push_back (client);
				endpoints.insert (client->channel->socket->remote_endpoint ());
				double elapsed_sec = client->elapsed_seconds ();
				auto blocks_per_sec = client->block_rate ();
				rate_sum += blocks_per_sec;
				if (client->elapsed_seconds () > nano::bootstrap_limits::bootstrap_connection_warmup_time_sec && client->block_count > 0)
				{
					sorted_connections.push (client);
				}
				// Force-stop the slowest peers, since they can take the whole bootstrap hostage by dribbling out blocks on the last remaining pull.
				// This is ~1.5kilobits/sec.
				if (elapsed_sec > nano::bootstrap_limits::bootstrap_minimum_termination_time_sec && blocks_per_sec < nano::bootstrap_limits::bootstrap_minimum_blocks_per_sec)
				{
					if (node->config.logging.bulk_pull_logging ())
					{
						node->logger.try_log (boost::str (boost::format ("Stopping slow peer %1% (elapsed sec %2%s > %3%s and %4% blocks per second < %5%)") % client->channel->to_string () % elapsed_sec % nano::bootstrap_limits::bootstrap_minimum_termination_time_sec % blocks_per_sec % nano::bootstrap_limits::bootstrap_minimum_blocks_per_sec));
					}

					client->stop (true);
				}
			}
		}
		// Cleanup expired clients
		clients.swap (new_clients);
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
			node->logger.try_log (boost::str (boost::format ("Dropping %1% bulk pull peers, target connections %2%") % drop % target));
		}

		for (int i = 0; i < drop; i++)
		{
			auto client = sorted_connections.top ();

			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log (boost::str (boost::format ("Dropping peer with block rate %1%, block count %2% (%3%) ") % client->block_rate () % client->block_count % client->channel->to_string ()));
			}

			client->stop (false);
			sorted_connections.pop ();
		}
	}

	if (node->config.logging.bulk_pull_logging ())
	{
		nano::unique_lock<std::mutex> lock (mutex);
		node->logger.try_log (boost::str (boost::format ("Bulk pull connections: %1%, rate: %2% blocks/sec, remaining account pulls: %3%, total blocks: %4%") % connections.load () % (int)rate_sum % pulls.size () % (int)total_blocks.load ()));
	}

	if (connections < target)
	{
		auto delta = std::min ((target - connections) * 2, nano::bootstrap_limits::bootstrap_max_new_connections);
		// TODO - tune this better
		// Not many peers respond, need to try to make more connections than we need.
		for (auto i = 0u; i < delta; i++)
		{
			auto endpoint (node->network.bootstrap_peer (mode == nano::bootstrap_mode::lazy));
			if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0) && endpoints.find (endpoint) == endpoints.end ())
			{
				connect_client (endpoint);
				nano::lock_guard<std::mutex> lock (mutex);
				endpoints.insert (endpoint);
			}
			else if (connections == 0)
			{
				node->logger.try_log (boost::str (boost::format ("Bootstrap stopped because there are no peers")));
				stopped = true;
				condition.notify_all ();
			}
		}
	}
	if (!stopped)
	{
		std::weak_ptr<nano::bootstrap_attempt> this_w (shared_from_this ());
		node->alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_w]() {
			if (auto this_l = this_w.lock ())
			{
				this_l->populate_connections ();
			}
		});
	}
}

void nano::bootstrap_attempt::add_connection (nano::endpoint const & endpoint_a)
{
	connect_client (nano::tcp_endpoint (endpoint_a.address (), endpoint_a.port ()));
}

void nano::bootstrap_attempt::connect_client (nano::tcp_endpoint const & endpoint_a)
{
	++connections;
	auto socket (std::make_shared<nano::socket> (node));
	auto this_l (shared_from_this ());
	socket->async_connect (endpoint_a,
	[this_l, socket, endpoint_a](boost::system::error_code const & ec) {
		if (!ec)
		{
			if (this_l->node->config.logging.bulk_pull_logging ())
			{
				this_l->node->logger.try_log (boost::str (boost::format ("Connection established to %1%") % endpoint_a));
			}
			auto client (std::make_shared<nano::bootstrap_client> (this_l->node, this_l, std::make_shared<nano::transport::channel_tcp> (*this_l->node, socket)));
			this_l->pool_connection (client);
		}
		else
		{
			if (this_l->node->config.logging.network_logging ())
			{
				switch (ec.value ())
				{
					default:
						this_l->node->logger.try_log (boost::str (boost::format ("Error initiating bootstrap connection to %1%: %2%") % endpoint_a % ec.message ()));
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
		--this_l->connections;
	});
}

void nano::bootstrap_attempt::pool_connection (std::shared_ptr<nano::bootstrap_client> client_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	if (!stopped && !client_a->pending_stop)
	{
		// Idle bootstrap client socket
		client_a->channel->socket->start_timer (node->network_params.node.idle_timeout);
		// Push into idle deque
		idle.push_front (client_a);
	}
	condition.notify_all ();
}

void nano::bootstrap_attempt::stop ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
	for (auto i : clients)
	{
		if (auto client = i.lock ())
		{
			client->channel->socket->close ();
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

void nano::bootstrap_attempt::add_pull (nano::pull_info const & pull_a)
{
	nano::pull_info pull (pull_a);
	node->bootstrap_initiator.cache.update_pull (pull);
	{
		nano::lock_guard<std::mutex> lock (mutex);
		pulls.push_back (pull);
	}
	condition.notify_all ();
}

void nano::bootstrap_attempt::requeue_pull (nano::pull_info const & pull_a)
{
	auto pull (pull_a);
	if (++pull.attempts < (nano::bootstrap_limits::bootstrap_frontier_retry_limit + (pull.processed / 10000)))
	{
		nano::lock_guard<std::mutex> lock (mutex);
		pulls.push_front (pull);
		condition.notify_all ();
	}
	else if (mode == nano::bootstrap_mode::lazy)
	{
		assert (pull.root == pull.head);
		if (!lazy_processed_or_exists (pull.root))
		{
			// Retry for lazy pulls (not weak state block link assumptions)
			nano::lock_guard<std::mutex> lock (mutex);
			pull.attempts++;
			pulls.push_back (pull);
			condition.notify_all ();
		}
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Failed to pull account %1% down to %2% after %3% attempts and %4% blocks processed") % pull.root.to_account () % pull.end.to_string () % pull.attempts % pull.processed));
		}
		node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::bulk_pull_failed_account, nano::stat::dir::in);

		node->bootstrap_initiator.cache.add (pull);
	}
}

void nano::bootstrap_attempt::add_bulk_push_target (nano::block_hash const & head, nano::block_hash const & end)
{
	nano::lock_guard<std::mutex> lock (mutex);
	bulk_push_targets.push_back (std::make_pair (head, end));
}

void nano::bootstrap_attempt::lazy_start (nano::block_hash const & hash_a)
{
	nano::unique_lock<std::mutex> lock (lazy_mutex);
	// Add start blocks, limit 1024 (32k with disabled legacy bootstrap)
	size_t max_keys (node->flags.disable_legacy_bootstrap ? 32 * 1024 : 1024);
	if (lazy_keys.size () < max_keys && lazy_keys.find (hash_a) == lazy_keys.end () && lazy_blocks.find (hash_a) == lazy_blocks.end ())
	{
		lazy_keys.insert (hash_a);
		lazy_pulls.push_back (hash_a);
	}
}

void nano::bootstrap_attempt::lazy_add (nano::block_hash const & hash_a)
{
	// Add only unknown blocks
	assert (!lazy_mutex.try_lock ());
	if (lazy_blocks.find (hash_a) == lazy_blocks.end ())
	{
		lazy_pulls.push_back (hash_a);
	}
}

void nano::bootstrap_attempt::lazy_requeue (nano::block_hash const & hash_a)
{
	nano::unique_lock<std::mutex> lock (lazy_mutex);
	// Add only known blocks
	auto existing (lazy_blocks.find (hash_a));
	if (existing != lazy_blocks.end ())
	{
		lazy_blocks.erase (existing);
		lazy_mutex.unlock ();
		requeue_pull (nano::pull_info (hash_a, hash_a, nano::block_hash (0), static_cast<nano::pull_info::count_t> (1)));
	}
}

void nano::bootstrap_attempt::lazy_pull_flush ()
{
	assert (!mutex.try_lock ());
	last_lazy_flush = std::chrono::steady_clock::now ();
	nano::unique_lock<std::mutex> lazy_lock (lazy_mutex);
	auto transaction (node->store.tx_begin_read ());
	for (auto & pull_start : lazy_pulls)
	{
		// Recheck if block was already processed
		if (lazy_blocks.find (pull_start) == lazy_blocks.end () && !node->store.block_exists (transaction, pull_start))
		{
			assert (node->network_params.bootstrap.lazy_max_pull_blocks <= std::numeric_limits<nano::pull_info::count_t>::max ());
			pulls.push_back (nano::pull_info (pull_start, pull_start, nano::block_hash (0), static_cast<nano::pull_info::count_t> (node->network_params.bootstrap.lazy_max_pull_blocks)));
		}
	}
	lazy_pulls.clear ();
}

bool nano::bootstrap_attempt::lazy_finished ()
{
	bool result (true);
	auto transaction (node->store.tx_begin_read ());
	nano::unique_lock<std::mutex> lock (lazy_mutex);
	for (auto it (lazy_keys.begin ()), end (lazy_keys.end ()); it != end && !stopped;)
	{
		if (node->store.block_exists (transaction, *it))
		{
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
	if (!result && lazy_pulls.empty () && lazy_state_backlog.empty ())
	{
		result = true;
	}
	return result;
}

void nano::bootstrap_attempt::lazy_clear ()
{
	assert (!lazy_mutex.try_lock ());
	lazy_blocks.clear ();
	lazy_keys.clear ();
	lazy_pulls.clear ();
	lazy_state_backlog.clear ();
	lazy_balances.clear ();
}

void nano::bootstrap_attempt::lazy_run ()
{
	assert (!node->flags.disable_lazy_bootstrap);
	populate_connections ();
	auto start_time (std::chrono::steady_clock::now ());
	auto max_time (std::chrono::minutes (node->flags.disable_legacy_bootstrap ? 48 * 60 : 30));
	nano::unique_lock<std::mutex> lock (mutex);
	while ((still_pulling () || !lazy_finished ()) && std::chrono::steady_clock::now () - start_time < max_time)
	{
		unsigned iterations (0);
		while (still_pulling () && std::chrono::steady_clock::now () - start_time < max_time)
		{
			if (!pulls.empty ())
			{
				request_pull (lock);
			}
			else
			{
				lazy_pull_flush ();
				if (pulls.empty ())
				{
					condition.wait_for (lock, std::chrono::seconds (2));
				}
			}
			++iterations;
			// Flushing lazy pulls
			if (iterations % 100 == 0 || last_lazy_flush + nano::bootstrap_limits::lazy_flush_delay_sec < std::chrono::steady_clock::now ())
			{
				lazy_pull_flush ();
			}
		}
		// Flushing lazy pulls
		lazy_pull_flush ();
		// Check if some blocks required for backlog were processed
		if (pulls.empty ())
		{
			lazy_backlog_cleanup ();
		}
	}
	if (!stopped)
	{
		node->logger.try_log ("Completed lazy pulls");
		nano::unique_lock<std::mutex> lazy_lock (lazy_mutex);
		++runs_count;
		// Start wallet lazy bootstrap if required
		if (!wallet_accounts.empty () && !node->flags.disable_wallet_bootstrap)
		{
			pulls.clear ();
			lazy_clear ();
			mode = nano::bootstrap_mode::wallet_lazy;
			lock.unlock ();
			lazy_lock.unlock ();
			wallet_run ();
			lock.lock ();
		}
		// Fallback to legacy bootstrap
		else if (runs_count < 3 && !lazy_keys.empty () && !node->flags.disable_legacy_bootstrap)
		{
			pulls.clear ();
			lazy_clear ();
			mode = nano::bootstrap_mode::legacy;
			lock.unlock ();
			lazy_lock.unlock ();
			run ();
			lock.lock ();
		}
	}
	stopped = true;
	condition.notify_all ();
	idle.clear ();
}

bool nano::bootstrap_attempt::process_block (std::shared_ptr<nano::block> block_a, nano::account const & known_account_a, uint64_t pull_blocks, bool block_expected)
{
	bool stop_pull (false);
	if (mode != nano::bootstrap_mode::legacy && block_expected)
	{
		stop_pull = process_block_lazy (block_a, known_account_a, pull_blocks);
	}
	else if (mode != nano::bootstrap_mode::legacy)
	{
		// Drop connection with unexpected block for lazy bootstrap
		stop_pull = true;
	}
	else
	{
		nano::unchecked_info info (block_a, known_account_a, 0, nano::signature_verification::unknown);
		node->block_processor.add (info);
	}
	return stop_pull;
}

bool nano::bootstrap_attempt::process_block_lazy (std::shared_ptr<nano::block> block_a, nano::account const & known_account_a, uint64_t pull_blocks)
{
	bool stop_pull (false);
	auto hash (block_a->hash ());
	nano::unique_lock<std::mutex> lock (lazy_mutex);
	// Processing new blocks
	if (lazy_blocks.find (hash) == lazy_blocks.end ())
	{
		nano::unchecked_info info (block_a, known_account_a, 0, nano::signature_verification::unknown);
		node->block_processor.add (info);
		// Search for new dependencies
		if (!block_a->source ().is_zero () && !node->ledger.block_exists (block_a->source ()) && block_a->source () != node->network_params.ledger.genesis_account)
		{
			lazy_add (block_a->source ());
		}
		else if (block_a->type () == nano::block_type::state || block_a->type () == nano::block_type::state2)
		{
			lazy_block_state (block_a);
		}
		lazy_blocks.insert (hash);
		// Adding lazy balances for first processed block in pull
		if (pull_blocks == 0 && (block_a->type () == nano::block_type::state || block_a->type () == nano::block_type::state2 || block_a->type () == nano::block_type::send))
		{
			lazy_balances.insert (std::make_pair (hash, block_a->balance ().number ()));
		}
		// Clearing lazy balances for previous block
		if (!block_a->previous ().is_zero () && lazy_balances.find (block_a->previous ()) != lazy_balances.end ())
		{
			lazy_balances.erase (block_a->previous ());
		}
		lazy_block_state_backlog_check (block_a, hash);
	}
	// Force drop lazy bootstrap connection for long bulk_pull
	if (pull_blocks > node->network_params.bootstrap.lazy_max_pull_blocks)
	{
		stop_pull = true;
	}
	return stop_pull;
}

void nano::bootstrap_attempt::lazy_block_state (std::shared_ptr<nano::block> block_a)
{
	std::shared_ptr<nano::state_block> block_l (std::static_pointer_cast<nano::state_block> (block_a));
	if (block_l != nullptr)
	{
		auto transaction (node->store.tx_begin_read ());
		nano::uint128_t balance (block_l->hashables.balance.number ());
		auto const & link (block_l->hashables.link);
		// If link is not epoch link or 0. And if block from link is unknown
		if (!link.is_zero () && !node->ledger.is_epoch_link (link) && lazy_blocks.find (link) == lazy_blocks.end () && !node->store.block_exists (transaction, link))
		{
			auto const & previous (block_l->hashables.previous);
			// If state block previous is 0 then source block required
			if (previous.is_zero ())
			{
				lazy_add (link);
			}
			// In other cases previous block balance required to find out subtype of state block
			else if (node->store.block_exists (transaction, previous))
			{
				if (node->ledger.balance (transaction, previous) <= balance)
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
			// Insert in backlog state blocks if previous wasn't already processed
			else
			{
				lazy_state_backlog.insert (std::make_pair (previous, std::make_pair (link, balance)));
			}
		}
	}
}

void nano::bootstrap_attempt::lazy_block_state_backlog_check (std::shared_ptr<nano::block> block_a, nano::block_hash const & hash_a)
{
	// Search unknown state blocks balances
	auto find_state (lazy_state_backlog.find (hash_a));
	if (find_state != lazy_state_backlog.end ())
	{
		auto next_block (find_state->second);
		// Retrieve balance for previous state & send blocks
		if (block_a->type () == nano::block_type::state || block_a->type () == nano::block_type::state2 || block_a->type () == nano::block_type::send)
		{
			if (block_a->balance ().number () <= next_block.second) // balance
			{
				lazy_add (next_block.first); // link
			}
		}
		// Assumption for other legacy block types
		else
		{
			// Disabled
		}
		lazy_state_backlog.erase (find_state);
	}
}

void nano::bootstrap_attempt::lazy_backlog_cleanup ()
{
	auto transaction (node->store.tx_begin_read ());
	nano::lock_guard<std::mutex> lock (lazy_mutex);
	for (auto it (lazy_state_backlog.begin ()), end (lazy_state_backlog.end ()); it != end && !stopped;)
	{
		if (node->store.block_exists (transaction, it->first))
		{
			auto next_block (it->second);
			if (node->ledger.balance (transaction, it->first) <= next_block.second) // balance
			{
				lazy_add (next_block.first); // link
			}
			it = lazy_state_backlog.erase (it);
		}
		else
		{
			++it;
		}
	}
}

bool nano::bootstrap_attempt::lazy_processed_or_exists (nano::block_hash const & hash_a)
{
	bool result (false);
	nano::unique_lock<std::mutex> lazy_lock (lazy_mutex);
	if (lazy_blocks.find (hash_a) != lazy_blocks.end ())
	{
		result = true;
	}
	else
	{
		lazy_lock.unlock ();
		if (node->ledger.block_exists (hash_a))
		{
			result = true;
		}
	}
	return result;
}

void nano::bootstrap_attempt::request_pending (nano::unique_lock<std::mutex> & lock_a)
{
	auto connection_l (connection (lock_a));
	if (connection_l)
	{
		auto account (wallet_accounts.front ());
		wallet_accounts.pop_front ();
		++pulling;
		// The bulk_pull_account_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
		// Dispatch request in an external thread in case it needs to be destroyed
		node->background ([connection_l, account]() {
			auto client (std::make_shared<nano::bulk_pull_account_client> (connection_l, account));
			client->request ();
		});
	}
}

void nano::bootstrap_attempt::requeue_pending (nano::account const & account_a)
{
	auto account (account_a);
	{
		nano::lock_guard<std::mutex> lock (mutex);
		wallet_accounts.push_front (account);
		condition.notify_all ();
	}
}

void nano::bootstrap_attempt::wallet_start (std::deque<nano::account> & accounts_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	wallet_accounts.swap (accounts_a);
}

bool nano::bootstrap_attempt::wallet_finished ()
{
	assert (!mutex.try_lock ());
	auto running (!stopped);
	auto more_accounts (!wallet_accounts.empty ());
	auto still_pulling (pulling > 0);
	return running && (more_accounts || still_pulling);
}

void nano::bootstrap_attempt::wallet_run ()
{
	assert (!node->flags.disable_wallet_bootstrap);
	populate_connections ();
	auto start_time (std::chrono::steady_clock::now ());
	auto max_time (std::chrono::minutes (10));
	nano::unique_lock<std::mutex> lock (mutex);
	while (wallet_finished () && std::chrono::steady_clock::now () - start_time < max_time)
	{
		if (!wallet_accounts.empty ())
		{
			request_pending (lock);
		}
		else
		{
			condition.wait (lock);
		}
	}
	if (!stopped)
	{
		node->logger.try_log ("Completed wallet lazy pulls");
		++runs_count;
		// Start lazy bootstrap if some lazy keys were inserted
		if (!lazy_finished ())
		{
			lock.unlock ();
			lazy_run ();
			lock.lock ();
		}
	}
	stopped = true;
	condition.notify_all ();
	idle.clear ();
}

nano::bootstrap_initiator::bootstrap_initiator (nano::node & node_a) :
node (node_a),
stopped (false),
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::bootstrap_initiator);
	run_bootstrap ();
})
{
}

nano::bootstrap_initiator::~bootstrap_initiator ()
{
	stop ();
}

void nano::bootstrap_initiator::bootstrap ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	if (!stopped && attempt == nullptr)
	{
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate, nano::stat::dir::out);
		attempt = std::make_shared<nano::bootstrap_attempt> (node.shared ());
		condition.notify_all ();
	}
}

void nano::bootstrap_initiator::bootstrap (nano::endpoint const & endpoint_a, bool add_to_peers)
{
	if (add_to_peers)
	{
		node.network.udp_channels.insert (nano::transport::map_endpoint_to_v6 (endpoint_a), node.network_params.protocol.protocol_version);
	}
	nano::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		if (attempt != nullptr)
		{
			attempt->stop ();
			// clang-format off
			condition.wait (lock, [attempt = attempt, &stopped = stopped] { return stopped || attempt == nullptr; });
			// clang-format on
		}
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate, nano::stat::dir::out);
		attempt = std::make_shared<nano::bootstrap_attempt> (node.shared ());
		attempt->add_connection (endpoint_a);
		condition.notify_all ();
	}
}

void nano::bootstrap_initiator::bootstrap_lazy (nano::block_hash const & hash_a, bool force)
{
	{
		nano::unique_lock<std::mutex> lock (mutex);
		if (force)
		{
			if (attempt != nullptr)
			{
				attempt->stop ();
				// clang-format off
				condition.wait (lock, [attempt = attempt, &stopped = stopped] { return stopped || attempt == nullptr; });
				// clang-format on
			}
		}
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate_lazy, nano::stat::dir::out);
		if (attempt == nullptr)
		{
			attempt = std::make_shared<nano::bootstrap_attempt> (node.shared (), nano::bootstrap_mode::lazy);
		}
		attempt->lazy_start (hash_a);
	}
	condition.notify_all ();
}

void nano::bootstrap_initiator::bootstrap_wallet (std::deque<nano::account> & accounts_a)
{
	{
		nano::unique_lock<std::mutex> lock (mutex);
		node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate_wallet_lazy, nano::stat::dir::out);
		if (attempt == nullptr)
		{
			attempt = std::make_shared<nano::bootstrap_attempt> (node.shared (), nano::bootstrap_mode::wallet_lazy);
		}
		attempt->wallet_start (accounts_a);
	}
	condition.notify_all ();
}

void nano::bootstrap_initiator::run_bootstrap ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (attempt != nullptr)
		{
			lock.unlock ();
			if (attempt->mode == nano::bootstrap_mode::legacy)
			{
				attempt->run ();
			}
			else if (attempt->mode == nano::bootstrap_mode::lazy)
			{
				attempt->lazy_run ();
			}
			else
			{
				attempt->wallet_run ();
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

void nano::bootstrap_initiator::add_observer (std::function<void(bool)> const & observer_a)
{
	nano::lock_guard<std::mutex> lock (observers_mutex);
	observers.push_back (observer_a);
}

bool nano::bootstrap_initiator::in_progress ()
{
	return current_attempt () != nullptr;
}

std::shared_ptr<nano::bootstrap_attempt> nano::bootstrap_initiator::current_attempt ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return attempt;
}

void nano::bootstrap_initiator::stop ()
{
	if (!stopped.exchange (true))
	{
		{
			nano::lock_guard<std::mutex> guard (mutex);
			if (attempt != nullptr)
			{
				attempt->stop ();
			}
		}
		condition.notify_all ();

		if (thread.joinable ())
		{
			thread.join ();
		}
	}
}

void nano::bootstrap_initiator::notify_listeners (bool in_progress_a)
{
	nano::lock_guard<std::mutex> lock (observers_mutex);
	for (auto & i : observers)
	{
		i (in_progress_a);
	}
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_initiator & bootstrap_initiator, const std::string & name)
{
	size_t count = 0;
	size_t cache_count = 0;
	{
		nano::lock_guard<std::mutex> guard (bootstrap_initiator.observers_mutex);
		count = bootstrap_initiator.observers.size ();
	}
	{
		nano::lock_guard<std::mutex> guard (bootstrap_initiator.cache.pulls_cache_mutex);
		cache_count = bootstrap_initiator.cache.cache.size ();
	}

	auto sizeof_element = sizeof (decltype (bootstrap_initiator.observers)::value_type);
	auto sizeof_cache_element = sizeof (decltype (bootstrap_initiator.cache.cache)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "observers", count, sizeof_element }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "pulls_cache", cache_count, sizeof_cache_element }));
	return composite;
}
}

void nano::pulls_cache::add (nano::pull_info const & pull_a)
{
	if (pull_a.processed > 500)
	{
		nano::lock_guard<std::mutex> guard (pulls_cache_mutex);
		// Clean old pull
		if (cache.size () > cache_size_max)
		{
			cache.erase (cache.begin ());
		}
		assert (cache.size () <= cache_size_max);
		nano::uint512_union head_512 (pull_a.root, pull_a.head_original);
		auto existing (cache.get<account_head_tag> ().find (head_512));
		if (existing == cache.get<account_head_tag> ().end ())
		{
			// Insert new pull
			auto inserted (cache.insert (nano::cached_pulls{ std::chrono::steady_clock::now (), head_512, pull_a.head }));
			(void)inserted;
			assert (inserted.second);
		}
		else
		{
			// Update existing pull
			cache.get<account_head_tag> ().modify (existing, [pull_a](nano::cached_pulls & cache_a) {
				cache_a.time = std::chrono::steady_clock::now ();
				cache_a.new_head = pull_a.head;
			});
		}
	}
}

void nano::pulls_cache::update_pull (nano::pull_info & pull_a)
{
	nano::lock_guard<std::mutex> guard (pulls_cache_mutex);
	nano::uint512_union head_512 (pull_a.root, pull_a.head_original);
	auto existing (cache.get<account_head_tag> ().find (head_512));
	if (existing != cache.get<account_head_tag> ().end ())
	{
		pull_a.head = existing->new_head;
	}
}

void nano::pulls_cache::remove (nano::pull_info const & pull_a)
{
	nano::lock_guard<std::mutex> guard (pulls_cache_mutex);
	nano::uint512_union head_512 (pull_a.root, pull_a.head_original);
	cache.get<account_head_tag> ().erase (head_512);
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (pulls_cache & pulls_cache, const std::string & name)
{
	size_t cache_count = 0;

	{
		nano::lock_guard<std::mutex> guard (pulls_cache.pulls_cache_mutex);
		cache_count = pulls_cache.cache.size ();
	}
	auto sizeof_element = sizeof (decltype (pulls_cache.cache)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "pulls_cache", cache_count, sizeof_element }));
	return composite;
}
}
