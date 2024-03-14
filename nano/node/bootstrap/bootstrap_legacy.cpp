#include <nano/node/bootstrap/bootstrap_bulk_push.hpp>
#include <nano/node/bootstrap/bootstrap_frontier.hpp>
#include <nano/node/bootstrap/bootstrap_legacy.hpp>
#include <nano/node/node.hpp>

#include <boost/format.hpp>

nano::bootstrap_attempt_legacy::bootstrap_attempt_legacy (std::shared_ptr<nano::node> const & node_a, uint64_t const incremental_id_a, std::string const & id_a, uint32_t const frontiers_age_a, nano::account const & start_account_a) :
	nano::bootstrap_attempt (node_a, nano::bootstrap_mode::legacy, incremental_id_a, id_a),
	frontiers_age (frontiers_age_a),
	start_account (start_account_a)
{
	node_a->bootstrap_initiator.notify_listeners (true);
}

bool nano::bootstrap_attempt_legacy::consume_future (std::future<bool> & future_a)
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

void nano::bootstrap_attempt_legacy::stop ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	lock.lock ();
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
	lock.unlock ();
	node->bootstrap_initiator.connections->clear_pulls (incremental_id);
}

void nano::bootstrap_attempt_legacy::request_push (nano::unique_lock<nano::mutex> & lock_a)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}
	bool error (false);
	lock_a.unlock ();
	auto connection_l (node->bootstrap_initiator.connections->find_connection (endpoint_frontier_request));
	lock_a.lock ();
	if (connection_l)
	{
		std::future<bool> future;
		{
			auto this_l = std::dynamic_pointer_cast<nano::bootstrap_attempt_legacy> (shared_from_this ());
			auto client = std::make_shared<nano::bulk_push_client> (connection_l, this_l);
			client->start ();
			push = client;
			future = client->promise.get_future ();
		}
		lock_a.unlock ();
		error = consume_future (future); // This is out of scope of `client' so when the last reference via boost::asio::io_context is lost and the client is destroyed, the future throws an exception.
		lock_a.lock ();
	}
}

void nano::bootstrap_attempt_legacy::add_frontier (nano::pull_info const & pull_a)
{
	// Prevent incorrect or malicious pulls with frontier 0 insertion
	if (!pull_a.head.is_zero ())
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		frontier_pulls.push_back (pull_a);
	}
}

void nano::bootstrap_attempt_legacy::add_bulk_push_target (nano::block_hash const & head, nano::block_hash const & end)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	bulk_push_targets.emplace_back (head, end);
}

bool nano::bootstrap_attempt_legacy::request_bulk_push_target (std::pair<nano::block_hash, nano::block_hash> & current_target_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto empty (bulk_push_targets.empty ());
	if (!empty)
	{
		current_target_a = bulk_push_targets.back ();
		bulk_push_targets.pop_back ();
	}
	return empty;
}

void nano::bootstrap_attempt_legacy::set_start_account (nano::account const & start_account_a)
{
	// Add last account fron frontier request
	nano::lock_guard<nano::mutex> lock{ mutex };
	start_account = start_account_a;
}

bool nano::bootstrap_attempt_legacy::request_frontier (nano::unique_lock<nano::mutex> & lock_a, bool first_attempt)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return true;
	}
	auto result (true);
	lock_a.unlock ();
	auto connection_l (node->bootstrap_initiator.connections->connection (shared_from_this (), first_attempt));
	lock_a.lock ();
	if (connection_l && !stopped)
	{
		endpoint_frontier_request = connection_l->channel->get_tcp_endpoint ();
		std::future<bool> future;
		{
			auto this_l = std::dynamic_pointer_cast<nano::bootstrap_attempt_legacy> (shared_from_this ());
			auto client = std::make_shared<nano::frontier_req_client> (connection_l, this_l);
			client->run (start_account, frontiers_age, node->config.bootstrap_frontier_request_count);
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
			account_count = nano::narrow_cast<unsigned int> (frontier_pulls.size ());
			// Shuffle pulls
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
				lock_a.unlock ();
				node->bootstrap_initiator.connections->add_pull (pull);
				lock_a.lock ();
				++pulling;
				frontier_pulls.pop_front ();
			}
		}
		if (!result)
		{
			node->logger.debug (nano::log::type::bootstrap_legacy, "Completed frontier request, {} out of sync accounts according to {}", account_count.load (), connection_l->channel->to_string ());
		}
		else
		{
			node->stats.inc (nano::stat::type::error, nano::stat::detail::frontier_req, nano::stat::dir::out);
		}
	}
	return result;
}

void nano::bootstrap_attempt_legacy::run_start (nano::unique_lock<nano::mutex> & lock_a)
{
	frontiers_received = false;
	auto frontier_failure (true);
	uint64_t frontier_attempts (0);
	while (!stopped && frontier_failure)
	{
		++frontier_attempts;
		frontier_failure = request_frontier (lock_a, frontier_attempts == 1);
	}
	frontiers_received = true;
}

void nano::bootstrap_attempt_legacy::run ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}
	debug_assert (started);
	debug_assert (!node->flags.disable_legacy_bootstrap);
	node->bootstrap_initiator.connections->populate_connections (false);
	nano::unique_lock<nano::mutex> lock{ mutex };
	run_start (lock);
	while (still_pulling ())
	{
		while (still_pulling ())
		{
			// clang-format off
			condition.wait (lock, [&stopped = stopped, &pulling = pulling] { return stopped || pulling == 0; });
		}

		// TODO: This check / wait is a heuristic and should be improved.
		auto wait_start = std::chrono::steady_clock::now ();
		while (!stopped && node->block_processor.size () != 0 && ((std::chrono::steady_clock::now () - wait_start) < std::chrono::seconds{ 10 }))
		{
			condition.wait_for (lock, std::chrono::milliseconds{ 100 }, [this, node] { return stopped || node->block_processor.size () == 0; });
		}

		if (start_account.number () != std::numeric_limits<nano::uint256_t>::max ())
		{
			node->logger.debug(nano::log::type::bootstrap_legacy, "Requesting new frontiers after: {}", start_account.to_account ());

			// Requesting new frontiers
			run_start (lock);
		}
	}
	if (!stopped)
	{
		node->logger.debug(nano::log::type::bootstrap_legacy, "Completed legacy pulls");

		if (!node->flags.disable_bootstrap_bulk_push_client)
		{
			request_push (lock);
		}
	}
	lock.unlock ();
	stop ();
	condition.notify_all ();
}

void nano::bootstrap_attempt_legacy::get_information (boost::property_tree::ptree & tree_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	tree_a.put ("frontier_pulls", std::to_string (frontier_pulls.size ()));
	tree_a.put ("frontiers_received", static_cast<bool> (frontiers_received));
	tree_a.put ("frontiers_age", std::to_string (frontiers_age));
	tree_a.put ("last_account", start_account.to_account ());
}
