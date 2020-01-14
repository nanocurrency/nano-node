#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_connections.hpp>
#include <nano/node/common.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>

#include <boost/format.hpp>

constexpr double nano::bootstrap_limits::bootstrap_connection_scale_target_blocks;
constexpr double nano::bootstrap_limits::bootstrap_minimum_blocks_per_sec;
constexpr double nano::bootstrap_limits::bootstrap_minimum_termination_time_sec;
constexpr unsigned nano::bootstrap_limits::bootstrap_max_new_connections;

nano::bootstrap_client::bootstrap_client (std::shared_ptr<nano::node> node_a, std::shared_ptr<nano::bootstrap_connections> connections_a, std::shared_ptr<nano::transport::channel_tcp> channel_a, std::shared_ptr<nano::socket> socket_a) :
node (node_a),
connections (connections_a),
channel (channel_a),
socket (socket_a),
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
start_time (std::chrono::steady_clock::now ())
{
	++connections->connections_count;
	receive_buffer->resize (256);
}

nano::bootstrap_client::~bootstrap_client ()
{
	--connections->connections_count;
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

nano::bootstrap_connections::bootstrap_connections (nano::node & node_a) :
node (node_a)
{
}

std::shared_ptr<nano::bootstrap_client> nano::bootstrap_connections::connection (std::shared_ptr<nano::bootstrap_attempt> attempt_a, bool use_front_connection)
{
	nano::unique_lock<std::mutex> lock (mutex);
	// clang-format off
	condition.wait (lock, [& stopped = stopped, &idle = idle, &new_connections_empty = new_connections_empty] { return stopped || !idle.empty () || new_connections_empty; });
	// clang-format on
	std::shared_ptr<nano::bootstrap_client> result;
	if (!idle.empty ())
	{
		if (!use_front_connection)
		{
			result = idle.back ();
			idle.pop_back ();
		}
		else
		{
			result = idle.front ();
			idle.pop_front ();
		}
	}
	if (result == nullptr && connections_count == 0 && new_connections_empty && attempt_a != nullptr)
	{
		node.logger.try_log (boost::str (boost::format ("Bootstrap attempt stopped because there are no peers")));
		attempt_a->stopped = true;
	}
	return result;
}

void nano::bootstrap_connections::pool_connection (std::shared_ptr<nano::bootstrap_client> client_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	if (!stopped && !client_a->pending_stop && !node.bootstrap_initiator.excluded_peers.check (client_a->channel->get_tcp_endpoint ()))
	{
		// Idle bootstrap client socket
		if (auto socket_l = client_a->channel->socket.lock ())
		{
			socket_l->start_timer (node.network_params.node.idle_timeout);
			// Push into idle deque
			idle.push_back (client_a);
		}
	}
	condition.notify_all ();
}

void nano::bootstrap_connections::add_connection (nano::endpoint const & endpoint_a)
{
	connect_client (nano::tcp_endpoint (endpoint_a.address (), endpoint_a.port ()));
}

std::shared_ptr<nano::bootstrap_client> nano::bootstrap_connections::find_connection (nano::tcp_endpoint const & endpoint_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	std::shared_ptr<nano::bootstrap_client> result;
	for (auto i (idle.begin ()), end (idle.end ()); i != end; ++i)
	{
		if ((*i)->channel->get_tcp_endpoint () == endpoint_a)
		{
			result = *i;
			idle.erase (i);
			break;
		}
	}
	return result;
}

void nano::bootstrap_connections::connect_client (nano::tcp_endpoint const & endpoint_a)
{
	++connections_count;
	auto socket (std::make_shared<nano::socket> (node.shared ()));
	auto this_l (shared_from_this ());
	socket->async_connect (endpoint_a,
	[this_l, socket, endpoint_a](boost::system::error_code const & ec) {
		if (!ec)
		{
			if (this_l->node.config.logging.bulk_pull_logging ())
			{
				this_l->node.logger.try_log (boost::str (boost::format ("Connection established to %1%") % endpoint_a));
			}
			auto client (std::make_shared<nano::bootstrap_client> (this_l->node.shared (), this_l, std::make_shared<nano::transport::channel_tcp> (*this_l->node.shared (), socket), socket));
			this_l->pool_connection (client);
		}
		else
		{
			if (this_l->node.config.logging.network_logging ())
			{
				switch (ec.value ())
				{
					default:
						this_l->node.logger.try_log (boost::str (boost::format ("Error initiating bootstrap connection to %1%: %2%") % endpoint_a % ec.message ()));
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
		--this_l->connections_count;
	});
}

unsigned nano::bootstrap_connections::target_connections (size_t pulls_remaining)
{
	if (node.config.bootstrap_connections >= node.config.bootstrap_connections_max)
	{
		return std::max (1U, node.config.bootstrap_connections_max);
	}

	// Only scale up to bootstrap_connections_max for large pulls.
	double step_scale = std::min (1.0, std::max (0.0, (double)pulls_remaining / nano::bootstrap_limits::bootstrap_connection_scale_target_blocks));
	double target = (double)node.config.bootstrap_connections + (double)(node.config.bootstrap_connections_max - node.config.bootstrap_connections) * step_scale;
	return std::max (1U, (unsigned)(target + 0.5f));
}

struct block_rate_cmp
{
	bool operator() (const std::shared_ptr<nano::bootstrap_client> & lhs, const std::shared_ptr<nano::bootstrap_client> & rhs) const
	{
		return lhs->block_rate () > rhs->block_rate ();
	}
};

void nano::bootstrap_connections::populate_connections (bool repeat)
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
				if (auto socket_l = client->channel->socket.lock ())
				{
					new_clients.push_back (client);
					endpoints.insert (socket_l->remote_endpoint ());
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
						if (node.config.logging.bulk_pull_logging ())
						{
							node.logger.try_log (boost::str (boost::format ("Stopping slow peer %1% (elapsed sec %2%s > %3%s and %4% blocks per second < %5%)") % client->channel->to_string () % elapsed_sec % nano::bootstrap_limits::bootstrap_minimum_termination_time_sec % blocks_per_sec % nano::bootstrap_limits::bootstrap_minimum_blocks_per_sec));
						}

						client->stop (true);
						new_clients.pop_back ();
					}
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

		if (node.config.logging.bulk_pull_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Dropping %1% bulk pull peers, target connections %2%") % drop % target));
		}

		for (int i = 0; i < drop; i++)
		{
			auto client = sorted_connections.top ();

			if (node.config.logging.bulk_pull_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Dropping peer with block rate %1%, block count %2% (%3%) ") % client->block_rate () % client->block_count % client->channel->to_string ()));
			}

			client->stop (false);
			sorted_connections.pop ();
		}
	}

	if (node.config.logging.bulk_pull_logging ())
	{
		nano::unique_lock<std::mutex> lock (mutex);
		node.logger.try_log (boost::str (boost::format ("Bulk pull connections: %1%, rate: %2% blocks/sec, remaining pulls: %3%") % connections_count.load () % (int)rate_sum % num_pulls));
	}

	if (connections_count < target)
	{
		auto delta = std::min ((target - connections_count) * 2, nano::bootstrap_limits::bootstrap_max_new_connections);
		// TODO - tune this better
		// Not many peers respond, need to try to make more connections than we need.
		for (auto i = 0u; i < delta; i++)
		{
			auto endpoint (node.network.bootstrap_peer (true));
			if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0) && endpoints.find (endpoint) == endpoints.end () && !node.bootstrap_initiator.excluded_peers.check (endpoint))
			{
				connect_client (endpoint);
				nano::lock_guard<std::mutex> lock (mutex);
				endpoints.insert (endpoint);
				new_connections_empty = false;
			}
			else if (connections_count == 0)
			{
				new_connections_empty = true;
				condition.notify_all ();
			}
		}
	}
	if (!stopped && repeat)
	{
		std::weak_ptr<nano::bootstrap_connections> this_w (shared_from_this ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_w]() {
			if (auto this_l = this_w.lock ())
			{
				this_l->populate_connections ();
			}
		});
	}
}

void nano::bootstrap_connections::start_populate_connections ()
{
	if (!populate_connections_started.exchange (true))
	{
		populate_connections ();
	}
}

void nano::bootstrap_connections::add_pull (nano::pull_info const & pull_a)
{
	nano::pull_info pull (pull_a);
	node.bootstrap_initiator.cache.update_pull (pull);
	{
		nano::lock_guard<std::mutex> lock (mutex);
		pulls.push_back (pull);
	}
	++pull.attempt->pulling;
	condition.notify_all ();
}

void nano::bootstrap_connections::request_pull (nano::unique_lock<std::mutex> & lock_a)
{
	lock_a.unlock ();
	auto connection_l (connection ());
	lock_a.lock ();
	if (connection_l)
	{
		auto pull (pulls.front ());
		pulls.pop_front ();
		if (pull.attempt->mode == nano::bootstrap_mode::lazy)
		{
			// Check if pull is obsolete (head was processed)
			while (!pulls.empty () && !pull.head.is_zero () && pull.attempt->lazy_processed_or_exists (pull.head) && pull.attempt->mode == nano::bootstrap_mode::lazy)
			{
				pull = pulls.front ();
				pulls.pop_front ();
			}
		}
		if (pull.attempt->mode == nano::bootstrap_mode::legacy)
		{
			pull.attempt->add_recent_pull (pull.head);
		}
		// The bulk_pull_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
		// Dispatch request in an external thread in case it needs to be destroyed
		node.background ([connection_l, pull]() {
			auto client (std::make_shared<nano::bulk_pull_client> (connection_l, pull));
			client->request ();
		});
	}
}

void nano::bootstrap_connections::requeue_pull (nano::pull_info const & pull_a, bool network_error)
{
	auto pull (pull_a);
	assert (pull.attempt != nullptr);
	if (!pull.attempt->stopped)
	{
		if (!network_error)
		{
			++pull.attempts;
		}
		++pull.attempt->requeued_pulls;
		if (pull.attempt->mode == nano::bootstrap_mode::legacy)
		{
			pull.attempt->restart_condition ();
		}
		if (pull.attempts < pull.retry_limit + (pull.processed / 10000))
		{
			nano::lock_guard<std::mutex> lock (mutex);
			pulls.push_front (pull);
			++pull.attempt->pulling;
			condition.notify_all ();
		}
		else if (pull.attempt->mode == nano::bootstrap_mode::lazy && (pull.retry_limit == std::numeric_limits<unsigned>::max () || pull.attempts <= pull.retry_limit + (pull.processed / node.network_params.bootstrap.lazy_max_pull_blocks)))
		{
			assert (pull.account_or_head == pull.head);
			if (!pull.attempt->lazy_processed_or_exists (pull.account_or_head))
			{
				nano::lock_guard<std::mutex> lock (mutex);
				pulls.push_back (pull);
				++pull.attempt->pulling;
				condition.notify_all ();
			}
		}
		else
		{
			if (node.config.logging.bulk_pull_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Failed to pull account %1% down to %2% after %3% attempts and %4% blocks processed") % pull.account_or_head.to_account () % pull.end.to_string () % pull.attempts % pull.processed));
			}
			node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::bulk_pull_failed_account, nano::stat::dir::in);

			if (pull.attempt->mode == nano::bootstrap_mode::lazy && pull.processed > 0)
			{
				pull.attempt->lazy_add (pull);
			}
			if (pull.attempt->mode == nano::bootstrap_mode::legacy)
			{
				node.bootstrap_initiator.cache.add (pull);
			}
		}
	}
}

void nano::bootstrap_connections::clear_pulls (std::shared_ptr<nano::bootstrap_attempt> attempt_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	for (auto i (pulls.begin ()), end (pulls.end ()); i != end; ++i)
	{
		if (i->attempt == attempt_a)
		{
			pulls.erase (i);
		}
	}
}

void nano::bootstrap_connections::run ()
{
	start_populate_connections ();
	nano::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
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
	stopped = true;
	condition.notify_all ();
}

void nano::bootstrap_connections::stop ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
	for (auto i : clients)
	{
		if (auto client = i.lock ())
		{
			client->socket->close ();
		}
	}
}
