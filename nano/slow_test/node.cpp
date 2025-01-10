#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/thread_runner.hpp>
#include <celerix/lib/work_version.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/confirming_set.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/manual.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/telemetry.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/node/unchecked_map.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/format.hpp>
#include <boost/unordered_set.hpp>

#include <random>

using namespace std::chrono_literals;

/**
 * function to count the block in the pruned store one by one
 * we manually count the blocks one by one because the rocksdb count feature is not accurate
 */
size_t manually_count_pruned_blocks (celerix::store::component & store)
{
	size_t count = 0;
	auto transaction = store.tx_begin_read ();
	auto i = store.pruned.begin (transaction);
	for (; i != store.pruned.end (transaction); ++i)
	{
		++count;
	}
	return count;
}

TEST (system, generate_mass_activity)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.enable_voting = false; // Prevent blocks cementing
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	uint32_t count (20);
	system.generate_mass_activity (count, *system.nodes[0]);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	for (auto i (system.nodes[0]->store.account.begin (transaction)), n (system.nodes[0]->store.account.end (transaction)); i != n; ++i)
	{
	}
}

TEST (system, generate_mass_activity_long)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.enable_voting = false; // Prevent blocks cementing
	auto node = system.add_node (node_config);
	celerix::thread_runner runner (system.io_ctx, system.logger, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	uint32_t count (1000000);
	auto count_env_var = std::getenv ("SLOW_TEST_SYSTEM_GENERATE_MASS_ACTIVITY_LONG_COUNT");
	if (count_env_var)
	{
		count = boost::lexical_cast<uint32_t> (count_env_var);
		std::cout << "count override due to env variable set, count=" << count << std::endl;
	}
	system.generate_mass_activity (count, *system.nodes[0]);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	for (auto i (system.nodes[0]->store.account.begin (transaction)), n (system.nodes[0]->store.account.end (transaction)); i != n; ++i)
	{
	}
	system.stop ();
	runner.join ();
}

TEST (system, receive_while_synchronizing)
{
	std::vector<boost::thread> threads;
	{
		celerix::test::system system;
		celerix::node_config node_config = system.default_config ();
		node_config.enable_voting = false; // Prevent blocks cementing
		auto node = system.add_node (node_config);
		celerix::thread_runner runner (system.io_ctx, system.logger, system.nodes[0]->config.io_threads);
		system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
		uint32_t count (1000);
		system.generate_mass_activity (count, *system.nodes[0]);
		celerix::keypair key;
		auto node1 (std::make_shared<celerix::node> (system.io_ctx, system.get_available_port (), celerix::unique_path (), system.work));
		ASSERT_FALSE (node1->init_error ());
		auto wallet (node1->wallets.create (1));
		wallet->insert_adhoc (celerix::dev::genesis_key.prv); // For voting
		ASSERT_EQ (key.pub, wallet->insert_adhoc (key.prv));
		node1->start ();
		system.nodes.push_back (node1);
		ASSERT_NE (nullptr, celerix::test::establish_tcp (system, *node1, node->network.endpoint ()));
		node1->workers.post_delayed (std::chrono::milliseconds (200), ([&system, &key] () {
			auto hash (system.wallet (0)->send_sync (celerix::dev::genesis_key.pub, key.pub, system.nodes[0]->config.receive_minimum.number ()));
			auto transaction = system.nodes[0]->ledger.tx_begin_read ();
			auto block = system.nodes[0]->ledger.any.block_get (transaction, hash);
			std::string block_text;
			block->serialize_json (block_text);
		}));
		ASSERT_TIMELY (10s, !node1->balance (key.pub).is_zero ());
		node1->stop ();
		system.stop ();
		runner.join ();
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (ledger, deep_account_compute)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_FALSE (store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::keypair key;
	auto balance (celerix::dev::constants.genesis_amount - 1);
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (key.pub)
				.balance (balance)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (celerix::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	auto open = builder
				.open ()
				.source (send->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.account (key.pub)
				.sign (key.prv, key.pub)
				.work (*pool.generate (key.pub))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
	auto sprevious (send->hash ());
	auto rprevious (open->hash ());
	for (auto i (0), n (100000); i != n; ++i)
	{
		balance -= 1;
		auto send = builder
					.send ()
					.previous (sprevious)
					.destination (key.pub)
					.balance (balance)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (sprevious))
					.build ();
		ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
		sprevious = send->hash ();
		auto receive = builder
					   .receive ()
					   .previous (rprevious)
					   .source (send->hash ())
					   .sign (key.prv, key.pub)
					   .work (*pool.generate (rprevious))
					   .build ();
		ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive));
		rprevious = receive->hash ();
		if (i % 100 == 0)
		{
			std::cerr << i << ' ';
		}
		ledger.any.block_account (transaction, sprevious);
		ledger.any.block_balance (transaction, rprevious);
	}
}

/*
 * This test case creates a node and a wallet primed with the genesis account credentials.
 * Then it spawns 'num_of_threads' threads, each doing 'num_of_sends' async sends
 * of 1000 raw each time. The test is considered a success, if the balance of the genesis account
 * reduces by 'num_of_threads * num_of_sends * 1000'.
 */
TEST (wallet, multithreaded_send_async)
{
	std::vector<boost::thread> threads;
	{
		celerix::test::system system (1);
		celerix::keypair key;
		auto wallet_l (system.wallet (0));
		wallet_l->insert_adhoc (celerix::dev::genesis_key.prv);
		wallet_l->insert_adhoc (key.prv);
		int num_of_threads = 20;
		int num_of_sends = 1000;
		for (auto i (0); i < num_of_threads; ++i)
		{
			threads.push_back (boost::thread ([wallet_l, &key, num_of_threads, num_of_sends] () {
				for (auto i (0); i < num_of_sends; ++i)
				{
					wallet_l->send_async (celerix::dev::genesis_key.pub, key.pub, 1000, [] (std::shared_ptr<celerix::block> const & block_a) {
						ASSERT_FALSE (block_a == nullptr);
						ASSERT_FALSE (block_a->hash ().is_zero ());
					});
				}
			}));
		}
		ASSERT_TIMELY_EQ (1000s, system.nodes[0]->balance (celerix::dev::genesis_key.pub), (celerix::dev::constants.genesis_amount - num_of_threads * num_of_sends * 1000));
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (store, load)
{
	celerix::test::system system (1);
	std::vector<boost::thread> threads;
	for (auto i (0); i < 100; ++i)
	{
		threads.push_back (boost::thread ([&system] () {
			for (auto i (0); i != 1000; ++i)
			{
				auto transaction (system.nodes[0]->store.tx_begin_write ());
				for (auto j (0); j != 10; ++j)
				{
					celerix::account account;
					celerix::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
					system.nodes[0]->store.account.put (transaction, account, celerix::account_info ());
				}
			}
		}));
	}
	for (auto & i : threads)
	{
		i.join ();
	}
}

namespace celerix
{
TEST (node, fork_storm)
{
	// WIP against issue #3709
	// This should be set large enough to trigger a test failure, but not so large that
	// simply allocating nodes in a reasonably normal test environment fails. (64 is overkill)
	// On a 12-core/16GB linux server, the failure triggers often with 11 nodes, and almost
	// always with higher values.
	static const auto node_count (23);

	celerix::node_flags flags;
	flags.disable_max_peers_per_ip = true;
	celerix::test::system system (node_count, celerix::transport::transport_type::tcp, flags);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto previous (system.nodes[0]->latest (celerix::dev::genesis_key.pub));
	auto balance (system.nodes[0]->balance (celerix::dev::genesis_key.pub));
	ASSERT_FALSE (previous.is_zero ());
	celerix::block_builder builder;
	for (auto node_j : system.nodes)
	{
		balance -= 1;
		celerix::keypair key;
		auto send = builder
					.send ()
					.previous (previous)
					.destination (key.pub)
					.balance (balance)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (0)
					.build ();
		node_j->work_generate_blocking (*send);
		previous = send->hash ();
		for (auto node_i : system.nodes)
		{
			auto send_result (node_i->process (send));
			ASSERT_EQ (celerix::block_status::progress, send_result);
			celerix::keypair rep;
			auto open = builder
						.open ()
						.source (previous)
						.representative (rep.pub)
						.account (key.pub)
						.sign (key.prv, key.pub)
						.work (0)
						.build ();
			node_i->work_generate_blocking (*open);
			auto open_result (node_i->process (open));
			ASSERT_EQ (celerix::block_status::progress, open_result);
			auto transaction (node_i->store.tx_begin_read ());
			node_i->network.flood_block (open, celerix::transport::traffic_type::test);
		}
	}
	auto again (true);

	int iteration (0);

	// Stall detection (if there is no progress, the test will hang indefinitely)
	auto old_empty (0);
	auto old_single (0);
	auto stall_count (0);

	while (again)
	{
		auto empty = 0;
		auto single = 0;
		std::for_each (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<celerix::node> const & node_a) {
			if (node_a->active.empty ())
			{
				++empty;
			}
			else
			{
				celerix::unique_lock<celerix::mutex> lock{ node_a->active.mutex };
				auto election = node_a->active.roots.begin ()->election;
				lock.unlock ();
				if (election->votes ().size () == 1)
				{
					++single;
				}
			}
		});
		ASSERT_NO_ERROR (system.poll ());

		// If no progress is happening after a lot of iterations
		// this test has uncovered something broken or made some
		// bad assumptions.
		if (old_empty == empty && old_single == single)
		{
			static const auto stall_tolerance (100000);
			++stall_count;
			ASSERT_LE (stall_count, stall_tolerance) << "Stall deteceted. These values were both expected to eventually reach 0 but have remained unchanged for " << stall_tolerance << " iterations. Empty: " << empty << " single: " << single << std::endl;
		}
		else
		{
			stall_count = 0;
			old_empty = empty;
			old_single = single;
		}

		again = (empty != 0) || (single != 0);

		++iteration;
	}
	ASSERT_TRUE (true);
}
} // namespace celerix

namespace
{
size_t heard_count (std::vector<uint8_t> const & nodes)
{
	auto result (0);
	for (auto i (nodes.begin ()), n (nodes.end ()); i != n; ++i)
	{
		switch (*i)
		{
			case 0:
				break;
			case 1:
				++result;
				break;
			case 2:
				++result;
				break;
		}
	}
	return result;
}
}

TEST (broadcast, world_broadcast_simulate)
{
	auto node_count (10000);
	// 0 = starting state
	// 1 = heard transaction
	// 2 = repeated transaction
	std::vector<uint8_t> nodes;
	nodes.resize (node_count, 0);
	nodes[0] = 1;
	auto any_changed (true);
	auto message_count (0);
	while (any_changed)
	{
		any_changed = false;
		for (auto i (nodes.begin ()), n (nodes.end ()); i != n; ++i)
		{
			switch (*i)
			{
				case 0:
					break;
				case 1:
					for (auto j (nodes.begin ()), m (nodes.end ()); j != m; ++j)
					{
						++message_count;
						switch (*j)
						{
							case 0:
								*j = 1;
								any_changed = true;
								break;
							case 1:
								break;
							case 2:
								break;
						}
					}
					*i = 2;
					any_changed = true;
					break;
				case 2:
					break;
				default:
					ASSERT_FALSE (true);
					break;
			}
		}
	}
	auto count (heard_count (nodes));
	(void)count;
}

TEST (broadcast, sqrt_broadcast_simulate)
{
	auto node_count (10000);
	auto broadcast_count (std::ceil (std::sqrt (node_count)));
	// 0 = starting state
	// 1 = heard transaction
	// 2 = repeated transaction
	std::vector<uint8_t> nodes;
	nodes.resize (node_count, 0);
	nodes[0] = 1;
	auto any_changed (true);
	uint64_t message_count (0);
	while (any_changed)
	{
		any_changed = false;
		for (auto i (nodes.begin ()), n (nodes.end ()); i != n; ++i)
		{
			switch (*i)
			{
				case 0:
					break;
				case 1:
					for (auto j (0); j != broadcast_count; ++j)
					{
						++message_count;
						auto entry (celerix::random_pool::generate_word32 (0, node_count - 1));
						switch (nodes[entry])
						{
							case 0:
								nodes[entry] = 1;
								any_changed = true;
								break;
							case 1:
								break;
							case 2:
								break;
						}
					}
					*i = 2;
					any_changed = true;
					break;
				case 2:
					break;
				default:
					ASSERT_FALSE (true);
					break;
			}
		}
	}
	auto count (heard_count (nodes));
	(void)count;
}

TEST (peer_container, random_set)
{
	celerix::test::system system (1);
	auto old (std::chrono::steady_clock::now ());
	auto current (std::chrono::steady_clock::now ());
	for (auto i (0); i < 10000; ++i)
	{
		auto list (system.nodes[0]->network.random_set (15));
	}
	auto end (std::chrono::steady_clock::now ());
	(void)end;
	auto old_ms (std::chrono::duration_cast<std::chrono::milliseconds> (current - old));
	(void)old_ms;
	auto new_ms (std::chrono::duration_cast<std::chrono::milliseconds> (end - current));
	(void)new_ms;
}

// Can take up to 2 hours
TEST (store, unchecked_load)
{
	celerix::test::system system{ 1 };
	auto & node = *system.nodes[0];
	celerix::block_builder builder;
	std::shared_ptr<celerix::block> block = builder
										 .send ()
										 .previous (0)
										 .destination (0)
										 .balance (0)
										 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
										 .work (0)
										 .build ();
	constexpr auto num_unchecked = 1'000'000;
	for (auto i (0); i < num_unchecked; ++i)
	{
		node.unchecked.put (i, block);
	}
	// Waits for all the blocks to get saved in the database
	ASSERT_TIMELY_EQ (8000s, num_unchecked, node.unchecked.count ());
}

TEST (store, vote_load)
{
	celerix::test::system system{ 1 };
	auto & node = *system.nodes[0];
	for (auto i = 0u; i < 1000000u; ++i)
	{
		auto vote = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, i, 0, std::vector<celerix::block_hash>{ i });
		node.vote_processor.vote (vote, std::make_shared<celerix::transport::inproc::channel> (node, node));
	}
}

/**
 * This test does the following:
 *   Creates a persistent database in the file system
 *   Adds 2 million random blocks to the database in chunks of 20 blocks per database transaction
 *   It then deletes half the blocks, soon after adding them
 *   Then it closes the database, reopens the database and checks that it still has the expected amount of blocks
 */
TEST (store, pruned_load)
{
	celerix::logger logger;
	auto path (celerix::unique_path ());
	constexpr auto num_pruned = 2000000;
	auto const expected_result = num_pruned / 2;
	constexpr auto batch_size = 20;
	boost::unordered_set<celerix::block_hash> hashes;
	{
		auto store = celerix::make_store (logger, path, celerix::dev::constants);
		ASSERT_FALSE (store->init_error ());
		for (auto i (0); i < num_pruned / batch_size; ++i)
		{
			{
				// write a batch of random blocks to the pruned store
				auto transaction (store->tx_begin_write ());
				for (auto k (0); k < batch_size; ++k)
				{
					celerix::block_hash random_hash;
					celerix::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
					store->pruned.put (transaction, random_hash);
					hashes.insert (random_hash);
				}
			}
			{
				// delete half of the blocks created above
				auto transaction (store->tx_begin_write ());
				for (auto k (0); !hashes.empty () && k < batch_size / 2; ++k)
				{
					auto hash (hashes.begin ());
					store->pruned.del (transaction, *hash);
					hashes.erase (hash);
				}
			}
		}
		ASSERT_EQ (expected_result, manually_count_pruned_blocks (*store));
	}

	// Reinitialize store
	{
		auto store = celerix::make_store (logger, path, celerix::dev::constants);
		ASSERT_FALSE (store->init_error ());
		ASSERT_EQ (expected_result, manually_count_pruned_blocks (*store));
	}
}

TEST (wallets, rep_scan)
{
	celerix::test::system system (1);
	auto & node (*system.nodes[0]);
	auto wallet (system.wallet (0));
	{
		auto transaction (node.wallets.tx_begin_write ());
		for (auto i (0); i < 10000; ++i)
		{
			wallet->deterministic_insert (transaction);
		}
	}
	auto begin (std::chrono::steady_clock::now ());
	node.wallets.foreach_representative ([] (celerix::public_key const & pub_a, celerix::raw_key const & prv_a) {
	});
	ASSERT_LT (std::chrono::steady_clock::now () - begin, std::chrono::milliseconds (5));
}

TEST (node, mass_vote_by_hash)
{
	celerix::test::system system (1);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_hash previous (celerix::dev::genesis->hash ());
	celerix::keypair key;
	std::vector<std::shared_ptr<celerix::state_block>> blocks;
	celerix::block_builder builder;
	for (auto i (0); i < 10000; ++i)
	{
		auto block = builder
					 .state ()
					 .account (celerix::dev::genesis_key.pub)
					 .previous (previous)
					 .representative (celerix::dev::genesis_key.pub)
					 .balance (celerix::dev::constants.genesis_amount - (i + 1) * celerix::Kcelerix_ratio)
					 .link (key.pub)
					 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					 .work (*system.work.generate (previous))
					 .build ();
		previous = block->hash ();
		blocks.push_back (block);
	}
	for (auto i (blocks.begin ()), n (blocks.end ()); i != n; ++i)
	{
		system.nodes[0]->block_processor.add (*i);
	}
}

namespace celerix
{
TEST (confirmation_height, many_accounts_single_confirmation)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = 100;
	node_config.backlog_scan.enable = false;
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	// The number of frontiers should be more than the celerix::confirmation_height::unbounded_cutoff to test the amount of blocks confirmed is correct.
	auto const num_accounts = celerix::confirmation_height::unbounded_cutoff * 2 + 50;
	celerix::keypair last_keypair = celerix::dev::genesis_key;
	celerix::block_builder builder;
	auto last_open_hash = node->latest (celerix::dev::genesis_key.pub);
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto i = num_accounts - 1; i > 0; --i)
		{
			celerix::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);

			auto send = builder
						.send ()
						.previous (last_open_hash)
						.destination (key.pub)
						.balance (node->online_reps.delta ())
						.sign (last_keypair.prv, last_keypair.pub)
						.work (*system.work.generate (last_open_hash))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send));
			auto open = builder
						.open ()
						.source (send->hash ())
						.representative (last_keypair.pub)
						.account (key.pub)
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, open));
			last_open_hash = open->hash ();
			last_keypair = key;
		}
	}

	// Call block confirm on the last open block which will confirm everything
	{
		auto block = node->block (last_open_hash);
		ASSERT_NE (nullptr, block);
		node->scheduler.manual.push (block);
		std::shared_ptr<celerix::election> election;
		ASSERT_TIMELY (10s, (election = node->active.election (block->qualified_root ())) != nullptr);
		election->force_confirm ();
	}

	ASSERT_TIMELY (120s, node->ledger.confirmed.block_exists_or_pruned (node->ledger.tx_begin_read (), last_open_hash));

	// All frontiers (except last) should have 2 blocks and both should be confirmed
	auto transaction = node->store.tx_begin_read ();
	for (auto i (node->store.account.begin (transaction)), n (node->store.account.end (transaction)); i != n; ++i)
	{
		auto & account = i->first;
		auto & account_info = i->second;
		auto count = (account != last_keypair.pub) ? 2 : 1;
		celerix::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, account, confirmation_height_info));
		ASSERT_EQ (count, confirmation_height_info.height);
		ASSERT_EQ (count, account_info.block_count);
	}

	size_t cemented_count = 0;
	for (auto i (node->ledger.store.confirmation_height.begin (transaction)), n (node->ledger.store.confirmation_height.end (transaction)); i != n; ++i)
	{
		cemented_count += i->second.height;
	}

	ASSERT_EQ (cemented_count, node->ledger.cemented_count ());
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in), num_accounts * 2 - 2);
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_bounded, celerix::stat::dir::in), num_accounts * 2 - 2);
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_unbounded, celerix::stat::dir::in), 0);

	ASSERT_TIMELY_EQ (40s, (node->ledger.cemented_count () - 1), node->stats.count (celerix::stat::type::confirmation_observer, celerix::stat::dir::out));
}

TEST (confirmation_height, many_accounts_many_confirmations)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = 100;
	node_config.backlog_scan.enable = false;
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	auto const num_accounts = celerix::confirmation_height::unbounded_cutoff * 2 + 50;
	auto latest_genesis = node->latest (celerix::dev::genesis_key.pub);
	celerix::block_builder builder;
	std::vector<std::shared_ptr<celerix::open_block>> open_blocks;
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto i = num_accounts - 1; i > 0; --i)
		{
			celerix::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);

			auto send = builder
						.send ()
						.previous (latest_genesis)
						.destination (key.pub)
						.balance (node->online_reps.delta ())
						.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						.work (*system.work.generate (latest_genesis))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send));
			auto open = builder
						.open ()
						.source (send->hash ())
						.representative (celerix::dev::genesis_key.pub)
						.account (key.pub)
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, open));
			open_blocks.push_back (std::move (open));
			latest_genesis = send->hash ();
		}
	}

	// Confirm all of the accounts
	for (auto & open_block : open_blocks)
	{
		node->scheduler.manual.push (open_block);
		std::shared_ptr<celerix::election> election;
		ASSERT_TIMELY (10s, (election = node->active.election (open_block->qualified_root ())) != nullptr);
		election->force_confirm ();
	}

	auto const num_blocks_to_confirm = (num_accounts - 1) * 2;
	ASSERT_TIMELY_EQ (1500s, node->stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in), num_blocks_to_confirm);

	auto num_confirmed_bounded = node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_bounded, celerix::stat::dir::in);
	ASSERT_GE (num_confirmed_bounded, celerix::confirmation_height::unbounded_cutoff);
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_unbounded, celerix::stat::dir::in), num_blocks_to_confirm - num_confirmed_bounded);

	ASSERT_TIMELY_EQ (60s, (node->ledger.cemented_count () - 1), node->stats.count (celerix::stat::type::confirmation_observer, celerix::stat::dir::out));

	auto transaction = node->store.tx_begin_read ();
	size_t cemented_count = 0;
	for (auto i (node->ledger.store.confirmation_height.begin (transaction)), n (node->ledger.store.confirmation_height.end (transaction)); i != n; ++i)
	{
		cemented_count += i->second.height;
	}

	ASSERT_EQ (num_blocks_to_confirm + 1, cemented_count);
	ASSERT_EQ (cemented_count, node->ledger.cemented_count ());

	ASSERT_TIMELY_EQ (20s, (node->ledger.cemented_count () - 1), node->stats.count (celerix::stat::type::confirmation_observer, celerix::stat::dir::out));
}

TEST (confirmation_height, long_chains)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto node = system.add_node (node_config);
	celerix::keypair key1;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_hash latest (node->latest (celerix::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key1.prv);

	auto const num_blocks = celerix::confirmation_height::unbounded_cutoff * 2 + 50;

	celerix::block_builder builder;
	// First open the other account
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key1.pub)
				.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio + num_blocks + 1)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	auto open = builder
				.open ()
				.source (send->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.account (key1.pub)
				.sign (key1.prv, key1.pub)
				.work (*system.work.generate (key1.pub))
				.build ();
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, open));
	}

	// Bulk send from genesis account to destination account
	auto previous_genesis_chain_hash = send->hash ();
	auto previous_destination_chain_hash = open->hash ();
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto i = num_blocks - 1; i > 0; --i)
		{
			auto send = builder
						.send ()
						.previous (previous_genesis_chain_hash)
						.destination (key1.pub)
						.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio + i + 1)
						.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						.work (*system.work.generate (previous_genesis_chain_hash))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send));
			auto receive = builder
						   .receive ()
						   .previous (previous_destination_chain_hash)
						   .source (send->hash ())
						   .sign (key1.prv, key1.pub)
						   .work (*system.work.generate (previous_destination_chain_hash))
						   .build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, receive));

			previous_genesis_chain_hash = send->hash ();
			previous_destination_chain_hash = receive->hash ();
		}
	}

	// Send one from destination to genesis and pocket it
	auto send1 = builder
				 .send ()
				 .previous (previous_destination_chain_hash)
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::Kcelerix_ratio - 2)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (previous_destination_chain_hash))
				 .build ();
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (previous_genesis_chain_hash)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio + 1)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*system.work.generate (previous_genesis_chain_hash))
					.build ();

	// Unpocketed. Send to a non-existing account to prevent auto receives from the wallet adjusting expected confirmation height
	celerix::keypair key2;
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (receive1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key2.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (receive1->hash ()))
				 .build ();

	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send1));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, receive1));
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send2));
	}

	// Call block confirm on the existing receive block on the genesis account which will confirm everything underneath on both accounts
	{
		node->scheduler.manual.push (receive1);
		std::shared_ptr<celerix::election> election;
		ASSERT_TIMELY (10s, (election = node->active.election (receive1->qualified_root ())) != nullptr);
		election->force_confirm ();
	}

	ASSERT_TIMELY (30s, node->ledger.confirmed.block_exists_or_pruned (node->ledger.tx_begin_read (), receive1->hash ()));

	auto transaction = node->ledger.tx_begin_read ();
	auto info = node->ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info);
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (node->store.confirmation_height.get (transaction, celerix::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (num_blocks + 2, confirmation_height_info.height);
	ASSERT_EQ (num_blocks + 3, info->block_count); // Includes the unpocketed send

	info = node->ledger.any.account_get (transaction, key1.pub);
	ASSERT_TRUE (info);
	ASSERT_FALSE (node->store.confirmation_height.get (transaction, key1.pub, confirmation_height_info));
	ASSERT_EQ (num_blocks + 1, confirmation_height_info.height);
	ASSERT_EQ (num_blocks + 1, info->block_count);

	size_t cemented_count = 0;
	for (auto i (node->ledger.store.confirmation_height.begin (transaction)), n (node->ledger.store.confirmation_height.end (transaction)); i != n; ++i)
	{
		cemented_count += i->second.height;
	}

	ASSERT_EQ (cemented_count, node->ledger.cemented_count ());
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in), num_blocks * 2 + 2);
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_bounded, celerix::stat::dir::in), num_blocks * 2 + 2);
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_unbounded, celerix::stat::dir::in), 0);

	ASSERT_TIMELY_EQ (40s, (node->ledger.cemented_count () - 1), node->stats.count (celerix::stat::type::confirmation_observer, celerix::stat::dir::out));
}

TEST (confirmation_height, dynamic_algorithm)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto node = system.add_node (node_config);
	celerix::keypair key;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto const num_blocks = celerix::confirmation_height::unbounded_cutoff;
	auto latest_genesis = celerix::dev::genesis;
	std::vector<std::shared_ptr<celerix::state_block>> state_blocks;
	celerix::block_builder builder;
	for (auto i = 0; i < num_blocks; ++i)
	{
		auto send = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (latest_genesis->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount - i - 1)
					.link (key.pub)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*system.work.generate (latest_genesis->hash ()))
					.build ();
		latest_genesis = send;
		state_blocks.push_back (send);
	}
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto const & block : state_blocks)
		{
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, block));
		}
	}

	node->confirming_set.add (state_blocks.front ()->hash ());
	ASSERT_TIMELY_EQ (20s, node->ledger.cemented_count (), 2);

	node->confirming_set.add (latest_genesis->hash ());

	ASSERT_TIMELY_EQ (20s, node->ledger.cemented_count (), num_blocks + 1);

	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in), num_blocks);
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_bounded, celerix::stat::dir::in), 1);
	ASSERT_EQ (node->ledger.stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed_unbounded, celerix::stat::dir::in), num_blocks - 1);
}

TEST (confirmation_height, many_accounts_send_receive_self)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = 100;
	node_config.backlog_scan.enable = false;
	node_config.active_elections.size = 400000;
	celerix::node_flags node_flags;
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

#ifndef NDEBUG
	auto const num_accounts = 10000;
#else
	auto const num_accounts = 100000;
#endif

	auto latest_genesis = node->latest (celerix::dev::genesis_key.pub);
	std::vector<celerix::keypair> keys;
	celerix::block_builder builder;
	std::vector<std::shared_ptr<celerix::open_block>> open_blocks;
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto i = 0; i < num_accounts; ++i)
		{
			celerix::keypair key;
			keys.emplace_back (key);

			auto send = builder
						.send ()
						.previous (latest_genesis)
						.destination (key.pub)
						.balance (celerix::dev::constants.genesis_amount - 1 - i)
						.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						.work (*system.work.generate (latest_genesis))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send));
			auto open = builder
						.open ()
						.source (send->hash ())
						.representative (celerix::dev::genesis_key.pub)
						.account (key.pub)
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, open));
			open_blocks.push_back (std::move (open));
			latest_genesis = send->hash ();
		}
	}

	// Confirm all of the accounts
	for (auto & open_block : open_blocks)
	{
		node->start_election (open_block);
		std::shared_ptr<celerix::election> election;
		ASSERT_TIMELY (10s, (election = node->active.election (open_block->qualified_root ())) != nullptr);
		election->force_confirm ();
	}

	system.deadline_set (100s);
	auto num_blocks_to_confirm = num_accounts * 2;
	while (node->stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in) != num_blocks_to_confirm)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	std::vector<std::shared_ptr<celerix::send_block>> send_blocks;
	std::vector<std::shared_ptr<celerix::receive_block>> receive_blocks;

	for (int i = 0; i < open_blocks.size (); ++i)
	{
		auto open_block = open_blocks[i];
		auto & keypair = keys[i];
		send_blocks.emplace_back (builder
								  .send ()
								  .previous (open_block->hash ())
								  .destination (keypair.pub)
								  .balance (1)
								  .sign (keypair.prv, keypair.pub)
								  .work (*system.work.generate (open_block->hash ()))
								  .build ());
		receive_blocks.emplace_back (builder
									 .receive ()
									 .previous (send_blocks.back ()->hash ())
									 .source (send_blocks.back ()->hash ())
									 .sign (keypair.prv, keypair.pub)
									 .work (*system.work.generate (send_blocks.back ()->hash ()))
									 .build ());
	}

	// Now send and receive to self
	for (int i = 0; i < open_blocks.size (); ++i)
	{
		node->process_active (send_blocks[i]);
		node->process_active (receive_blocks[i]);
	}

	system.deadline_set (300s);
	num_blocks_to_confirm = num_accounts * 4;
	while (node->stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in) != num_blocks_to_confirm)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	system.deadline_set (200s);
	while ((node->ledger.cemented_count () - 1) != node->stats.count (celerix::stat::type::confirmation_observer, celerix::stat::dir::out))
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto transaction = node->store.tx_begin_read ();
	size_t cemented_count = 0;
	for (auto i (node->ledger.store.confirmation_height.begin (transaction)), n (node->ledger.store.confirmation_height.end (transaction)); i != n; ++i)
	{
		cemented_count += i->second.height;
	}

	ASSERT_EQ (num_blocks_to_confirm + 1, cemented_count);
	ASSERT_EQ (cemented_count, node->ledger.cemented_count ());

	system.deadline_set (60s);
	while ((node->ledger.cemented_count () - 1) != node->stats.count (celerix::stat::type::confirmation_observer, celerix::stat::dir::out))
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	system.deadline_set (60s);
}

// Same as the many_accounts_send_receive_self test, except works on the confirmation height processor directly
// as opposed to active transactions which implicitly calls confirmation height processor.
TEST (confirmation_height, many_accounts_send_receive_self_no_elections)
{
	celerix::test::system system;
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	celerix::logger logger;
	auto path (celerix::unique_path ());
	auto store = celerix::make_store (logger, path, celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	celerix::store::write_queue write_database_queue;
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	std::atomic<bool> stopped{ false };
	boost::latch initialized_latch{ 0 };

	celerix::block_hash block_hash_being_processed{ 0 };
	celerix::store::write_queue write_queue;

	celerix::node_config node_config;
	celerix::unchecked_map unchecked{ 0, stats, false };
	celerix::block_processor block_processor{ node_config, ledger, unchecked, stats, logger };
	celerix::confirming_set_config confirming_set_config{};
	celerix::confirming_set confirming_set{ confirming_set_config, ledger, block_processor, stats, logger };

	auto const num_accounts = 100000;

	auto latest_genesis = celerix::dev::genesis->hash ();
	std::vector<celerix::keypair> keys;
	std::vector<std::shared_ptr<celerix::open_block>> open_blocks;

	celerix::block_builder builder;

	{
		auto transaction = ledger.tx_begin_write ();
		store->initialize (transaction, ledger.cache, ledger.constants);

		// Send from genesis account to all other accounts and create open block for them
		for (auto i = 0; i < num_accounts; ++i)
		{
			celerix::keypair key;
			keys.emplace_back (key);
			auto send = builder
						.send ()
						.previous (latest_genesis)
						.destination (key.pub)
						.balance (celerix::dev::constants.genesis_amount - 1 - i)
						.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						.work (*pool.generate (latest_genesis))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
			auto open = builder
						.open ()
						.source (send->hash ())
						.representative (celerix::dev::genesis_key.pub)
						.account (key.pub)
						.sign (key.prv, key.pub)
						.work (*pool.generate (key.pub))
						.build ();
			ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
			open_blocks.push_back (std::move (open));
			latest_genesis = send->hash ();
		}
	}

	for (auto & open_block : open_blocks)
	{
		confirming_set.add (open_block->hash ());
	}

	system.deadline_set (1000s);
	auto num_blocks_to_confirm = num_accounts * 2;
	while (stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in) != num_blocks_to_confirm)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	std::vector<std::shared_ptr<celerix::send_block>> send_blocks;
	std::vector<std::shared_ptr<celerix::receive_block>> receive_blocks;

	// Now add all send/receive blocks
	{
		auto transaction = ledger.tx_begin_write ();
		for (int i = 0; i < open_blocks.size (); ++i)
		{
			auto open_block = open_blocks[i];
			auto & keypair = keys[i];
			send_blocks.emplace_back (builder
									  .send ()
									  .previous (open_block->hash ())
									  .destination (keypair.pub)
									  .balance (1)
									  .sign (keypair.prv, keypair.pub)
									  .work (*system.work.generate (open_block->hash ()))
									  .build ());
			receive_blocks.emplace_back (builder
										 .receive ()
										 .previous (send_blocks.back ()->hash ())
										 .source (send_blocks.back ()->hash ())
										 .sign (keypair.prv, keypair.pub)
										 .work (*system.work.generate (send_blocks.back ()->hash ()))
										 .build ());

			ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send_blocks.back ()));
			ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive_blocks.back ()));
		}
	}

	// Randomize the order that send and receive blocks are added to the confirmation height processor
	std::random_device rd;
	std::mt19937 g (rd ());
	std::shuffle (send_blocks.begin (), send_blocks.end (), g);
	std::mt19937 g1 (rd ());
	std::shuffle (receive_blocks.begin (), receive_blocks.end (), g1);

	// Now send and receive to self
	for (int i = 0; i < open_blocks.size (); ++i)
	{
		confirming_set.add (send_blocks[i]->hash ());
		confirming_set.add (receive_blocks[i]->hash ());
	}

	system.deadline_set (1000s);
	num_blocks_to_confirm = num_accounts * 4;
	while (stats.count (celerix::stat::type::confirmation_height, celerix::stat::detail::blocks_confirmed, celerix::stat::dir::in) != num_blocks_to_confirm)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	while (confirming_set.size () > 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto transaction = store->tx_begin_read ();
	size_t cemented_count = 0;
	for (auto i (store->confirmation_height.begin (transaction)), n (store->confirmation_height.end (transaction)); i != n; ++i)
	{
		cemented_count += i->second.height;
	}

	ASSERT_EQ (num_blocks_to_confirm + 1, cemented_count);
	ASSERT_EQ (cemented_count, ledger.cemented_count ());
}

}

namespace
{
class data
{
public:
	std::atomic<bool> awaiting_cache{ false };
	std::atomic<bool> keep_requesting_metrics{ true };
	std::shared_ptr<celerix::node> node;
	std::chrono::system_clock::time_point orig_time;
	std::atomic_flag orig_time_set = ATOMIC_FLAG_INIT;
};
class shared_data
{
public:
	celerix::test::counted_completion write_completion{ 0 };
	std::atomic<bool> done{ false };
};

template <typename T>
void callback_process (shared_data & shared_data_a, data & data, T & all_node_data_a, std::chrono::system_clock::time_point last_updated)
{
	if (!data.orig_time_set.test_and_set ())
	{
		data.orig_time = last_updated;
	}

	if (data.awaiting_cache && data.orig_time != last_updated)
	{
		data.keep_requesting_metrics = false;
	}
	if (data.orig_time != last_updated)
	{
		data.awaiting_cache = true;
		data.orig_time = last_updated;
	}
	shared_data_a.write_completion.increment ();
};
}

TEST (telemetry, ongoing_requests)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	celerix::test::wait_peer_connections (system);

	ASSERT_EQ (0, node_client->telemetry.size ());
	ASSERT_EQ (0, node_server->telemetry.size ());
	ASSERT_EQ (0, node_client->stats.count (celerix::stat::type::bootstrap, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in));
	ASSERT_EQ (0, node_client->stats.count (celerix::stat::type::bootstrap, celerix::stat::detail::telemetry_req, celerix::stat::dir::out));

	ASSERT_TIMELY (20s, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in) == 1 && node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in) == 1);

	// Wait till the next ongoing will be called, and add a 1s buffer for the actual processing
	auto time = std::chrono::steady_clock::now ();
	ASSERT_TIMELY (10s, std::chrono::steady_clock::now () >= (time + celerix::dev::network_params.network.telemetry_cache_cutoff + 1s));

	ASSERT_EQ (2, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in));
	ASSERT_EQ (2, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::in));
	ASSERT_EQ (2, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::out));
	ASSERT_EQ (2, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in));
	ASSERT_EQ (2, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::in));
	ASSERT_EQ (2, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::out));
}

namespace celerix
{
namespace transport
{
	TEST (telemetry, simultaneous_requests)
	{
		celerix::test::system system;
		celerix::node_flags node_flags;
		auto const num_nodes = 4;
		for (int i = 0; i < num_nodes; ++i)
		{
			system.add_node (node_flags);
		}

		celerix::test::wait_peer_connections (system);

		std::vector<std::thread> threads;
		auto const num_threads = 4;

		std::array<data, num_nodes> node_data{};
		for (auto i = 0; i < num_nodes; ++i)
		{
			node_data[i].node = system.nodes[i];
		}

		shared_data shared_data;

		// Create a few threads where each node sends out telemetry request messages to all other nodes continuously, until the cache it reached and subsequently expired.
		// The test waits until all telemetry_ack messages have been received.
		for (int i = 0; i < num_threads; ++i)
		{
			threads.emplace_back ([&node_data, &shared_data] () {
				while (std::any_of (node_data.cbegin (), node_data.cend (), [] (auto const & data) { return data.keep_requesting_metrics.load (); }))
				{
					for (auto & data : node_data)
					{
						// Keep calling get_metrics_async until the cache has been saved and then become outdated (after a certain period of time) for each node
						if (data.keep_requesting_metrics)
						{
							shared_data.write_completion.increment_required_count ();

							// Pick first peer to be consistent
							auto peer = data.node->network.tcp_channels.channels[0].channel;

							auto maybe_telemetry = data.node->telemetry.get_telemetry (peer->get_remote_endpoint ());
							if (maybe_telemetry)
							{
								callback_process (shared_data, data, node_data, maybe_telemetry->timestamp);
							}
						}
						std::this_thread::sleep_for (1ms);
					}
				}

				shared_data.write_completion.await_count_for (20s);
				shared_data.done = true;
			});
		}

		ASSERT_TIMELY (30s, shared_data.done);

		ASSERT_TRUE (std::all_of (node_data.begin (), node_data.end (), [] (auto const & data) { return !data.keep_requesting_metrics; }));

		for (auto & thread : threads)
		{
			thread.join ();
		}
	}
}
}

TEST (telemetry, under_load)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	celerix::node_flags node_flags;
	auto node = system.add_node (node_config, node_flags);
	node_config.peering_port = system.get_available_port ();
	auto node1 = system.add_node (node_config, node_flags);
	celerix::keypair key;
	celerix::keypair key1;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto latest_genesis = node->latest (celerix::dev::genesis_key.pub);
	auto num_blocks = 150000;
	celerix::block_builder builder;
	auto send = builder
				.state ()
				.account (celerix::dev::genesis_key.pub)
				.previous (latest_genesis)
				.representative (celerix::dev::genesis_key.pub)
				.balance (celerix::dev::constants.genesis_amount - num_blocks)
				.link (key.pub)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (latest_genesis))
				.build ();
	node->process_active (send);
	latest_genesis = send->hash ();
	auto open = builder
				.state ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (num_blocks)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	node->process_active (open);
	auto latest_key = open->hash ();

	auto thread_func = [key1, &system, node, num_blocks] (celerix::keypair const & keypair, celerix::block_hash const & latest, celerix::uint128_t const initial_amount) {
		auto latest_l = latest;
		celerix::block_builder builder;
		for (int i = 0; i < num_blocks; ++i)
		{
			auto send = builder
						.state ()
						.account (keypair.pub)
						.previous (latest_l)
						.representative (keypair.pub)
						.balance (initial_amount - i - 1)
						.link (key1.pub)
						.sign (keypair.prv, keypair.pub)
						.work (*system.work.generate (latest_l))
						.build ();
			latest_l = send->hash ();
			node->process_active (send);
		}
	};

	std::thread thread1 (thread_func, celerix::dev::genesis_key, latest_genesis, celerix::dev::constants.genesis_amount - num_blocks);
	std::thread thread2 (thread_func, key, latest_key, num_blocks);

	ASSERT_TIMELY_EQ (200s, node1->ledger.block_count (), num_blocks * 2 + 3);

	thread1.join ();
	thread2.join ();

	for (auto const & node : system.nodes)
	{
		ASSERT_EQ (0, node->stats.count (celerix::stat::type::telemetry, celerix::stat::detail::failed_send_telemetry_req));
		ASSERT_EQ (0, node->stats.count (celerix::stat::type::telemetry, celerix::stat::detail::request_within_protection_cache_zone));
		ASSERT_EQ (0, node->stats.count (celerix::stat::type::telemetry, celerix::stat::detail::unsolicited_telemetry_ack));
		ASSERT_EQ (0, node->stats.count (celerix::stat::type::telemetry, celerix::stat::detail::no_response_received));
	}
}

/**
 * This test checks that the telemetry cached data is consistent and that it timeouts when it should.
 * It does the following:
 * It disables ongoing telemetry requests and creates 2 nodes, client and server.
 * The client node sends a manual telemetry req to the server node and waits for the telemetry reply.
 * The telemetry reply is saved in the callback and then it is also requested via celerix::telemetry::get_metrics().
 * The 2 telemetry data obtained by the 2 different methods are checked that they are the same.
 * Then the test idles until the telemetry data timeouts from the cache.
 * Then the manual req and reply process is repeated and checked.
 */
TEST (telemetry, cache_read_and_timeout)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	celerix::test::wait_peer_connections (system);

	// Request telemetry metrics
	std::optional<celerix::telemetry_data> telemetry_data;
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (channel, nullptr);

	node_client->telemetry.trigger ();
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));

	auto responses = node_client->telemetry.get_all_telemetries ();
	ASSERT_TRUE (!responses.empty ());
	ASSERT_EQ (telemetry_data, responses.begin ()->second);

	// Confirm only 1 request was made
	ASSERT_EQ (1, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in));
	ASSERT_EQ (0, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::in));
	ASSERT_EQ (1, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::out));
	ASSERT_EQ (0, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in));
	ASSERT_EQ (1, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::in));
	ASSERT_EQ (0, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::out));

	// wait until the telemetry data times out
	ASSERT_TIMELY (5s, node_client->telemetry.get_all_telemetries ().empty ());

	// the telemetry data cache should be empty now
	responses = node_client->telemetry.get_all_telemetries ();
	ASSERT_TRUE (responses.empty ());

	// Request telemetry metrics again
	node_client->telemetry.trigger ();
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));

	responses = node_client->telemetry.get_all_telemetries ();
	ASSERT_TRUE (!responses.empty ());
	ASSERT_EQ (telemetry_data, responses.begin ()->second);

	ASSERT_EQ (2, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in));
	ASSERT_EQ (0, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::in));
	ASSERT_EQ (2, node_client->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::out));
	ASSERT_EQ (0, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_ack, celerix::stat::dir::in));
	ASSERT_EQ (2, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::in));
	ASSERT_EQ (0, node_server->stats.count (celerix::stat::type::message, celerix::stat::detail::telemetry_req, celerix::stat::dir::out));
}

TEST (telemetry, many_nodes)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.disable_request_loop = true;
	// The telemetry responses can timeout if using a large number of nodes under sanitizers, so lower the number.
	auto const num_nodes = celerix::memory_intensive_instrumentation () ? 4 : 10;
	for (auto i = 0; i < num_nodes; ++i)
	{
		celerix::node_config node_config = system.default_config ();
		// Make a metric completely different for each node so we can check afterwards that there are no duplicates
		node_config.bandwidth_limit = 100000 + i;

		auto node = std::make_shared<celerix::node> (system.io_ctx, celerix::unique_path (), node_config, system.work, node_flags);
		node->start ();
		system.nodes.push_back (node);
	}

	// Merge peers after creating nodes as some backends (RocksDB) can take a while to initialize nodes (Windows/Debug for instance)
	// and timeouts can occur between nodes while starting up many nodes synchronously.
	for (auto const & node : system.nodes)
	{
		for (auto const & other_node : system.nodes)
		{
			if (node != other_node)
			{
				node->network.merge_peer (other_node->network.endpoint ());
			}
		}
	}

	celerix::test::wait_peer_connections (system);

	// Give all nodes a non-default number of blocks
	celerix::keypair key;
	celerix::block_builder builder;
	auto send = builder
				.state ()
				.account (celerix::dev::genesis_key.pub)
				.previous (celerix::dev::genesis->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.balance (celerix::dev::constants.genesis_amount - celerix::celerix_ratio)
				.link (key.pub)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (celerix::dev::genesis->hash ()))
				.build ();
	for (auto node : system.nodes)
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send));
	}

	// This is the node which will request metrics from all other nodes
	auto node_client = system.nodes.front ();

	std::vector<celerix::telemetry_data> telemetry_datas;
	auto peers = node_client->network.list (num_nodes - 1);
	ASSERT_EQ (peers.size (), num_nodes - 1);
	for (auto const & peer : peers)
	{
		std::optional<celerix::telemetry_data> telemetry_data;
		ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (peer->get_remote_endpoint ()));
		telemetry_datas.push_back (*telemetry_data);
	}

	ASSERT_EQ (telemetry_datas.size (), num_nodes - 1);

	// Check the metrics
	for (auto & data : telemetry_datas)
	{
		ASSERT_EQ (data.unchecked_count, 0);
		ASSERT_EQ (data.cemented_count, 1);
		ASSERT_LE (data.peer_count, 9U);
		ASSERT_EQ (data.account_count, 1);
		ASSERT_EQ (data.block_count, 2);
		ASSERT_EQ (data.protocol_version, celerix::dev::network_params.network.protocol_version);
		ASSERT_GE (data.bandwidth_cap, 100000);
		ASSERT_LT (data.bandwidth_cap, 100000 + system.nodes.size ());
		ASSERT_EQ (data.major_version, celerix::get_major_node_version ());
		ASSERT_EQ (data.minor_version, celerix::get_minor_node_version ());
		ASSERT_EQ (data.patch_version, celerix::get_patch_node_version ());
		ASSERT_EQ (data.pre_release_version, celerix::get_pre_release_node_version ());
		ASSERT_EQ (data.maker, 0);
		ASSERT_LT (data.uptime, 100);
		ASSERT_EQ (data.genesis_block, celerix::dev::genesis->hash ());
		ASSERT_LE (data.timestamp, std::chrono::system_clock::now ());
		ASSERT_EQ (data.active_difficulty, system.nodes.front ()->default_difficulty (celerix::work_version::work_1));
	}

	// We gave some nodes different bandwidth caps, confirm they are not all the same
	auto bandwidth_cap = telemetry_datas.front ().bandwidth_cap;
	telemetry_datas.erase (telemetry_datas.begin ());
	auto all_bandwidth_limits_same = std::all_of (telemetry_datas.begin (), telemetry_datas.end (), [bandwidth_cap] (auto & telemetry_data) {
		return telemetry_data.bandwidth_cap == bandwidth_cap;
	});
	ASSERT_FALSE (all_bandwidth_limits_same);
}

// Test the node epoch_upgrader with a large number of accounts and threads
// Possible to manually add work peers
TEST (node, mass_epoch_upgrader)
{
	auto perform_test = [] (size_t const batch_size) {
		unsigned threads = 5;
		size_t total_accounts = 2500;

#ifndef NDEBUG
		total_accounts /= 5;
#endif

		struct info
		{
			celerix::keypair key;
			celerix::block_hash pending_hash;
		};

		std::vector<info> opened (total_accounts / 2);
		std::vector<info> unopened (total_accounts / 2);

		celerix::test::system system;
		celerix::node_config node_config = system.default_config ();
		node_config.work_threads = 4;
		// node_config.work_peers = { { "192.168.1.101", 7000 } };
		auto & node = *system.add_node (node_config);

		auto balance = node.balance (celerix::dev::genesis_key.pub);
		auto latest = node.latest (celerix::dev::genesis_key.pub);
		celerix::uint128_t amount = 1;

		// Send to all accounts
		std::array<std::vector<info> *, 2> all{ &opened, &unopened };
		for (auto & accounts : all)
		{
			for (auto & info : *accounts)
			{
				balance -= amount;
				celerix::state_block_builder builder;
				std::error_code ec;
				auto block = builder
							 .account (celerix::dev::genesis_key.pub)
							 .previous (latest)
							 .balance (balance)
							 .link (info.key.pub)
							 .representative (celerix::dev::genesis_key.pub)
							 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
							 .work (*node.work_generate_blocking (latest, node_config.network_params.work.threshold (celerix::work_version::work_1, celerix::block_details (celerix::epoch::epoch_0, false, false, false))))
							 .build (ec);
				ASSERT_FALSE (ec);
				ASSERT_NE (nullptr, block);
				ASSERT_EQ (celerix::block_status::progress, node.process (block));
				latest = block->hash ();
				info.pending_hash = block->hash ();
			}
		}
		ASSERT_EQ (1 + total_accounts, node.ledger.block_count ());
		ASSERT_EQ (1, node.ledger.account_count ());

		// Receive for half of accounts
		for (auto const & info : opened)
		{
			celerix::state_block_builder builder;
			std::error_code ec;
			auto block = builder
						 .account (info.key.pub)
						 .previous (0)
						 .balance (amount)
						 .link (info.pending_hash)
						 .representative (info.key.pub)
						 .sign (info.key.prv, info.key.pub)
						 .work (*node.work_generate_blocking (info.key.pub, node_config.network_params.work.threshold (celerix::work_version::work_1, celerix::block_details (celerix::epoch::epoch_0, false, false, false))))
						 .build (ec);
			ASSERT_FALSE (ec);
			ASSERT_NE (nullptr, block);
			ASSERT_EQ (celerix::block_status::progress, node.process (block));
		}
		ASSERT_EQ (1 + total_accounts + opened.size (), node.ledger.block_count ());
		ASSERT_EQ (1 + opened.size (), node.ledger.account_count ());

		celerix::keypair epoch_signer (celerix::dev::genesis_key);

		auto const block_count_before = node.ledger.block_count ();
		auto const total_to_upgrade = 1 + total_accounts;
		std::cout << "Mass upgrading " << total_to_upgrade << " accounts" << std::endl;
		while (node.ledger.block_count () != block_count_before + total_to_upgrade)
		{
			auto const pre_upgrade = node.ledger.block_count ();
			auto upgrade_count = std::min<size_t> (batch_size, block_count_before + total_to_upgrade - pre_upgrade);
			ASSERT_FALSE (node.epoch_upgrader.start (epoch_signer.prv, celerix::epoch::epoch_1, upgrade_count, threads));
			// Already ongoing - should fail
			ASSERT_TRUE (node.epoch_upgrader.start (epoch_signer.prv, celerix::epoch::epoch_1, upgrade_count, threads));
			system.deadline_set (60s);
			while (node.ledger.block_count () != pre_upgrade + upgrade_count)
			{
				ASSERT_NO_ERROR (system.poll ());
				std::this_thread::sleep_for (200ms);
				std::cout << node.ledger.block_count () - block_count_before << " / " << total_to_upgrade << std::endl;
			}
			std::this_thread::sleep_for (50ms);
		}
		auto expected_blocks = block_count_before + total_accounts + 1;
		ASSERT_EQ (expected_blocks, node.ledger.block_count ());
		// Check upgrade
		{
			auto transaction (node.store.tx_begin_read ());
			size_t block_count_sum = 0;
			for (auto i (node.store.account.begin (transaction)); i != node.store.account.end (transaction); ++i)
			{
				celerix::account_info info (i->second);
				ASSERT_EQ (info.epoch (), celerix::epoch::epoch_1);
				block_count_sum += info.block_count;
			}
			ASSERT_EQ (expected_blocks, block_count_sum);
		}
	};
	// Test with a limited number of upgrades and an unlimited
	perform_test (42);
	perform_test (std::numeric_limits<size_t>::max ());
}

namespace celerix
{
TEST (node, mass_block_new)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node = *system.add_node (node_config);
	node.network_params.network.aec_loop_interval_ms = 500;

#ifndef NDEBUG
	auto const num_blocks = 5000;
#else
	auto const num_blocks = 50000;
#endif
	std::cout << num_blocks << " x4 blocks" << std::endl;

	// Upgrade to epoch_2
	system.upgrade_genesis_epoch (node, celerix::epoch::epoch_1);
	system.upgrade_genesis_epoch (node, celerix::epoch::epoch_2);

	auto next_block_count = num_blocks + 3;
	auto process_all = [&] (std::vector<std::shared_ptr<celerix::state_block>> const & blocks_a) {
		for (auto const & block : blocks_a)
		{
			node.process_active (block);
		}
		ASSERT_TIMELY_EQ (200s, node.ledger.block_count (), next_block_count);
		next_block_count += num_blocks;
		while (node.block_processor.size () > 0)
		{
			std::this_thread::sleep_for (std::chrono::milliseconds{ 100 });
		}
		// Clear all active
		{
			celerix::lock_guard<celerix::mutex> guard{ node.active.mutex };
			node.active.roots.clear ();
		}
	};

	celerix::keypair key;
	std::vector<celerix::keypair> keys (num_blocks);
	celerix::state_block_builder builder;
	std::vector<std::shared_ptr<celerix::state_block>> send_blocks;
	auto send_threshold (celerix::dev::network_params.work.threshold (celerix::work_version::work_1, celerix::block_details (celerix::epoch::epoch_2, true, false, false)));
	auto latest_genesis = node.latest (celerix::dev::genesis_key.pub);
	for (auto i = 0; i < num_blocks; ++i)
	{
		auto send = builder.make_block ()
					.account (celerix::dev::genesis_key.pub)
					.previous (latest_genesis)
					.balance (celerix::dev::constants.genesis_amount - i - 1)
					.representative (celerix::dev::genesis_key.pub)
					.link (keys[i].pub)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*system.work.generate (celerix::work_version::work_1, latest_genesis, send_threshold))
					.build ();
		latest_genesis = send->hash ();
		send_blocks.push_back (std::move (send));
	}
	std::cout << "Send blocks built, start processing" << std::endl;
	celerix::timer<> timer;
	timer.start ();
	process_all (send_blocks);
	std::cout << "Send blocks time: " << timer.stop ().count () << " " << timer.unit () << "\n\n";

	std::vector<std::shared_ptr<celerix::state_block>> open_blocks;
	auto receive_threshold (celerix::dev::network_params.work.threshold (celerix::work_version::work_1, celerix::block_details (celerix::epoch::epoch_2, false, true, false)));
	for (auto i = 0; i < num_blocks; ++i)
	{
		auto const & key = keys[i];
		auto open = builder.make_block ()
					.account (key.pub)
					.previous (0)
					.balance (1)
					.representative (key.pub)
					.link (send_blocks[i]->hash ())
					.sign (key.prv, key.pub)
					.work (*system.work.generate (celerix::work_version::work_1, key.pub, receive_threshold))
					.build ();
		open_blocks.push_back (std::move (open));
	}
	std::cout << "Open blocks built, start processing" << std::endl;
	timer.restart ();
	process_all (open_blocks);
	std::cout << "Open blocks time: " << timer.stop ().count () << " " << timer.unit () << "\n\n";

	// These blocks are from each key to themselves
	std::vector<std::shared_ptr<celerix::state_block>> send_blocks2;
	for (auto i = 0; i < num_blocks; ++i)
	{
		auto const & key = keys[i];
		auto const & latest = open_blocks[i];
		auto send2 = builder.make_block ()
					 .account (key.pub)
					 .previous (latest->hash ())
					 .balance (0)
					 .representative (key.pub)
					 .link (key.pub)
					 .sign (key.prv, key.pub)
					 .work (*system.work.generate (celerix::work_version::work_1, latest->hash (), send_threshold))
					 .build ();
		send_blocks2.push_back (std::move (send2));
	}
	std::cout << "Send2 blocks built, start processing" << std::endl;
	timer.restart ();
	process_all (send_blocks2);
	std::cout << "Send2 blocks time: " << timer.stop ().count () << " " << timer.unit () << "\n\n";

	// Each key receives the previously sent blocks
	std::vector<std::shared_ptr<celerix::state_block>> receive_blocks;
	for (auto i = 0; i < num_blocks; ++i)
	{
		auto const & key = keys[i];
		auto const & latest = send_blocks2[i];
		auto send2 = builder.make_block ()
					 .account (key.pub)
					 .previous (latest->hash ())
					 .balance (1)
					 .representative (key.pub)
					 .link (latest->hash ())
					 .sign (key.prv, key.pub)
					 .work (*system.work.generate (celerix::work_version::work_1, latest->hash (), receive_threshold))
					 .build ();
		receive_blocks.push_back (std::move (send2));
	}
	std::cout << "Receive blocks built, start processing" << std::endl;
	timer.restart ();
	process_all (receive_blocks);
	std::cout << "Receive blocks time: " << timer.stop ().count () << " " << timer.unit () << "\n\n";
}

// Tests that local blocks are flooded to all principal representatives
// Sanitizers or running within valgrind use different timings and number of nodes
TEST (node, aggressive_flooding)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_bootstrap_bulk_pull_server = true;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	celerix::node_config node_config;
	node_config.bootstrap.enable = false;
	auto & node1 (*system.add_node (node_config, node_flags));
	auto & wallet1 (*system.wallet (0));
	wallet1.insert_adhoc (celerix::dev::genesis_key.prv);
	std::vector<std::pair<std::shared_ptr<celerix::node>, std::shared_ptr<celerix::wallet>>> nodes_wallets;
	nodes_wallets.resize (!celerix::memory_intensive_instrumentation () ? 5 : 3);

	std::generate (nodes_wallets.begin (), nodes_wallets.end (), [&system, node_flags] () {
		celerix::node_config node_config = system.default_config ();
		auto node (system.add_node (node_config, node_flags));
		return std::make_pair (node, system.wallet (system.nodes.size () - 1));
	});

	// This test is only valid if a non-aggressive flood would not reach every peer
	ASSERT_TIMELY_EQ (5s, node1.network.size (), nodes_wallets.size ());
	ASSERT_LT (node1.network.fanout (), nodes_wallets.size ());

	// Each new node should see genesis representative
	ASSERT_TIMELY (10s, std::all_of (nodes_wallets.begin (), nodes_wallets.end (), [] (auto const & node_wallet) { return node_wallet.first->rep_crawler.principal_representatives ().size () != 0; }));

	// Send a large amount to create a principal representative in each node
	auto large_amount = (celerix::dev::constants.genesis_amount / 2) / nodes_wallets.size ();
	std::vector<std::shared_ptr<celerix::block>> genesis_blocks;
	for (auto & node_wallet : nodes_wallets)
	{
		celerix::keypair keypair;
		node_wallet.second->store.representative_set (node_wallet.first->wallets.tx_begin_write (), keypair.pub);
		node_wallet.second->insert_adhoc (keypair.prv);
		auto block (wallet1.send_action (celerix::dev::genesis_key.pub, keypair.pub, large_amount));
		ASSERT_NE (nullptr, block);
		genesis_blocks.push_back (block);
	}

	// Ensure all nodes have the full genesis chain
	for (auto & node_wallet : nodes_wallets)
	{
		for (auto const & block : genesis_blocks)
		{
			auto process_result (node_wallet.first->process (block));
			ASSERT_TRUE (celerix::block_status::progress == process_result || celerix::block_status::old == process_result);
		}
		ASSERT_EQ (node1.latest (celerix::dev::genesis_key.pub), node_wallet.first->latest (celerix::dev::genesis_key.pub));
		ASSERT_EQ (genesis_blocks.back ()->hash (), node_wallet.first->latest (celerix::dev::genesis_key.pub));
		// Confirm blocks for rep crawler & receiving
		celerix::test::confirm (node_wallet.first->ledger, genesis_blocks.back ());
	}
	celerix::test::confirm (node1.ledger, genesis_blocks.back ());

	// Wait until all genesis blocks are received
	auto all_received = [&nodes_wallets] () {
		return std::all_of (nodes_wallets.begin (), nodes_wallets.end (), [] (auto const & node_wallet) {
			auto local_representative (node_wallet.second->store.representative (node_wallet.first->wallets.tx_begin_read ()));
			return node_wallet.first->ledger.any.account_balance (node_wallet.first->ledger.tx_begin_read (), local_representative) > 0;
		});
	};

	ASSERT_TIMELY (!celerix::slow_instrumentation () ? 10s : 40s, all_received ());

	ASSERT_TIMELY_EQ (!celerix::slow_instrumentation () ? 10s : 40s, node1.ledger.block_count (), 1 + 2 * nodes_wallets.size ());

	// Wait until the main node sees all representatives
	ASSERT_TIMELY_EQ (!celerix::slow_instrumentation () ? 10s : 40s, node1.rep_crawler.principal_representatives ().size (), nodes_wallets.size ());

	// Generate blocks and ensure they are sent to all representatives
	celerix::state_block_builder builder;
	std::shared_ptr<celerix::state_block> block{};
	{
		auto transaction = node1.ledger.tx_begin_read ();
		block = builder.make_block ()
				.account (celerix::dev::genesis_key.pub)
				.representative (celerix::dev::genesis_key.pub)
				.previous (node1.ledger.any.account_head (transaction, celerix::dev::genesis_key.pub))
				.balance (node1.ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub).value ().number () - 1)
				.link (celerix::dev::genesis_key.pub)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*node1.work_generate_blocking (node1.ledger.any.account_head (transaction, celerix::dev::genesis_key.pub)))
				.build ();
	}
	// Processing locally goes through the aggressive block flooding path
	ASSERT_EQ (celerix::block_status::progress, node1.process_local (block).value ());

	auto all_have_block = [&nodes_wallets] (celerix::block_hash const & hash_a) {
		return std::all_of (nodes_wallets.begin (), nodes_wallets.end (), [hash = hash_a] (auto const & node_wallet) {
			return node_wallet.first->block (hash) != nullptr;
		});
	};

	ASSERT_TIMELY (!celerix::slow_instrumentation () ? 5s : 25s, all_have_block (block->hash ()));

	// Do the same for a wallet block
	auto wallet_block = wallet1.send_sync (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.pub, 10);
	ASSERT_TIMELY (!celerix::slow_instrumentation () ? 5s : 25s, all_have_block (wallet_block));

	// All blocks: genesis + (send+open) for each representative + 2 local blocks
	// The main node only sees all blocks if other nodes are flooding their PR's open block to all other PRs
	ASSERT_EQ (1 + 2 * nodes_wallets.size () + 2, node1.ledger.block_count ());
}

TEST (node, send_single_many_peers)
{
	celerix::test::system system (celerix::memory_intensive_instrumentation () ? 4 : 10);
	celerix::keypair key2;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (celerix::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (3.5min, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<celerix::node> const & node_a) { return !node_a->balance (key2.pub).is_zero (); }));
	system.stop ();
	for (auto node : system.nodes)
	{
		ASSERT_TRUE (node->stopped);
	}
}
}

TEST (node, wallet_create_block_confirm_conflicts)
{
	for (int i = 0; i < 5; ++i)
	{
		celerix::test::system system;
		celerix::block_builder builder;
		celerix::node_config node_config (system.get_available_port ());
		node_config.backlog_scan.enable = false;
		auto node = system.add_node (node_config);
		auto const num_blocks = 10000;

		// First open the other account
		auto latest = celerix::dev::genesis->hash ();
		celerix::keypair key1;
		{
			auto transaction = node->ledger.tx_begin_write ();
			for (auto i = num_blocks - 1; i > 0; --i)
			{
				auto send = builder
							.send ()
							.previous (latest)
							.destination (key1.pub)
							.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio + i + 1)
							.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
							.work (*system.work.generate (latest))
							.build ();
				ASSERT_EQ (celerix::block_status::progress, node->ledger.process (transaction, send));
				latest = send->hash ();
			}
		}

		// Keep creating wallets. This is to check that there is no issues present when confirming blocks at the same time.
		std::atomic<bool> done{ false };
		std::thread t ([node, &done] () {
			while (!done)
			{
				node->wallets.create (celerix::random_wallet_id ());
			}
		});

		// Call block confirm on the top level send block which will confirm everything underneath on both accounts.
		{
			auto block = node->ledger.any.block_get (node->ledger.tx_begin_read (), latest);
			node->scheduler.manual.push (block);
			std::shared_ptr<celerix::election> election;
			ASSERT_TIMELY (10s, (election = node->active.election (block->qualified_root ())) != nullptr);
			election->force_confirm ();
		}

		ASSERT_TIMELY (120s, node->ledger.confirmed.block_exists_or_pruned (node->ledger.tx_begin_read (), latest) && node->confirming_set.size () == 0);
		done = true;
		t.join ();
	}
}

namespace celerix
{
/**
 * This test creates a small network of evenly weighted PRs and ensures a sequence of blocks from the genesis account to random accounts are able to be processed
 * Ongoing bootstrap is disabled to directly test election activation. A failure to activate a block on any PR will cause the test to stall
 */
TEST (system, block_sequence)
{
	size_t const block_count = 400;
	size_t const pr_count = 4;
	size_t const listeners_per_pr = 0;
	celerix::test::system system;
	std::deque<celerix::keypair> reps;
	for (auto i = 0; i < pr_count; ++i)
	{
		reps.push_back (celerix::keypair{});
	}
	system.ledger_initialization_set (reps, celerix::Kcelerix_ratio);
	system.deadline_set (3600s);
	celerix::node_config config;
	config.peering_port = system.get_available_port ();
	// config.bandwidth_limit = 16 * 1024;
	config.enable_voting = true;
	config.backlog_scan.enable = false;
	celerix::node_flags flags;
	flags.disable_max_peers_per_ip = true;
	flags.disable_ongoing_bootstrap = true;
	auto root = system.add_node (config, flags);
	auto wallet = root->wallets.items.begin ()->second;
	wallet->insert_adhoc (celerix::dev::genesis_key.prv);
	for (auto rep : reps)
	{
		system.wallet (0);
		config.peering_port = system.get_available_port ();
		auto pr = system.add_node (config, flags, celerix::transport::transport_type::tcp, rep);
		for (auto j = 0; j < listeners_per_pr; ++j)
		{
			config.peering_port = system.get_available_port ();
			system.add_node (config, flags);
		}
		std::cerr << rep.pub.to_account () << ' ' << pr->wallets.items.begin ()->second->exists (rep.pub) << pr->weight (rep.pub) << ' ' << '\n';
	}
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [] (std::shared_ptr<celerix::node> const & node) {
		// std::cerr << node->rep_crawler.representative_count () << ' ';
		return node->rep_crawler.representative_count () < 3;
	}))
	{
		system.poll ();
	}
	for (auto & node : system.nodes)
	{
		std::cerr << std::to_string (node->network.port) << ": ";
		auto prs = node->rep_crawler.principal_representatives ();
		for (auto pr : prs)
		{
			std::cerr << pr.account.to_account () << ' ';
		}
		std::cerr << '\n';
	}
	celerix::keypair key;
	auto start = std::chrono::system_clock::now ();
	std::deque<std::shared_ptr<celerix::block>> blocks;
	for (auto i = 0; i < block_count; ++i)
	{
		if ((i % 1000) == 0)
		{
			std::cerr << "Block: " << std::to_string (i) << " ms: " << std::to_string (std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now () - start).count ()) << "\n";
		}
		auto block = wallet->send_action (celerix::dev::genesis_key.pub, key.pub, 1);
		debug_assert (block != nullptr);
		blocks.push_back (block);
	}
	auto done = false;
	std::chrono::system_clock::time_point last;
	auto interval = 1000ms;
	while (!done)
	{
		if (std::chrono::system_clock::now () - last > interval)
		{
			std::string message;
			for (auto i : system.nodes)
			{
				message += boost::str (boost::format ("N:%1% b:%2% c:%3% a:%4% s:%5% p:%6%\n") % std::to_string (i->network.port) % std::to_string (i->ledger.block_count ()) % std::to_string (i->ledger.cemented_count ()) % std::to_string (i->active.size ()) % std::to_string (i->scheduler.priority.size ()) % std::to_string (i->network.size ()));
				celerix::lock_guard<celerix::mutex> lock{ i->active.mutex };
				for (auto const & j : i->active.roots)
				{
					auto election = j.election;
					if (election->confirmation_request_count > 10)
					{
						message += boost::str (boost::format ("\t r:%1% i:%2%\n") % j.root.to_string () % std::to_string (election->confirmation_request_count));
						for (auto const & k : election->votes ())
						{
							message += boost::str (boost::format ("\t\t r:%1% t:%2%\n") % k.first.to_account () % std::to_string (k.second.timestamp));
						}
					}
				}
			}
			std::cerr << message << std::endl;
			last = std::chrono::system_clock::now ();
		}
		done = std::all_of (system.nodes.begin (), system.nodes.end (), [&blocks] (std::shared_ptr<celerix::node> node) { return node->block_confirmed (blocks.back ()->hash ()); });
		system.poll ();
	}
}
} // namespace celerix
