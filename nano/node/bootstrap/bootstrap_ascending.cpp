#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/common.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;

/*
 * connection_pool
 */

nano::bootstrap::bootstrap_ascending::connection_pool::connection_pool (bootstrap_ascending & bootstrap_a) :
	bootstrap{ bootstrap_a }
{
}

void nano::bootstrap::bootstrap_ascending::connection_pool::put (socket_channel_t const & connection)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	connections.push_back (connection);
}

std::future<std::optional<nano::bootstrap::bootstrap_ascending::socket_channel_t>> nano::bootstrap::bootstrap_ascending::connection_pool::request ()
{
	std::promise<std::optional<nano::bootstrap::bootstrap_ascending::socket_channel_t>> promise;
	auto future = promise.get_future ();

	nano::unique_lock<nano::mutex> lock{ mutex };
	if (!connections.empty ())
	{
		bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::reuse);

		// connections is not empty, pop a connection, assign it to the async tag and call the callback
		auto connection = connections.front ();
		connections.pop_front ();

		lock.unlock ();

		//		bootstrap.debug_log (boost::str (boost::format ("popped connection %1%, connections.size=%2%")
		//		% connection.first->remote_endpoint () % connections.size ()));

		promise.set_value (connection);
	}
	else
	{
		lock.unlock ();

		// connections is empty, try to find peers to bootstrap with
		auto endpoint = bootstrap.node.network.next_bootstrap_peer (bootstrap.node.network_params.network.bootstrap_protocol_version_min);
		if (endpoint == nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
		{
			bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect_missing);
			bootstrap.debug_log ("Could not find a possible peer to connect with.");

			promise.set_value (std::nullopt);
		}
		else
		{
			auto socket = std::make_shared<nano::client_socket> (bootstrap.node);
			auto channel = std::make_shared<nano::transport::channel_tcp> (bootstrap.node, socket);

			bootstrap.debug_log (boost::str (boost::format ("connecting to possible peer %1% ") % endpoint));
			bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect);

			// `this` pointer is valid for as long as node is running
			socket->async_connect (endpoint,
			[this, endpoint, socket, channel, promise_s = std::make_shared<decltype (promise)> (std::move (promise))] (boost::system::error_code const & ec) {
				if (ec)
				{
					bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect_failed);
					std::cerr << boost::str (boost::format ("connect failed to: %1%\n") % endpoint);
					promise_s->set_value (std::nullopt);
				}
				else
				{
					std::cerr << boost::str (boost::format ("connected to: %1%\n") % socket->remote_endpoint ());
					bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect_success);
					promise_s->set_value (std::make_pair (socket, channel));
				}
			});
		}
	}

	return future;
}

/*
 * account_sets
 */

nano::bootstrap::bootstrap_ascending::account_sets::account_sets (nano::stat & stats_a) :
	stats{ stats_a }
{
}

void nano::bootstrap::bootstrap_ascending::account_sets::dump () const
{
	std::cerr << boost::str (boost::format ("Forwarding: %1%   blocking: %2%\n") % forwarding.size () % blocking.size ());
	std::deque<size_t> weight_counts;
	for (auto & [account, count] : backoff)
	{
		auto log = std::log2 (std::max<decltype (count)> (count, 1));
		// std::cerr << "log: " << log << ' ';
		auto index = static_cast<size_t> (log);
		if (weight_counts.size () <= index)
		{
			weight_counts.resize (index + 1);
		}
		++weight_counts[index];
	}
	std::string output;
	output += "Backoff hist (size: " + std::to_string (backoff.size ()) + "): ";
	for (size_t i = 0, n = weight_counts.size (); i < n; ++i)
	{
		output += std::to_string (weight_counts[i]) + ' ';
	}
	output += '\n';
	std::cerr << output;
}

void nano::bootstrap::bootstrap_ascending::account_sets::prioritize (nano::account const & account, float priority)
{
	if (blocking.count (account) == 0)
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::prioritize);

		forwarding.insert (account);
		auto iter = backoff.find (account);
		if (iter == backoff.end ())
		{
			backoff.emplace (account, priority);
		}
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::prioritize_failed);
	}
}

void nano::bootstrap::bootstrap_ascending::account_sets::block (nano::account const & account, nano::block_hash const & dependency)
{
	stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::block);

	backoff.erase (account);
	forwarding.erase (account);
	blocking[account] = dependency;
}

void nano::bootstrap::bootstrap_ascending::account_sets::unblock (nano::account const & account, nano::block_hash const & hash)
{
	// Unblock only if the dependency is fulfilled
	if (blocking.count (account) > 0 && blocking[account] == hash)
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::unblock);

		blocking.erase (account);
		backoff[account] = 0.0f;
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::unblock_failed);
	}
}

void nano::bootstrap::bootstrap_ascending::account_sets::force_unblock (const nano::account & account)
{
	blocking.erase (account);
	backoff[account] = 0.0f;
}

std::vector<double> nano::bootstrap::bootstrap_ascending::account_sets::probability_transform (std::vector<decltype (backoff)::mapped_type> const & attempts) const
{
	std::vector<double> result;
	result.reserve (attempts.size ());
	for (auto i = attempts.begin (), n = attempts.end (); i != n; ++i)
	{
		result.push_back (1.0 / std::pow (2.0, *i));
	}
	return result;
}

nano::account nano::bootstrap::bootstrap_ascending::account_sets::random ()
{
	debug_assert (!backoff.empty ());
	std::vector<decltype (backoff)::mapped_type> attempts;
	std::vector<nano::account> candidates;
	while (candidates.size () < account_sets::backoff_exclusion)
	{
		debug_assert (candidates.size () == attempts.size ());
		nano::account search;
		nano::random_pool::generate_block (search.bytes.data (), search.bytes.size ());
		auto iter = backoff.lower_bound (search);
		if (iter == backoff.end ())
		{
			iter = backoff.begin ();
		}
		auto const [account, count] = *iter;
		attempts.push_back (count);
		candidates.push_back (account);
	}
	auto weights = probability_transform (attempts);
	std::discrete_distribution dist{ weights.begin (), weights.end () };
	auto selection = dist (rng);
	debug_assert (!weights.empty () && selection < weights.size ());
	auto result = candidates[selection];
	return result;
}

nano::account nano::bootstrap::bootstrap_ascending::account_sets::next ()
{
	nano::account result;
	if (!forwarding.empty ())
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::next_forwarding);

		auto iter = forwarding.begin ();
		result = *iter;
		forwarding.erase (iter);
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::next_random);

		result = random ();
	}
	backoff[result] += 1.0f;
	return result;
}

bool nano::bootstrap::bootstrap_ascending::account_sets::blocked (nano::account const & account) const
{
	return blocking.count (account) > 0;
}

nano::bootstrap::bootstrap_ascending::account_sets::backoff_info_t nano::bootstrap::bootstrap_ascending::account_sets::backoff_info () const
{
	return { forwarding, blocking, backoff };
}

/*
 * async_tag
 */

// nano::bootstrap::bootstrap_ascending::async_tag::async_tag (nano::bootstrap::bootstrap_ascending & bootstrap_a, nano::bootstrap::bootstrap_ascending::thread & bootstrap_thread_a) :
//	bootstrap{ bootstrap_a } bootstrap_thread{ bootstrap_thread_a }
//{
//	// FIXME: the lifetime of a request should not be allowed to be infinite, it should be bounded
//	nano::lock_guard<nano::mutex> lock{ bootstrap->bootstrap.mutex };
//	++bootstrap->requests;
//	bootstrap->bootstrap.debug_log (boost::str (boost::format ("Request started requests=%1%")
//	% bootstrap->requests));
//	bootstrap->bootstrap.condition.notify_all ();
// }
//
// nano::bootstrap::bootstrap_ascending::async_tag::~async_tag ()
//{
//	nano::lock_guard<nano::mutex> lock{ bootstrap->bootstrap.mutex };
//	--bootstrap->requests;
//	if (blocks != 0)
//	{
//		++bootstrap->bootstrap.responses;
//	}
//	if (success_m)
//	{
//		debug_assert (connection_m);
//		bootstrap->bootstrap.pool.add (*connection_m);
//		bootstrap->bootstrap.debug_log (boost::str (boost::format ("Request completed successfully: peer=%1% blocks=%2%")
//		% connection_m->first->remote_endpoint () % blocks));
//	}
//	else
//	{
//		if (connection_m)
//		{
//			bootstrap->bootstrap.debug_log (boost::str (boost::format ("Request completed abnormally: peer=%1% blocks=%2%")
//			% connection_m->first->remote_endpoint () % blocks));
//		}
//		else
//		{
//			bootstrap->bootstrap.debug_log ("Request abandoned without trying connecting to a peer");
//		}
//	}
//	bootstrap->bootstrap.condition.notify_all ();
// }
//
// void nano::bootstrap::bootstrap_ascending::async_tag::success ()
//{
//	debug_assert (connection_m);
//	success_m = true;
// }
//
// void nano::bootstrap::bootstrap_ascending::async_tag::connection_set (socket_channel const & connection)
//{
//	connection_m = connection;
//	bootstrap->bootstrap.debug_log (boost::str (boost::format ("async tag connection_set to %1%") % connection.first->remote_endpoint ()));
// }
//
// auto nano::bootstrap::bootstrap_ascending::async_tag::connection () -> socket_channel &
//{
//	debug_assert (connection_m);
//	return *connection_m;
// }

/*
 * pull_tag
 */

nano::bootstrap::bootstrap_ascending::pull_tag::pull_tag (bootstrap_ascending & bootstrap_a, socket_channel_t socket_channel_a, const nano::hash_or_account & start_a) :
	bootstrap{ bootstrap_a },
	socket{ socket_channel_a.first },
	channel{ socket_channel_a.second },
	start{ start_a },
	deserializer{ std::make_shared<nano::bootstrap::block_deserializer> () }
{
}

std::future<bool> nano::bootstrap::bootstrap_ascending::pull_tag::send ()
{
	nano::bulk_pull message{ bootstrap.node.network_params.network };
	message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
	message.start = start;
	message.end = 0;
	message.count = request_message_count;

	bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::request);

	auto promise = nano::shared_promise<bool> ();
	channel->send (message, [promise = promise] (boost::system::error_code const & ec, std::size_t size) {
		if (ec)
		{
			// TODO: Log error
			std::cerr << "pull_tag::send error: " << ec << std::endl;

			promise->set_value (false);
		}
		else
		{
			promise->set_value (true);
		}
	});
	return promise->get_future ();
}

std::future<bool> nano::bootstrap::bootstrap_ascending::pull_tag::read ()
{
	auto promise = nano::shared_promise<bool> ();
	read_block ([promise = promise] (auto result) {
		if (result == process_result::end)
		{
			// Successfully read to end
			promise->set_value (true);
		}
		else
		{
			std::cerr << "pull_tag::read error: " << (int)result << std::endl;

			promise->set_value (false);
		}
	});
	return promise->get_future ();
}

void nano::bootstrap::bootstrap_ascending::pull_tag::read_block (std::function<void (process_result)> callback)
{
	bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block);

	deserializer->read (*socket, [this_w = std::weak_ptr (shared_from_this ()), callback = std::move (callback)] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (auto this_s = this_w.lock ())
		{
			auto result = this_s->block_received (ec, std::move (block));
			if (result == process_result::success) // Keep receiving until finished or error
			{
				this_s->read_block (std::move (callback));
			}
			else
			{
				callback (result);
			}
		}
	});
}

nano::bootstrap::bootstrap_ascending::pull_tag::process_result nano::bootstrap::bootstrap_ascending::pull_tag::block_received (boost::system::error_code ec, std::shared_ptr<nano::block> block)
{
	if (ec)
	{
		bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block_error);
		return process_result::error;
	}
	else
	{
		if (block == nullptr)
		{
			bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block_end);
			return process_result::end;
		}
		// FIXME: temporary measure to get the ascending bootstrapper working on the test network
		if (bootstrap.node.network_params.network.is_test_network () && block->hash () == nano::block_hash ("B1D60C0B886B57401EF5A1DAA04340E53726AA6F4D706C085706F31BBD100CEE"))
		{
			// skip test net genesis because it has a bad pow
			//			bootstrap.debug_log ("skipping test genesis block");

			return process_result::success;
		}
		else if (bootstrap.node.network_params.work.validate_entry (*block))
		{
			bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::insufficient_work);

			//			bootstrap.debug_log (boost::str (boost::format ("bad block from peer %1%: hash=%2% %3%")
			//			% socket->remote_endpoint () % block->hash ().to_string () % block->to_json ()));

			return process_result::malice;
		}
		else
		{
			bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block_done);

			//			bootstrap.debug_log (boost::str (boost::format ("Read block from peer %1%: hash=%2% %3%")
			//			% socket->remote_endpoint () % block->hash ().to_string () % block->to_json ()));

			process_block (block);

			++block_counter;

			return process_result::success;
		}
	}
}

/*
 * thread
 */

nano::bootstrap::bootstrap_ascending::thread::thread (bootstrap_ascending & bootstrap_a) :
	bootstrap{ bootstrap_a }
{
}

nano::account nano::bootstrap::bootstrap_ascending::thread::pick_account ()
{
	nano::lock_guard<nano::mutex> lock{ bootstrap.mutex };
	return bootstrap.accounts.next ();
}

bool nano::bootstrap::bootstrap_ascending::thread::wait_available_request ()
{
	nano::unique_lock<nano::mutex> lock{ bootstrap.mutex };
	// TODO: Make requests counter global across whole ascending bootstrap
	bootstrap.condition.wait (lock, [this] () { return bootstrap.stopped || requests < requests_max; });
	bootstrap.debug_log (boost::str (boost::format ("wait_available_request stopped=%1% request=%2%") % bootstrap.stopped % requests));
	return bootstrap.stopped;
}

std::optional<nano::bootstrap::bootstrap_ascending::socket_channel_t> nano::bootstrap::bootstrap_ascending::thread::pick_connection ()
{
	auto socket_channel_fut = bootstrap.pool.request ();
	// TODO: Handle timeout -> close socket
	auto result = socket_channel_fut.wait_for (10s);
	if (result == std::future_status::timeout)
	{
		return std::nullopt;
	}
	debug_assert (result == std::future_status::ready);
	auto maybe_socket_channel = socket_channel_fut.get ();
	return maybe_socket_channel;
}

bool nano::bootstrap::bootstrap_ascending::thread::request_one ()
{
	auto maybe_socket_channel = pick_connection ();
	if (!maybe_socket_channel)
	{
		return false;
	}
	auto socket_channel = *maybe_socket_channel;

	auto account = pick_account ();
	nano::account_info info;
	nano::hash_or_account start = account;

	// check if the account picked has blocks, if it does, start the pull from the highest block
	if (!bootstrap.store.account.get (bootstrap.store.tx_begin_read (), account, info))
	{
		start = info.head;
	}

	auto pull_tag = std::make_shared<bootstrap_ascending::pull_tag> (bootstrap, socket_channel, start);
	pull_tag->process_block = process_block;

	auto send_fut = pull_tag->send ();
	auto send_result = send_fut.get ();
	release_assert (send_result);

	auto read_fut = pull_tag->read ();
	auto read_result = read_fut.get ();
	release_assert (read_result);

	bootstrap.pool.put (socket_channel);

	return true; // Success
}

static int pass_number = 0;

void nano::bootstrap::bootstrap_ascending::thread::run ()
{
	std::cerr << "!! Starting with:" << std::to_string (pass_number++) << std::endl;
	while (!bootstrap.stopped)
	{
		auto success = request_one ();
		if (!success)
		{
			std::this_thread::sleep_for (10s);
		}
	}
	std::cerr << "!! stopping" << std::endl;
}

/*
 * bootstrap_ascending
 */

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (nano::node & node_a, nano::store & store_a, nano::block_processor & block_processor_a, nano::ledger & ledger_a, nano::stat & stats_a) :
	node{ node_a },
	store{ store_a },
	block_processor{ block_processor_a },
	ledger{ ledger_a },
	stats{ stats_a },
	accounts{ stats_a },
	pool{ *this }
{
	block_processor.processed.add ([this] (nano::transaction const & tx, nano::process_return const & result, nano::block const & block) {
		inspect (tx, result, block);
	});
}

nano::bootstrap::bootstrap_ascending::~bootstrap_ascending ()
{
	stop ();
}

void nano::bootstrap::bootstrap_ascending::seed ()
{
	uint64_t account_count = 0, receivable_count = 0;

	auto tx = store.tx_begin_read ();
	for (auto i = store.account.begin (tx), n = store.account.end (); i != n; ++i)
	{
		accounts.prioritize (i->first, 0.0f);
		account_count++;
	}
	for (auto i = store.pending.begin (tx), n = store.pending.end (); i != n; ++i)
	{
		accounts.prioritize (i->first.key (), 0.0f);
		receivable_count++;
	}

	//	debug_log (boost::str (boost::format ("bootstrap_ascending seed: accounts=%3% receivable=%4%")
	//	% account_count % receivable_count));
}

void nano::bootstrap::bootstrap_ascending::start ()
{
	seed ();

	for (auto i = 0; i < parallelism; ++i)
	{
		threads.emplace_back ([this] () {
			nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);

			bootstrap_ascending::thread thread{ *this };
			thread.process_block = [this] (auto & block) {
				block_processor.add (block);
			};
			thread.run ();
		});
	}
}

void nano::bootstrap::bootstrap_ascending::stop ()
{
	stopped = true;
	condition.notify_all ();

	for (auto & thread : threads)
	{
		thread.join ();
	}
	threads.clear ();
}

void nano::bootstrap::bootstrap_ascending::inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block)
{
	auto const hash = block.hash ();

	switch (result.code)
	{
		case nano::process_result::progress:
		{
			const auto account = ledger.account (tx, hash);
			const auto is_send = ledger.is_send (tx, block);

			nano::lock_guard<nano::mutex> lock{ mutex };

			// If we've inserted any block in to an account, unmark it as blocked
			accounts.force_unblock (account);
			// Forward and initialize backoff value with 0.0 for the current account
			// 0.0 has the highest priority
			accounts.prioritize (account, 0.0f);

			if (is_send)
			{
				// Initialize with value of 1.0 a value of lower priority than an account itselt
				// This is the same priority as if it had already made 1 attempt.
				auto const send_factor = 1.0f;

				switch (block.type ())
				{
					// Forward and initialize backoff for the referenced account
					case nano::block_type::send:
						accounts.unblock (block.destination (), hash);
						accounts.prioritize (block.destination (), send_factor);
						break;
					case nano::block_type::state:
						accounts.unblock (block.link ().as_account (), hash);
						accounts.prioritize (block.link ().as_account (), send_factor);
						break;
					default:
						debug_assert (false);
						break;
				}
			}
			break;
		}
		case nano::process_result::gap_source:
		{
			const auto account = block.previous ().is_zero () ? block.account () : ledger.account (tx, block.previous ());
			const auto source = block.source ().is_zero () ? block.link ().as_block_hash () : block.source ();

			nano::lock_guard<nano::mutex> lock{ mutex };
			// Mark account as blocked because it is missing the source block
			accounts.block (account, source);
			break;
		}
		case nano::process_result::gap_previous:
			break;
		default:
			break;
	}
}

void nano::bootstrap::bootstrap_ascending::dump_stats ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	std::cerr << boost::str (boost::format ("Requests total: %1% forwarded: %2% responses: %3%\n") % requests_total.load () % forwarded % responses.load ());
	accounts.dump ();
	responses = 0;
}

void nano::bootstrap::bootstrap_ascending::debug_log (const std::string & s) const
{
	std::cerr << s << std::endl;
}

nano::bootstrap::bootstrap_ascending::account_sets::backoff_info_t nano::bootstrap::bootstrap_ascending::backoff_info () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.backoff_info ();
}