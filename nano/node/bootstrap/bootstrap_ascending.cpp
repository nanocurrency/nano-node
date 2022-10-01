#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/common.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::connection_pool::connection_pool (nano::bootstrap::bootstrap_ascending & bootstrap) :
	bootstrap{ bootstrap }
{
}

void nano::bootstrap::bootstrap_ascending::connection_pool::add (socket_channel const & connection)
{
	bootstrap.debug_log (boost::str (boost::format ("connection push back %1%") % connection.first->remote_endpoint ()));
	connections.push_back (connection);
}

bool nano::bootstrap::bootstrap_ascending::connection_pool::operator() (std::shared_ptr<async_tag> tag, std::function<void ()> op)
{
	if (!connections.empty ())
	{
		bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::reuse);

		// connections is not empty, pop a connection, assign it to the async tag and call the callback
		tag->connection_set (connections.front ());
		connections.pop_front ();
		op ();
		bootstrap.debug_log (boost::str (boost::format ("popped connection %1%, connections.size=%2%")
		% tag->connection ().first->remote_endpoint () % connections.size ()));
		return false;
	}

	// connections is empty, try to find peers to bootstrap with
	auto endpoint = bootstrap.node.network.next_bootstrap_peer (bootstrap.node.network_params.network.bootstrap_protocol_version_min);
	if (endpoint == nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
	{
		bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect_missing);
		bootstrap.debug_log ("Could not find a possible peer to connect with.");
		return true;
	}

	auto socket = std::make_shared<nano::client_socket> (bootstrap.node);
	auto channel = std::make_shared<nano::transport::channel_tcp> (bootstrap.node, socket);
	tag->connection_set (std::make_pair (socket, channel));

	bootstrap.debug_log (boost::str (boost::format ("connecting to possible peer %1% ") % endpoint));
	bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect);
	std::weak_ptr<nano::node> node_weak = bootstrap.node.shared ();
	socket->async_connect (endpoint,
	[node_weak, endpoint, socket, op] (boost::system::error_code const & ec) {
		auto node = node_weak.lock ();
		if (!node)
		{
			return;
		}
		if (ec)
		{
			node->ascendboot.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect_failed);
			node->ascendboot.debug_log (boost::str (boost::format ("connect failed to: %1%") % endpoint));
			return;
		}
		node->ascendboot.debug_log (boost::str (boost::format ("connected to: %1%") % socket->remote_endpoint ()));
		node->ascendboot.stats.inc (nano::stat::type::bootstrap_ascending_connections, nano::stat::detail::connect_success);

		op ();
	});
	return false;
}

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

nano::bootstrap::bootstrap_ascending::async_tag::async_tag (std::shared_ptr<nano::bootstrap::bootstrap_ascending::thread> thread_a) :
	thread_weak{ thread_a },
	node_weak{ thread_a->bootstrap.node.shared () }
{
	// FIXME: the lifetime of a request should not be allowed to be infinite, it should be bounded
	nano::lock_guard<nano::mutex> lock{ thread_a->bootstrap.mutex };
	++thread_a->requests;
	thread_a->bootstrap.debug_log (boost::str (boost::format ("Request started requests=%1%")
	% thread_a->requests));
	thread_a->bootstrap.condition.notify_all ();
}

nano::bootstrap::bootstrap_ascending::async_tag::~async_tag ()
{
	// ensure that both thread object and the node are still alive
	// the ascending bootstrap is a component of node and, if node is alive, it will be alive too
	auto thread = thread_weak.lock ();
	auto node = node_weak.lock ();
	if (!thread || !node)
	{
		return;
	}

	nano::lock_guard<nano::mutex> lock{ thread->bootstrap.mutex };
	--thread->requests;
	if (blocks != 0)
	{
		++thread->bootstrap.responses;
	}
	if (success_m)
	{
		debug_assert (connection_m);
		thread->bootstrap.pool.add (*connection_m);
		thread->bootstrap.debug_log (boost::str (boost::format ("Request completed successfully: peer=%1% blocks=%2%")
		% connection_m->first->remote_endpoint () % blocks));
	}
	else
	{
		if (connection_m)
		{
			thread->bootstrap.debug_log (boost::str (boost::format ("Request completed abnormally: peer=%1% blocks=%2%")
			% connection_m->first->remote_endpoint () % blocks));
		}
		else
		{
			thread->bootstrap.debug_log ("Request abandoned without trying connecting to a peer");
		}
	}
	thread->bootstrap.condition.notify_all ();
}

void nano::bootstrap::bootstrap_ascending::async_tag::success ()
{
	debug_assert (connection_m);
	success_m = true;
}

void nano::bootstrap::bootstrap_ascending::async_tag::connection_set (socket_channel const & connection)
{
	connection_m = connection;
}

auto nano::bootstrap::bootstrap_ascending::async_tag::connection () -> socket_channel &
{
	debug_assert (connection_m);
	return *connection_m;
}

nano::bootstrap::bootstrap_ascending::thread::thread (bootstrap_ascending & bootstrap_a) :
	bootstrap{ bootstrap_a }
{
}

void nano::bootstrap::bootstrap_ascending::thread::send (std::shared_ptr<async_tag> tag, nano::hash_or_account const & start)
{
	nano::bulk_pull message{ bootstrap.node.network_params.network };
	message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
	message.start = start;
	message.end = 0;
	message.count = request_message_count;

	++bootstrap.requests_total;
	bootstrap.debug_log (boost::str (boost::format ("Request sent for: %1% to: %2%")
	% message.start.to_string () % tag->connection ().first->remote_endpoint ()));
	bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::request);

	auto channel = tag->connection ().second;
	std::weak_ptr<nano::node> node_weak = bootstrap.node.shared ();
	std::weak_ptr<bootstrap_ascending::thread> this_weak = shared ();
	channel->send (message, [node_weak, this_weak, tag] (boost::system::error_code const & ec, std::size_t size) {
		auto node = node_weak.lock ();
		auto this_l = this_weak.lock ();
		if (!node || !this_l)
		{
			return;
		}
		if (ec)
		{
			node->ascendboot.debug_log (boost::str (boost::format ("Error during bulk_pull send: %1!") % ec.value ()));
			return;
		}

		this_l->read_block (tag);
	});
}

void nano::bootstrap::bootstrap_ascending::thread::read_block (std::shared_ptr<async_tag> tag)
{
	bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block);

	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer> ();
	auto socket = tag->connection ().first;
	std::weak_ptr<nano::node> node_weak = bootstrap.node.shared ();
	std::weak_ptr<bootstrap_ascending::thread> this_weak = shared ();
	deserializer->read (*socket, [node_weak, this_weak, tag] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		auto node = node_weak.lock ();
		auto this_l = this_weak.lock ();
		if (!node || !this_l)
		{
			return;
		}
		if (ec)
		{
			this_l->bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block_error);
			this_l->bootstrap.debug_log (boost::str (boost::format ("Error during bulk_pull read: %1%") % ec.value ()));
			return;
		}
		if (block == nullptr)
		{
			this_l->bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block_end);
			this_l->bootstrap.debug_log (boost::str (boost::format ("graceful stream end: %1% blocks=%2%")
			% tag->connection ().first->remote_endpoint () % tag->blocks));

			tag->success ();
			return;
		}
		// FIXME: temporary measure to get the ascending bootstrapper working on the test network
		if (this_l->bootstrap.node.network_params.network.is_test_network () && block->hash () == nano::block_hash ("B1D60C0B886B57401EF5A1DAA04340E53726AA6F4D706C085706F31BBD100CEE"))
		{
			// skip test net genesis because it has a bad pow
			this_l->bootstrap.debug_log ("skipping test genesis block");
		}
		else if (this_l->bootstrap.node.network_params.work.validate_entry (*block))
		{
			// TODO: should we close the socket at this point?
			this_l->bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::insufficient_work);
			this_l->bootstrap.debug_log (boost::str (boost::format ("bad block from peer %1%: hash=%2% %3%")
			% tag->connection ().first->remote_endpoint () % block->hash ().to_string () % block->to_json ()));
		}
		else
		{
			this_l->bootstrap.stats.inc (nano::stat::type::bootstrap_ascending_thread, nano::stat::detail::read_block_done);
			this_l->bootstrap.debug_log (boost::str (boost::format ("Read block from peer %1%: hash=%2% %3%")
			% tag->connection ().first->remote_endpoint () % block->hash ().to_string () % block->to_json ()));
			this_l->bootstrap.node.block_processor.add (block);
			++tag->blocks;
		}
		this_l->read_block (tag);
	});
}

nano::account nano::bootstrap::bootstrap_ascending::thread::pick_account ()
{
	nano::lock_guard<nano::mutex> lock{ bootstrap.mutex };
	return bootstrap.accounts.next ();
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void nano::bootstrap::bootstrap_ascending::inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block)
{
	auto const hash = block.hash ();

	switch (result.code)
	{
		case nano::process_result::progress:
		{
			const auto account = node.ledger.account (tx, hash);
			const auto is_send = node.ledger.is_send (tx, block);

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
			const auto account = block.previous ().is_zero () ? block.account () : node.ledger.account (tx, block.previous ());
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

bool nano::bootstrap::bootstrap_ascending::thread::wait_available_request ()
{
	nano::unique_lock<nano::mutex> lock{ bootstrap.mutex };
	bootstrap.condition.wait (lock, [this] () { return bootstrap.stopped || requests < requests_max; });
	bootstrap.debug_log (boost::str (boost::format ("wait_available_request stopped=%1% request=%2%") % bootstrap.stopped % requests));
	return bootstrap.stopped;
}

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (nano::node & node_a) :
	node{ node_a },
	pool{ *this },
	stats{ node_a.stats }, // TODO: For convenience, once ascending bootstrap is a separate node component, pass via constructor
	accounts{ node_a.stats }
{
	debug_log (boost::str (boost::format ("bootstrap_ascending constructor")));
}

nano::bootstrap::bootstrap_ascending::~bootstrap_ascending ()
{
	debug_log ("bootstrap_ascending destructor starting");
	stop ();
	if (main_thread.joinable ())
	{
		main_thread.join ();
	}
	debug_log ("bootstrap_ascending destructor finished");
}

void nano::bootstrap::bootstrap_ascending::init ()
{
	uint64_t account_count = 0, receivable_count = 0;

	auto tx = node.store.tx_begin_read ();
	for (auto i = node.store.account.begin (tx), n = node.store.account.end (); i != n; ++i)
	{
		accounts.prioritize (i->first, 0.0f);
		account_count++;
	}
	for (auto i = node.store.pending.begin (tx), n = node.store.pending.end (); i != n; ++i)
	{
		accounts.prioritize (i->first.key (), 0.0f);
		receivable_count++;
	}

	debug_log (boost::str (boost::format ("bootstrap_ascending init: accounts=%1% receivable=%2%")
	% account_count % receivable_count));
}

bool nano::bootstrap::bootstrap_ascending::thread::request_one ()
{
	// do not do too many requests in parallel, impose throttling
	wait_available_request ();

	if (bootstrap.stopped)
	{
		bootstrap.debug_log ("ascending bootstrap thread exiting due to stop flag");
		return false;
	}

	bootstrap.node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::initiate_ascending, nano::stat::dir::out);

	auto this_l = shared ();
	auto tag = std::make_shared<async_tag> (this_l);
	auto account = pick_account ();
	nano::account_info info;
	nano::hash_or_account start = account;

	// check if the account picked has blocks, if it does, start the pull from the highest block
	if (!bootstrap.node.store.account.get (bootstrap.node.store.tx_begin_read (), account, info))
	{
		start = info.head;
		bootstrap.debug_log (boost::str (boost::format ("request one: %1% (%2%) from block %3%")
		% account.to_account () % account.to_string () % start.to_string ()));
	}
	else
	{
		bootstrap.debug_log (boost::str (boost::format ("request one: %1% (%2%)")
		% account.to_account () % account.to_string ()));
	}

	// pick a connection and send the pull request and setup response processing callback
	nano::unique_lock<nano::mutex> lock{ bootstrap.mutex };
	std::weak_ptr<nano::node> node_weak = bootstrap.node.shared ();
	std::weak_ptr<bootstrap_ascending::thread> this_weak = shared ();
	auto error = bootstrap.pool (tag, [node_weak, this_weak, start, tag] () {
		auto node = node_weak.lock ();
		auto this_l = this_weak.lock ();
		if (!node || !this_l)
		{
			return;
		}
		this_l->send (tag, start);
	});
	lock.unlock ();
	return error;
}

static int pass_number = 0;

void nano::bootstrap::bootstrap_ascending::thread::run ()
{
	std::cerr << "!! Starting with:" << std::to_string (pass_number++) << std::endl;
	while (!bootstrap.stopped)
	{
		auto error = request_one ();
		if (error)
		{
			auto delay = bootstrap.node.network_params.network.is_dev_network () ? 50ms : 5s;
			std::this_thread::sleep_for (delay);
		}
	}
	std::cerr << "!! stopping" << std::endl;
}

auto nano::bootstrap::bootstrap_ascending::thread::shared () -> std::shared_ptr<thread>
{
	return shared_from_this ();
}

void nano::bootstrap::bootstrap_ascending::run ()
{
	nano::thread_role::set (nano::thread_role::name::ascending_bootstrap_main);

	debug_log (boost::str (boost::format ("Starting ascending bootstrap main thread, parallelism=%1%") % parallelism));

	std::weak_ptr<nano::node> node_weak = node.shared ();
	node.block_processor.processed.add ([node_weak] (nano::transaction const & tx, nano::process_return const & result, nano::block const & block) {
		auto node = node_weak.lock ();
		if (!node)
		{
			// std::cerr << boost::str (boost::format ("Missed block: %1%\n") % block.hash ().to_string ());
			return;
		}
		node->ascendboot.inspect (tx, result, block);
	});

	std::deque<std::thread> threads;
	for (auto i = 0; i < parallelism; ++i)
	{
		threads.emplace_back ([this] () {
			nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
			auto thread = std::make_shared<bootstrap_ascending::thread> (*this);
			thread->run ();
		});
	}

	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		condition.wait_for (lock, std::chrono::seconds{ 10 }, [this] () { return stopped; });
	}

	dump_stats ();

	debug_log (boost::str (boost::format ("Waiting for the ascending bootstrap sub-threads")));
	for (auto & thread : threads)
	{
		thread.join ();
	}

	debug_log (boost::str (boost::format ("Exiting ascending bootstrap main thread")));
}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{
}

void nano::bootstrap::bootstrap_ascending::debug_log (const std::string & s) const
{
	static bool enable = nano::get_env_or_default ("NANO_ASCENDBOOT_DEBUG", "no") == std::string ("yes");
	if (enable)
	{
		std::cerr << s << std::endl;
	}
}

void nano::bootstrap::bootstrap_ascending::start ()
{
	std::cerr << "starting\n";
	debug_assert (!main_thread.joinable ());
	main_thread = std::thread{
		[this] () { run (); },
	};
	std::cerr << "started\n";
}

void nano::bootstrap::bootstrap_ascending::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	condition.notify_all ();
}

nano::bootstrap::bootstrap_ascending::account_sets::backoff_info_t nano::bootstrap::bootstrap_ascending::backoff_info () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.backoff_info ();
}
