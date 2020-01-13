#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_bulk_push.hpp>
#include <nano/node/bootstrap/bootstrap_frontier.hpp>
#include <nano/node/common.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>

#include <boost/format.hpp>

#include <algorithm>

constexpr size_t nano::bootstrap_limits::bootstrap_max_confirm_frontiers;
constexpr double nano::bootstrap_limits::required_frontier_confirmation_ratio;
constexpr unsigned nano::bootstrap_limits::frontier_confirmation_blocks_limit;
constexpr unsigned nano::bootstrap_limits::requeued_pulls_limit;
constexpr unsigned nano::bootstrap_limits::requeued_pulls_limit_test;

nano::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<nano::node> node_a, nano::bootstrap_mode mode_a) :
node (node_a),
mode (mode_a)
{
	connections = std::make_shared<nano::bootstrap_connections> (node, condition);
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

bool nano::bootstrap_attempt::still_pulling ()
{
	assert (!mutex.try_lock ());
	auto running (!stopped && !connections->stopped);
	auto more_pulls (!pulls.empty ());
	auto still_pulling (pulling > 0);
	return running && (more_pulls || still_pulling);
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

void nano::bootstrap_attempt::stop ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
	connections->stop ();
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

void nano::bootstrap_attempt::add_frontier (nano::pull_info const & pull_a)
{
	nano::pull_info pull (pull_a);
	nano::lock_guard<std::mutex> lock (mutex);
	frontier_pulls.push_back (pull);
}

void nano::bootstrap_attempt::requeue_pull (nano::pull_info const & pull_a, bool network_error)
{
	auto pull (pull_a);
	if (!network_error)
	{
		++pull.attempts;
	}
	++requeued_pulls;
	if (pull.attempts < pull.retry_limit + (pull.processed / 10000))
	{
		nano::lock_guard<std::mutex> lock (mutex);
		pulls.push_front (pull);
		condition.notify_all ();
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Failed to pull account %1% down to %2% after %3% attempts and %4% blocks processed") % pull.account_or_head.to_account () % pull.end.to_string () % pull.attempts % pull.processed));
		}
		node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::bulk_pull_failed_account, nano::stat::dir::in);

		node->bootstrap_initiator.cache.add (pull);
	}
}

void nano::bootstrap_attempt::add_bulk_push_target (nano::block_hash const & head, nano::block_hash const & end)
{
	nano::lock_guard<std::mutex> lock (mutex);
	bulk_push_targets.emplace_back (head, end);
}

void nano::bootstrap_attempt_legacy::request_pull (nano::unique_lock<std::mutex> & lock_a)
{
	auto connection_l (connections->connection (lock_a));
	if (connection_l)
	{
		auto pull (pulls.front ());
		pulls.pop_front ();
		recent_pulls_head.push_back (pull.head);
		if (recent_pulls_head.size () > nano::bootstrap_limits::bootstrap_max_confirm_frontiers)
		{
			recent_pulls_head.pop_front ();
		}
		++pulling;
		auto this_l (shared_from_this ());
		// The bulk_pull_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
		// Dispatch request in an external thread in case it needs to be destroyed
		node->background ([connection_l, this_l, pull]() {
			auto client (std::make_shared<nano::bulk_pull_client> (connection_l, this_l, pull));
			client->request ();
		});
	}
}

void nano::bootstrap_attempt_legacy::request_push (nano::unique_lock<std::mutex> & lock_a)
{
	bool error (false);
	if (auto connection_shared = connection_frontier_request.lock ())
	{
		std::future<bool> future;
		{
			auto this_l (shared_from_this ());
			auto client (std::make_shared<nano::bulk_push_client> (connection_shared, this_l));
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

void nano::bootstrap_attempt_legacy::attempt_restart_check (nano::unique_lock<std::mutex> & lock_a)
{
	/* Conditions to start frontiers confirmation:
	- not completed frontiers confirmation
	- more than 256 pull retries usually indicating issues with requested pulls
	- or 128k processed blocks indicating large bootstrap */
	if (!frontiers_confirmed && (requeued_pulls > (!node->network_params.network.is_test_network () ? nano::bootstrap_limits::requeued_pulls_limit : nano::bootstrap_limits::requeued_pulls_limit_test) || total_blocks > nano::bootstrap_limits::frontier_confirmation_blocks_limit))
	{
		auto confirmed (confirm_frontiers (lock_a));
		assert (lock_a.owns_lock ());
		if (!confirmed)
		{
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::frontier_confirmation_failed, nano::stat::dir::in);
			auto score (node->bootstrap_initiator.excluded_peers.add (endpoint_frontier_request, node->network.size ()));
			if (score >= nano::bootstrap_excluded_peers::score_limit)
			{
				node->logger.always_log (boost::str (boost::format ("Adding peer %1% to excluded peers list with score %2% after %3% seconds bootstrap attempt") % endpoint_frontier_request % score % std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - attempt_start).count ()));
			}
			lock_a.unlock ();
			stop ();
			lock_a.lock ();
			// Start new bootstrap connection
			auto node_l (node->shared ());
			node->background ([node_l]() {
				node_l->bootstrap_initiator.bootstrap (true);
			});
		}
		else
		{
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::frontier_confirmation_successful, nano::stat::dir::in);
		}
		frontiers_confirmed = confirmed;
	}
}

bool nano::bootstrap_attempt_legacy::confirm_frontiers (nano::unique_lock<std::mutex> & lock_a)
{
	bool confirmed (false);
	assert (!frontiers_confirmed);
	// clang-format off
	condition.wait (lock_a, [& stopped = stopped] { return !stopped; });
	// clang-format on
	std::vector<nano::block_hash> frontiers;
	for (auto i (pulls.begin ()), end (pulls.end ()); i != end && frontiers.size () != nano::bootstrap_limits::bootstrap_max_confirm_frontiers; ++i)
	{
		if (!i->head.is_zero () && std::find (frontiers.begin (), frontiers.end (), i->head) == frontiers.end ())
		{
			frontiers.push_back (i->head);
		}
	}
	for (auto i (recent_pulls_head.begin ()), end (recent_pulls_head.end ()); i != end && frontiers.size () != nano::bootstrap_limits::bootstrap_max_confirm_frontiers; ++i)
	{
		if (!i->is_zero () && std::find (frontiers.begin (), frontiers.end (), *i) == frontiers.end ())
		{
			frontiers.push_back (*i);
		}
	}
	lock_a.unlock ();
	auto frontiers_count (frontiers.size ());
	if (frontiers_count > 0)
	{
		const size_t reps_limit = 20;
		auto representatives (node->rep_crawler.representatives ());
		auto reps_weight (node->rep_crawler.total_weight ());
		auto representatives_copy (representatives);
		nano::uint128_t total_weight (0);
		// Select random peers from bottom 50% of principal representatives
		if (representatives.size () > 1)
		{
			std::reverse (representatives.begin (), representatives.end ());
			representatives.resize (representatives.size () / 2);
			for (auto i = static_cast<CryptoPP::word32> (representatives.size () - 1); i > 0; --i)
			{
				auto k = nano::random_pool::generate_word32 (0, i);
				std::swap (representatives[i], representatives[k]);
			}
			if (representatives.size () > reps_limit)
			{
				representatives.resize (reps_limit);
			}
		}
		for (auto const & rep : representatives)
		{
			total_weight += rep.weight.number ();
		}
		// Select peers with total 25% of reps stake from top 50% of principal representatives
		representatives_copy.resize (representatives_copy.size () / 2);
		while (total_weight < reps_weight / 4) // 25%
		{
			auto k = nano::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (representatives_copy.size () - 1));
			auto rep (representatives_copy[k]);
			if (std::find (representatives.begin (), representatives.end (), rep) == representatives.end ())
			{
				representatives.push_back (rep);
				total_weight += rep.weight.number ();
			}
		}
		// Start requests
		for (auto i (0), max_requests (20); i <= max_requests && !confirmed && !stopped; ++i)
		{
			std::unordered_map<std::shared_ptr<nano::transport::channel>, std::deque<std::pair<nano::block_hash, nano::root>>> batched_confirm_req_bundle;
			std::deque<std::pair<nano::block_hash, nano::root>> request;
			// Find confirmed frontiers (tally > 12.5% of reps stake, 60% of requestsed reps responded
			for (auto ii (frontiers.begin ()); ii != frontiers.end ();)
			{
				if (node->ledger.block_exists (*ii))
				{
					ii = frontiers.erase (ii);
				}
				else
				{
					nano::lock_guard<std::mutex> active_lock (node->active.mutex);
					auto existing (node->active.find_inactive_votes_cache (*ii));
					nano::uint128_t tally;
					for (auto & voter : existing.voters)
					{
						tally += node->ledger.weight (voter);
					}
					if (existing.confirmed || (tally > reps_weight / 8 && existing.voters.size () >= representatives.size () * 0.6)) // 12.5% of weight, 60% of reps
					{
						ii = frontiers.erase (ii);
					}
					else
					{
						for (auto const & rep : representatives)
						{
							if (std::find (existing.voters.begin (), existing.voters.end (), rep.account) == existing.voters.end ())
							{
								release_assert (!ii->is_zero ());
								auto rep_request (batched_confirm_req_bundle.find (rep.channel));
								if (rep_request == batched_confirm_req_bundle.end ())
								{
									std::deque<std::pair<nano::block_hash, nano::root>> insert_root_hash = { std::make_pair (*ii, *ii) };
									batched_confirm_req_bundle.emplace (rep.channel, insert_root_hash);
								}
								else
								{
									rep_request->second.emplace_back (*ii, *ii);
								}
							}
						}
						++ii;
					}
				}
			}
			auto confirmed_count (frontiers_count - frontiers.size ());
			if (confirmed_count >= frontiers_count * nano::bootstrap_limits::required_frontier_confirmation_ratio) // 80% of frontiers confirmed
			{
				confirmed = true;
			}
			else if (i < max_requests)
			{
				node->network.broadcast_confirm_req_batched_many (batched_confirm_req_bundle);
				std::this_thread::sleep_for (std::chrono::milliseconds (!node->network_params.network.is_test_network () ? 500 : 5));
			}
		}
		if (!confirmed)
		{
			node->logger.always_log (boost::str (boost::format ("Failed to confirm frontiers for bootstrap attempt. %1% of %2% frontiers were not confirmed") % frontiers.size () % frontiers_count));
		}
	}
	lock_a.lock ();
	return confirmed;
}
bool nano::bootstrap_attempt_legacy::request_frontier (nano::unique_lock<std::mutex> & lock_a, bool first_attempt)
{
	auto result (true);
	auto connection_l (connections->connection (lock_a, first_attempt));
	connection_frontier_request = connection_l;
	if (connection_l)
	{
		endpoint_frontier_request = connection_l->channel->get_tcp_endpoint ();
		std::future<bool> future;
		{
			auto this_l (shared_from_this ());
			auto client (std::make_shared<nano::frontier_req_client> (connection_l, this_l));
			client->run ();
			frontiers = client;
			future = client->promise.get_future ();
		}
		lock_a.unlock ();
		result = consume_future (future); // This is out of scope of `client' so when the last reference via boost::asio::io_context is lost and the client is destroyed, the future throws an exception.
		lock_a.lock ();
		if (result)
		{
			frontier_pulls.clear ();
		}
		else
		{
			account_count = frontier_pulls.size ();
			// Shuffle pulls.
			release_assert (std::numeric_limits<CryptoPP::word32>::max () > frontier_pulls.size ());
			if (!frontier_pulls.empty ())
			{
				for (auto i = static_cast<CryptoPP::word32> (frontier_pulls.size () - 1); i > 0; --i)
				{
					auto k = nano::random_pool::generate_word32 (0, i);
					std::swap (frontier_pulls[i], frontier_pulls[k]);
				}
			}
			// Add to regular pulls
			while (!frontier_pulls.empty ())
			{
				auto pull (frontier_pulls.front ());
				pulls.push_back (pull);
				frontier_pulls.pop_front ();
			}
		}
		if (node->config.logging.network_logging ())
		{
			if (!result)
			{
				node->logger.try_log (boost::str (boost::format ("Completed frontier request, %1% out of sync accounts according to %2%") % account_count % connection_l->channel->to_string ()));
			}
			else
			{
				node->stats.inc (nano::stat::type::error, nano::stat::detail::frontier_req, nano::stat::dir::out);
			}
		}
	}
	return result;
}

void nano::bootstrap_attempt_legacy::run_start (nano::unique_lock<std::mutex> & lock_a)
{
	frontiers_received = false;
	frontiers_confirmed = false;
	total_blocks = 0;
	requeued_pulls = 0;
	pulls.clear ();
	recent_pulls_head.clear ();
	auto frontier_failure (true);
	uint64_t frontier_attempts (0);
	while (!stopped && !connections->stopped && frontier_failure)
	{
		++frontier_attempts;
		frontier_failure = request_frontier (lock_a, frontier_attempts == 1);
	}
	frontiers_received = true;
}

void nano::bootstrap_attempt_legacy::run ()
{
	assert (!node->flags.disable_legacy_bootstrap);
	connections->start_populate_connections ();
	nano::unique_lock<std::mutex> lock (mutex);
	run_start (lock);
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
			attempt_restart_check (lock);
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
		if (!node->flags.disable_bootstrap_bulk_push_client)
		{
			request_push (lock);
		}
		if (!stopped)
		{
			node->unchecked_cleanup ();
		}
	}
	stopped = true;
	connections->stopped = true;
	condition.notify_all ();
	connections->idle.clear ();
}

bool nano::bootstrap_attempt_legacy::process_block (std::shared_ptr<nano::block> block_a, nano::account const & known_account_a, uint64_t pull_blocks, nano::bulk_pull::count_t max_blocks, bool block_expected, unsigned retry_limit)
{
	nano::unchecked_info info (block_a, known_account_a, 0, nano::signature_verification::unknown);
	node->block_processor.add (info);
	return false;
}

void nano::bootstrap_attempt_legacy::requeue_pending (nano::account const &)
{
	assert (false);
}

size_t nano::bootstrap_attempt_legacy::wallet_size ()
{
	assert (false);
	return 0;
}
