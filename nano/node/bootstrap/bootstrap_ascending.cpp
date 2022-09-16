#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/common.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::connection_pool::connection_pool (nano::node & node) :
	node{ node }
{
}

void nano::bootstrap::bootstrap_ascending::connection_pool::operator() (socket_channel const & connection)
{
	connections.push_back (connection);
}

bool nano::bootstrap::bootstrap_ascending::connection_pool::operator() (std::shared_ptr<async_tag> tag, std::function<void ()> op)
{
	if (!connections.empty ())
	{
		tag->connection_set (connections.front ());
		connections.pop_front ();
		op ();
		return false;
	}
	auto endpoint = node.network.bootstrap_peer (true);
	if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
	{
		auto socket = std::make_shared<nano::client_socket> (node);
		auto channel = std::make_shared<nano::transport::channel_tcp> (node, socket);
		tag->connection_set (std::make_pair (socket, channel));
		std::cerr << boost::str (boost::format ("Connecting: %1%\n") % endpoint);
		socket->async_connect (endpoint,
		[endpoint, op] (boost::system::error_code const & ec) {
			if (ec)
			{
				std::cerr << boost::str (boost::format ("connect failed to: %1%\n") % endpoint);
				return;
			}
			std::cerr << boost::str (boost::format ("connected to: %1%\n") % endpoint);
			op ();
		});
		return false;
	}
	else
	{
		return true;
	}
}

nano::bootstrap::bootstrap_ascending::account_sets::account_sets ()
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
		forwarding.insert (account);
		auto iter = backoff.find (account);
		if (iter == backoff.end ())
		{
			backoff.emplace (account, priority);
		}
	}
}

void nano::bootstrap::bootstrap_ascending::account_sets::block (nano::account const & account)
{
	backoff.erase (account);
	forwarding.erase (account);
	blocking.insert (account);
}

void nano::bootstrap::bootstrap_ascending::account_sets::unblock (nano::account const & account)
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
		auto iter = forwarding.begin ();
		result = *iter;
		forwarding.erase (iter);
	}
	else
	{
		result = random ();
	}
	backoff[result] += 1.0f;
	return result;
}

bool nano::bootstrap::bootstrap_ascending::account_sets::blocked (nano::account const & account) const
{
	return blocking.count (account) > 0;
}

nano::bootstrap::bootstrap_ascending::async_tag::async_tag (std::shared_ptr<nano::bootstrap::bootstrap_ascending::thread> bootstrap) :
	bootstrap{ bootstrap }
{
	nano::lock_guard<nano::mutex> lock{ bootstrap->bootstrap.mutex };
	++bootstrap->requests;
	bootstrap->bootstrap.condition.notify_all ();
	// std::cerr << boost::str (boost::format ("Request started\n"));
}

nano::bootstrap::bootstrap_ascending::async_tag::~async_tag ()
{
	nano::lock_guard<nano::mutex> lock{ bootstrap->bootstrap.mutex };
	--bootstrap->requests;
	if (blocks != 0)
	{
		++bootstrap->bootstrap.responses;
	}
	if (success_m)
	{
		debug_assert (connection_m);
		bootstrap->bootstrap.pool (*connection_m);
	}
	bootstrap->bootstrap.condition.notify_all ();
	// std::cerr << boost::str (boost::format ("Request completed\n"));
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

nano::bootstrap::bootstrap_ascending::thread::thread (std::shared_ptr<bootstrap_ascending> bootstrap) :
	bootstrap_ptr{ bootstrap }
{
}

void nano::bootstrap::bootstrap_ascending::thread::send (std::shared_ptr<async_tag> tag, nano::hash_or_account const & start)
{
	nano::bulk_pull message{ bootstrap.node->network_params.network };
	message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
	message.start = start;
	message.end = 0;
	message.count = request_message_count;
	// std::cerr << boost::str (boost::format ("Request sent for: %1% to: %2%\n") % message.start.to_string () % tag->connection ().first->remote_endpoint ());
	auto channel = tag->connection ().second;
	++bootstrap.requests_total;
	channel->send (message, [this_l = shared (), tag] (boost::system::error_code const & ec, std::size_t size) {
		this_l->read_block (tag);
	});
}

void nano::bootstrap::bootstrap_ascending::thread::read_block (std::shared_ptr<async_tag> tag)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer> ();
	auto socket = tag->connection ().first;
	deserializer->read (*socket, [this_l = shared (), tag] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			// std::cerr << "stream end\n";
			tag->success ();
			return;
		}
		// std::cerr << boost::str (boost::format ("block: %1%\n") % block->hash ().to_string ());
		this_l->bootstrap.node->block_processor.add (block);
		this_l->read_block (tag);
		++tag->blocks;
	});
}

nano::account nano::bootstrap::bootstrap_ascending::thread::pick_account ()
{
	nano::lock_guard<nano::mutex> lock{ bootstrap.mutex };
	return bootstrap.accounts.next ();
}

void nano::bootstrap::bootstrap_ascending::inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block)
{
	switch (result.code)
	{
		case nano::process_result::progress:
		{
			auto account = node->ledger.account (tx, block.hash ());
			auto is_send = node->ledger.is_send (tx, block);
			nano::lock_guard<nano::mutex> lock{ mutex };
			accounts.unblock (account);
			accounts.prioritize (account, 0.0f);
			if (is_send)
			{
				auto const send_factor = 1.0f;
				switch (block.type ())
				{
					case nano::block_type::send:
						accounts.prioritize (block.destination (), send_factor);
						break;
					case nano::block_type::state:
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
			auto account = block.previous ().is_zero () ? block.account () : node->ledger.account (tx, block.previous ());
			nano::lock_guard<nano::mutex> lock{ mutex };
			accounts.block (account);
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
	return bootstrap.stopped;
}

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a) :
	bootstrap_attempt{ node_a, nano::bootstrap_mode::ascending, incremental_id_a, id_a },
	pool{ *node }
{
	auto tx = node_a->store.tx_begin_read ();
	for (auto i = node_a->store.account.begin (tx), n = node_a->store.account.end (); i != n; ++i)
	{
		accounts.prioritize (i->first, 0.0f);
	}
	for (auto i = node_a->store.pending.begin (tx), n = node_a->store.pending.end (); i != n; ++i)
	{
		accounts.prioritize (i->first.key (), 0.0f);
	}
}

bool nano::bootstrap::bootstrap_ascending::thread::request_one ()
{
	wait_available_request ();
	if (bootstrap.stopped)
	{
		return false;
	}

	auto this_l = shared ();
	auto tag = std::make_shared<async_tag> (this_l);
	auto account = pick_account ();
	nano::account_info info;
	nano::hash_or_account start = account;
	if (!bootstrap.node->store.account.get (bootstrap.node->store.tx_begin_read (), account, info))
	{
		start = info.head;
	}
	nano::unique_lock<nano::mutex> lock{ bootstrap.mutex };
	auto error = bootstrap.pool (tag, [this_l = shared (), start, tag] () {
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
			std::this_thread::sleep_for (10s);
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
	node->block_processor.processed.add ([this_w = std::weak_ptr<nano::bootstrap::bootstrap_ascending>{ shared () }] (nano::transaction const & tx, nano::process_return const & result, nano::block const & block) {
		auto this_l = this_w.lock ();
		if (this_l == nullptr)
		{
			// std::cerr << boost::str (boost::format ("Missed block: %1%\n") % block.hash ().to_string ());
			return;
		}
		this_l->inspect (tx, result, block);
	});

	std::deque<std::thread> threads;
	for (auto i = 0; i < parallelism; ++i)
	{
		threads.emplace_back ([this_l = shared ()] () {
			nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
			auto thread = std::make_shared<bootstrap_ascending::thread> (this_l);
			thread->run ();
		});
	}

	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		condition.wait_for (lock, std::chrono::seconds{ 10 }, [this] () { return stopped.load (); });
	}

	dump_stats ();

	for (auto & thread : threads)
	{
		thread.join ();
	}
}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{
}

std::shared_ptr<nano::bootstrap::bootstrap_ascending> nano::bootstrap::bootstrap_ascending::shared ()
{
	return std::static_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ());
}
