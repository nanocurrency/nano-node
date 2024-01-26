#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/common.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <boost/asio.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cstdlib>

using namespace std::chrono_literals;

std::string nano::error_system_messages::message (int ev) const
{
	switch (static_cast<nano::error_system> (ev))
	{
		case nano::error_system::generic:
			return "Unknown error";
		case nano::error_system::deadline_expired:
			return "Deadline expired";
	}

	return "Invalid error code";
}

nano::node & nano::test::system::node (std::size_t index) const
{
	debug_assert (index < nodes.size ());
	return *nodes[index];
}

std::shared_ptr<nano::node> nano::test::system::add_node (nano::node_flags node_flags_a, nano::transport::transport_type type_a)
{
	return add_node (default_config (), node_flags_a, type_a);
}

/** Returns the node added. */
std::shared_ptr<nano::node> nano::test::system::add_node (nano::node_config const & node_config_a, nano::node_flags node_flags_a, nano::transport::transport_type type_a, std::optional<nano::keypair> const & rep)
{
	auto node (std::make_shared<nano::node> (io_ctx, nano::unique_path (), node_config_a, work, node_flags_a, node_sequence++));
	for (auto i : initialization_blocks)
	{
		auto result = node->ledger.process (node->store.tx_begin_write (), *i);
		debug_assert (result.code == nano::process_result::progress);
	}
	debug_assert (!node->init_error ());
	auto wallet = node->wallets.create (nano::random_wallet_id ());
	if (rep)
	{
		wallet->insert_adhoc (rep->prv);
	}
	node->start ();
	nodes.reserve (nodes.size () + 1);
	nodes.push_back (node);
	if (nodes.size () > 1)
	{
		debug_assert (nodes.size () - 1 <= node->network_params.network.max_peers_per_ip || node->flags.disable_max_peers_per_ip); // Check that we don't start more nodes than limit for single IP address
		auto begin = nodes.end () - 2;
		for (auto i (begin), j (begin + 1), n (nodes.end ()); j != n; ++i, ++j)
		{
			auto node1 (*i);
			auto node2 (*j);

			auto starting_size_1 = node1->network.size ();
			auto starting_size_2 = node2->network.size ();

			auto starting_realtime_1 = node1->tcp_listener->realtime_count.load ();
			auto starting_realtime_2 = node2->tcp_listener->realtime_count.load ();

			auto starting_keepalives_1 = node1->stats.count (stat::type::message, stat::detail::keepalive, stat::dir::in);
			auto starting_keepalives_2 = node2->stats.count (stat::type::message, stat::detail::keepalive, stat::dir::in);

			// TCP is the only transport layer available.
			debug_assert (type_a == nano::transport::transport_type::tcp);
			(*j)->network.merge_peer ((*i)->network.endpoint ());

			{
				auto ec = poll_until_true (5s, [&node1, &node2, starting_size_1, starting_size_2] () {
					auto size_1 = node1->network.size ();
					auto size_2 = node2->network.size ();
					return size_1 > starting_size_1 && size_2 > starting_size_2;
				});
				debug_assert (!ec);
			}

			if (type_a == nano::transport::transport_type::tcp && node_config_a.tcp_incoming_connections_max != 0 && !node_flags_a.disable_tcp_realtime)
			{
				{
					// Wait for initial connection finish
					auto ec = poll_until_true (5s, [&node1, &node2, starting_realtime_1, starting_realtime_2] () {
						auto realtime_1 = node1->tcp_listener->realtime_count.load ();
						auto realtime_2 = node2->tcp_listener->realtime_count.load ();
						return realtime_1 > starting_realtime_1 && realtime_2 > starting_realtime_2;
					});
					debug_assert (!ec);
				}
				{
					// Wait for keepalive message exchange
					auto ec = poll_until_true (5s, [&node1, &node2, starting_keepalives_1, starting_keepalives_2] () {
						auto keepalives_1 = node1->stats.count (stat::type::message, stat::detail::keepalive, stat::dir::in);
						auto keepalives_2 = node2->stats.count (stat::type::message, stat::detail::keepalive, stat::dir::in);
						return keepalives_1 > starting_keepalives_1 && keepalives_2 > starting_keepalives_2;
					});
					debug_assert (!ec);
				}
			}
		}

		{
			// Ensure no bootstrap initiators are in progress
			auto ec = poll_until_true (5s, [this, &begin] () {
				return std::all_of (begin, nodes.end (), [] (std::shared_ptr<nano::node> const & node_a) { return !node_a->bootstrap_initiator.in_progress (); });
			});
			debug_assert (!ec);
		}
	}
	else
	{
		auto ec = poll_until_true (5s, [&node] () {
			return !node->bootstrap_initiator.in_progress ();
		});
		debug_assert (!ec);
	}

	return node;
}

std::shared_ptr<nano::node> nano::test::system::make_disconnected_node (std::optional<nano::node_config> opt_node_config, nano::node_flags flags)
{
	nano::node_config node_config = opt_node_config.has_value () ? *opt_node_config : default_config ();
	auto node = std::make_shared<nano::node> (io_ctx, nano::unique_path (), node_config, work, flags);
	if (node->init_error ())
	{
		std::cerr << "node init error\n";
		return nullptr;
	}
	node->start ();
	return node;
}

nano::test::system::system ()
{
	auto scale_str = std::getenv ("DEADLINE_SCALE_FACTOR");
	if (scale_str)
	{
		deadline_scaling_factor = std::stod (scale_str);
	}
}

nano::test::system::system (uint16_t count_a, nano::transport::transport_type type_a, nano::node_flags flags_a) :
	system ()
{
	nodes.reserve (count_a);
	for (uint16_t i (0); i < count_a; ++i)
	{
		add_node (default_config (), flags_a, type_a);
	}
}

nano::test::system::~system ()
{
	// Only stop system in destructor to avoid confusing and random bugs when debugging assertions that hit deadline expired condition
	stop ();

#ifndef _WIN32
	// Windows cannot remove the log and data files while they are still owned by this process.
	// They will be removed later

	// Clean up tmp directories created by the tests. Since it's sometimes useful to
	// see log files after test failures, an environment variable is supported to
	// retain the files.
	if (std::getenv ("TEST_KEEP_TMPDIRS") == nullptr)
	{
		nano::remove_temporary_directories ();
	}
#endif
}

void nano::test::system::ledger_initialization_set (std::vector<nano::keypair> const & reps, nano::amount const & reserve)
{
	nano::block_hash previous = nano::dev::genesis->hash ();
	auto amount = (nano::dev::constants.genesis_amount - reserve.number ()) / reps.size ();
	auto balance = nano::dev::constants.genesis_amount;
	for (auto const & i : reps)
	{
		balance -= amount;
		nano::state_block_builder builder;
		builder.account (nano::dev::genesis_key.pub)
		.previous (previous)
		.representative (nano::dev::genesis_key.pub)
		.link (i.pub)
		.balance (balance)
		.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
		.work (*work.generate (previous));
		initialization_blocks.emplace_back (builder.build_shared ());
		previous = initialization_blocks.back ()->hash ();
		builder.make_block ();
		builder.account (i.pub)
		.previous (0)
		.representative (i.pub)
		.link (previous)
		.balance (amount)
		.sign (i.prv, i.pub)
		.work (*work.generate (i.pub));
		initialization_blocks.emplace_back (builder.build_shared ());
	}
}

std::shared_ptr<nano::wallet> nano::test::system::wallet (size_t index_a)
{
	debug_assert (nodes.size () > index_a);
	auto size (nodes[index_a]->wallets.items.size ());
	(void)size;
	debug_assert (size == 1);
	return nodes[index_a]->wallets.items.begin ()->second;
}

nano::account nano::test::system::account (store::transaction const & transaction_a, size_t index_a)
{
	auto wallet_l (wallet (index_a));
	auto keys (wallet_l->store.begin (transaction_a));
	debug_assert (keys != wallet_l->store.end ());
	auto result (keys->first);
	debug_assert (++keys == wallet_l->store.end ());
	return nano::account (result);
}

uint64_t nano::test::system::work_generate_limited (nano::block_hash const & root_a, uint64_t min_a, uint64_t max_a)
{
	debug_assert (min_a > 0);
	uint64_t result = 0;
	do
	{
		result = *work.generate (root_a, min_a);
	} while (work.network_constants.work.difficulty (nano::work_version::work_1, root_a, result) >= max_a);
	return result;
}

/** Initiate an epoch upgrade. Writes the epoch block into the ledger and leaves it to
 *  node background processes (e.g. frontiers confirmation) to cement the block.
 */
std::unique_ptr<nano::state_block> nano::test::upgrade_epoch (nano::work_pool & pool_a, nano::ledger & ledger_a, nano::epoch epoch_a)
{
	auto transaction (ledger_a.store.tx_begin_write ());
	auto dev_genesis_key = nano::dev::genesis_key;
	auto account = dev_genesis_key.pub;
	auto latest = ledger_a.latest (transaction, account);
	auto balance = ledger_a.account_balance (transaction, account);

	nano::state_block_builder builder;
	std::error_code ec;
	auto epoch = builder
				 .account (dev_genesis_key.pub)
				 .previous (latest)
				 .balance (balance)
				 .link (ledger_a.epoch_link (epoch_a))
				 .representative (dev_genesis_key.pub)
				 .sign (dev_genesis_key.prv, dev_genesis_key.pub)
				 .work (*pool_a.generate (latest, pool_a.network_constants.work.threshold (nano::work_version::work_1, nano::block_details (epoch_a, false, false, true))))
				 .build (ec);

	bool error{ true };
	if (!ec && epoch)
	{
		error = ledger_a.process (transaction, *epoch).code != nano::process_result::progress;
	}

	return !error ? std::move (epoch) : nullptr;
}

std::unique_ptr<nano::state_block> nano::test::system::upgrade_genesis_epoch (nano::node & node_a, nano::epoch const epoch_a)
{
	return upgrade_epoch (work, node_a.ledger, epoch_a);
}

void nano::test::system::deadline_set (std::chrono::duration<double, std::nano> const & delta_a)
{
	deadline = std::chrono::steady_clock::now () + delta_a * deadline_scaling_factor;
}

std::error_code nano::test::system::poll (std::chrono::nanoseconds const & wait_time)
{
#if NANO_ASIO_HANDLER_TRACKING == 0
	io_ctx.run_one_for (wait_time);
#else
	nano::timer<> timer;
	timer.start ();
	auto count = io_ctx.poll_one ();
	if (count == 0)
	{
		std::this_thread::sleep_for (wait_time);
	}
	else if (count == 1 && timer.since_start ().count () >= NANO_ASIO_HANDLER_TRACKING)
	{
		auto timestamp = std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
		std::cout << (boost::format ("[%1%] io_thread held for %2%ms") % timestamp % timer.since_start ().count ()).str () << std::endl;
	}
#endif

	std::error_code ec;
	if (std::chrono::steady_clock::now () > deadline)
	{
		ec = nano::error_system::deadline_expired;
	}
	return ec;
}

std::error_code nano::test::system::poll_until_true (std::chrono::nanoseconds deadline_a, std::function<bool ()> predicate_a)
{
	std::error_code ec;
	deadline_set (deadline_a);
	while (!ec && !predicate_a ())
	{
		ec = poll ();
	}
	return ec;
}

/**
 * This function repetitively calls io_ctx.run_one_for until delay number of milliseconds elapse.
 * It can be used to sleep for a duration in unit tests whilst allowing the background io contexts to continue processing.
 * @param delay milliseconds of delay
 */
void nano::test::system::delay_ms (std::chrono::milliseconds const & delay)
{
	auto now = std::chrono::steady_clock::now ();
	auto endtime = now + delay;
	while (now <= endtime)
	{
		io_ctx.run_one_for (endtime - now);
		now = std::chrono::steady_clock::now ();
	}
}

namespace
{
class traffic_generator : public std::enable_shared_from_this<traffic_generator>
{
public:
	traffic_generator (uint32_t count_a, uint32_t wait_a, std::shared_ptr<nano::node> const & node_a, nano::test::system & system_a) :
		count (count_a),
		wait (wait_a),
		node (node_a),
		system (system_a)
	{
	}
	void run ()
	{
		auto count_l (count - 1);
		count = count_l - 1;
		system.generate_activity (*node, accounts);
		if (count_l > 0)
		{
			auto this_l (shared_from_this ());
			node->workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::milliseconds (wait), [this_l] () { this_l->run (); });
		}
	}
	std::vector<nano::account> accounts;
	uint32_t count;
	uint32_t wait;
	std::shared_ptr<nano::node> node;
	nano::test::system & system;
};
}

void nano::test::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a)
{
	for (size_t i (0), n (nodes.size ()); i != n; ++i)
	{
		generate_usage_traffic (count_a, wait_a, i);
	}
}

void nano::test::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a, size_t index_a)
{
	debug_assert (nodes.size () > index_a);
	debug_assert (count_a > 0);
	auto generate (std::make_shared<traffic_generator> (count_a, wait_a, nodes[index_a], *this));
	generate->run ();
}

void nano::test::system::generate_rollback (nano::node & node_a, std::vector<nano::account> & accounts_a)
{
	auto transaction (node_a.store.tx_begin_write ());
	debug_assert (std::numeric_limits<CryptoPP::word32>::max () > accounts_a.size ());
	auto index (random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (accounts_a.size () - 1)));
	auto account (accounts_a[index]);
	auto info = node_a.ledger.account_info (transaction, account);
	if (info)
	{
		auto hash (info->open_block);
		if (hash != node_a.network_params.ledger.genesis->hash ())
		{
			accounts_a[index] = accounts_a[accounts_a.size () - 1];
			accounts_a.pop_back ();
			std::vector<std::shared_ptr<nano::block>> rollback_list;
			auto error = node_a.ledger.rollback (transaction, hash, rollback_list);
			(void)error;
			debug_assert (!error);
			for (auto & i : rollback_list)
			{
				node_a.active.erase (*i);
			}
		}
	}
}

void nano::test::system::generate_receive (nano::node & node_a)
{
	std::shared_ptr<nano::block> send_block;
	{
		auto transaction (node_a.store.tx_begin_read ());
		nano::account random_account;
		random_pool::generate_block (random_account.bytes.data (), sizeof (random_account.bytes));
		auto i (node_a.store.pending.begin (transaction, nano::pending_key (random_account, 0)));
		if (i != node_a.store.pending.end ())
		{
			nano::pending_key const & send_hash (i->first);
			send_block = node_a.store.block.get (transaction, send_hash.hash);
		}
	}
	if (send_block != nullptr)
	{
		auto receive_error (wallet (0)->receive_sync (send_block, nano::dev::genesis->account (), std::numeric_limits<nano::uint128_t>::max ()));
		(void)receive_error;
	}
}

void nano::test::system::generate_activity (nano::node & node_a, std::vector<nano::account> & accounts_a)
{
	auto what (random_pool::generate_byte ());
	if (what < 0x1)
	{
		generate_rollback (node_a, accounts_a);
	}
	else if (what < 0x10)
	{
		generate_change_known (node_a, accounts_a);
	}
	else if (what < 0x20)
	{
		generate_change_unknown (node_a, accounts_a);
	}
	else if (what < 0x70)
	{
		generate_receive (node_a);
	}
	else if (what < 0xc0)
	{
		generate_send_existing (node_a, accounts_a);
	}
	else
	{
		generate_send_new (node_a, accounts_a);
	}
}

nano::account nano::test::system::get_random_account (std::vector<nano::account> & accounts_a)
{
	debug_assert (std::numeric_limits<CryptoPP::word32>::max () > accounts_a.size ());
	auto index (random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (accounts_a.size () - 1)));
	auto result (accounts_a[index]);
	return result;
}

nano::uint128_t nano::test::system::get_random_amount (store::transaction const & transaction_a, nano::node & node_a, nano::account const & account_a)
{
	nano::uint128_t balance (node_a.ledger.account_balance (transaction_a, account_a));
	nano::uint128_union random_amount;
	nano::random_pool::generate_block (random_amount.bytes.data (), sizeof (random_amount.bytes));
	return (((nano::uint256_t{ random_amount.number () } * balance) / nano::uint256_t{ std::numeric_limits<nano::uint128_t>::max () }).convert_to<nano::uint128_t> ());
}

void nano::test::system::generate_send_existing (nano::node & node_a, std::vector<nano::account> & accounts_a)
{
	nano::uint128_t amount;
	nano::account destination;
	nano::account source;
	{
		nano::account account;
		random_pool::generate_block (account.bytes.data (), sizeof (account.bytes));
		auto transaction (node_a.store.tx_begin_read ());
		store::iterator<nano::account, nano::account_info> entry (node_a.store.account.begin (transaction, account));
		if (entry == node_a.store.account.end ())
		{
			entry = node_a.store.account.begin (transaction);
		}
		debug_assert (entry != node_a.store.account.end ());
		destination = nano::account (entry->first);
		source = get_random_account (accounts_a);
		amount = get_random_amount (transaction, node_a, source);
	}
	if (!amount.is_zero ())
	{
		auto hash (wallet (0)->send_sync (source, destination, amount));
		(void)hash;
		debug_assert (!hash.is_zero ());
	}
}

void nano::test::system::generate_change_known (nano::node & node_a, std::vector<nano::account> & accounts_a)
{
	nano::account source (get_random_account (accounts_a));
	if (!node_a.latest (source).is_zero ())
	{
		nano::account destination (get_random_account (accounts_a));
		auto change_error (wallet (0)->change_sync (source, destination));
		(void)change_error;
		debug_assert (!change_error);
	}
}

void nano::test::system::generate_change_unknown (nano::node & node_a, std::vector<nano::account> & accounts_a)
{
	nano::account source (get_random_account (accounts_a));
	if (!node_a.latest (source).is_zero ())
	{
		nano::keypair key;
		nano::account destination (key.pub);
		auto change_error (wallet (0)->change_sync (source, destination));
		(void)change_error;
		debug_assert (!change_error);
	}
}

void nano::test::system::generate_send_new (nano::node & node_a, std::vector<nano::account> & accounts_a)
{
	debug_assert (node_a.wallets.items.size () == 1);
	nano::uint128_t amount;
	nano::account source;
	{
		auto transaction (node_a.store.tx_begin_read ());
		source = get_random_account (accounts_a);
		amount = get_random_amount (transaction, node_a, source);
	}
	if (!amount.is_zero ())
	{
		auto pub (node_a.wallets.items.begin ()->second->deterministic_insert ());
		accounts_a.push_back (pub);
		auto hash (wallet (0)->send_sync (source, pub, amount));
		(void)hash;
		debug_assert (!hash.is_zero ());
	}
}

void nano::test::system::generate_mass_activity (uint32_t count_a, nano::node & node_a)
{
	std::vector<nano::account> accounts;
	auto dev_genesis_key = nano::dev::genesis_key;
	wallet (0)->insert_adhoc (dev_genesis_key.prv);
	accounts.push_back (dev_genesis_key.pub);
	auto previous (std::chrono::steady_clock::now ());
	for (uint32_t i (0); i < count_a; ++i)
	{
		if ((i & 0xff) == 0)
		{
			auto now (std::chrono::steady_clock::now ());
			auto us (std::chrono::duration_cast<std::chrono::microseconds> (now - previous).count ());
			auto count = node_a.ledger.cache.block_count.load ();
			std::cerr << boost::str (boost::format ("Mass activity iteration %1% us %2% us/t %3% block count: %4%\n") % i % us % (us / 256) % count);
			previous = now;
		}
		generate_activity (node_a, accounts);
	}
}

void nano::test::system::stop ()
{
	for (auto i : nodes)
	{
		i->stop ();
	}
	work.stop ();
}

nano::node_config nano::test::system::default_config ()
{
	nano::node_config config{ get_available_port () };
	return config;
}

uint16_t nano::test::system::get_available_port ()
{
	auto base_port_str = std::getenv ("NANO_TEST_BASE_PORT");
	if (!base_port_str)
		return 0; // let the O/S decide

	// Maximum possible sockets which may feasibly be used in 1 test
	constexpr auto max = 200;
	static uint16_t current = 0;

	// Read the TEST_BASE_PORT environment and override the default base port if it exists
	uint16_t base_port = boost::lexical_cast<uint16_t> (base_port_str);

	uint16_t const available_port = base_port + current;
	++current;

	// Reset port number once we have reached the maximum
	if (current >= max)
		current = 0;

	return available_port;
}

// Makes sure everything is cleaned up
void nano::test::cleanup_dev_directories_on_exit ()
{
	// Clean up tmp directories created by the tests. Since it's sometimes useful to
	// see log files after test failures, an environment variable is supported to
	// retain the files.
	if (std::getenv ("TEST_KEEP_TMPDIRS") == nullptr)
	{
		nano::remove_temporary_directories ();
	}
}
