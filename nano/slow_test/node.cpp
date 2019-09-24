#include <nano/core_test/testutil.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/testing.hpp>
#include <nano/node/transport/udp.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace std::chrono_literals;

TEST (system, generate_mass_activity)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false; // Prevent blocks cementing
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	uint32_t count (20);
	system.generate_mass_activity (count, *system.nodes[0]);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	for (auto i (system.nodes[0]->store.latest_begin (transaction)), n (system.nodes[0]->store.latest_end ()); i != n; ++i)
	{
	}
}

TEST (system, generate_mass_activity_long)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false; // Prevent blocks cementing
	auto node = system.add_node (node_config);
	system.wallet (0)->wallets.watcher->stop (); // Stop work watcher
	nano::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	uint32_t count (1000000000);
	system.generate_mass_activity (count, *system.nodes[0]);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	for (auto i (system.nodes[0]->store.latest_begin (transaction)), n (system.nodes[0]->store.latest_end ()); i != n; ++i)
	{
	}
	system.stop ();
	runner.join ();
}

TEST (system, receive_while_synchronizing)
{
	std::vector<boost::thread> threads;
	{
		nano::system system;
		nano::node_config node_config (24000, system.logging);
		node_config.enable_voting = false; // Prevent blocks cementing
		auto node = system.add_node (node_config);
		nano::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
		system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
		uint32_t count (1000);
		system.generate_mass_activity (count, *system.nodes[0]);
		nano::keypair key;
		auto node1 (std::make_shared<nano::node> (system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
		ASSERT_FALSE (node1->init_error ());
		auto channel (std::make_shared<nano::transport::channel_udp> (node1->network.udp_channels, system.nodes[0]->network.endpoint (), node1->network_params.protocol.protocol_version));
		node1->network.send_keepalive (channel);
		auto wallet (node1->wallets.create (1));
		wallet->insert_adhoc (nano::test_genesis_key.prv); // For voting
		ASSERT_EQ (key.pub, wallet->insert_adhoc (key.prv));
		node1->start ();
		system.nodes.push_back (node1);
		system.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (200), ([&system, &key]() {
			auto hash (system.wallet (0)->send_sync (nano::test_genesis_key.pub, key.pub, system.nodes[0]->config.receive_minimum.number ()));
			auto transaction (system.nodes[0]->store.tx_begin_read ());
			auto block (system.nodes[0]->store.block_get (transaction, hash));
			std::string block_text;
			block->serialize_json (block_text);
		}));
		while (node1->balance (key.pub).is_zero ())
		{
			system.poll ();
		}
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
	nano::logger_mt logger;
	auto store = nano::make_store (logger, nano::unique_path ());
	ASSERT_FALSE (store->init_error ());
	nano::stat stats;
	nano::ledger ledger (*store, stats);
	nano::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
	nano::work_pool pool (std::numeric_limits<unsigned>::max ());
	nano::keypair key;
	auto balance (nano::genesis_amount - 1);
	nano::send_block send (genesis.hash (), key.pub, balance, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, send).code);
	nano::open_block open (send.hash (), nano::test_genesis_key.pub, key.pub, key.prv, key.pub, *pool.generate (key.pub));
	ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, open).code);
	auto sprevious (send.hash ());
	auto rprevious (open.hash ());
	for (auto i (0), n (100000); i != n; ++i)
	{
		balance -= 1;
		nano::send_block send (sprevious, key.pub, balance, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *pool.generate (sprevious));
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, send).code);
		sprevious = send.hash ();
		nano::receive_block receive (rprevious, send.hash (), key.prv, key.pub, *pool.generate (rprevious));
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, receive).code);
		rprevious = receive.hash ();
		if (i % 100 == 0)
		{
			std::cerr << i << ' ';
		}
		ledger.account (transaction, sprevious);
		ledger.balance (transaction, rprevious);
	}
}

TEST (wallet, multithreaded_send_async)
{
	std::vector<boost::thread> threads;
	{
		nano::system system (24000, 1);
		nano::keypair key;
		auto wallet_l (system.wallet (0));
		wallet_l->insert_adhoc (nano::test_genesis_key.prv);
		wallet_l->insert_adhoc (key.prv);
		for (auto i (0); i < 20; ++i)
		{
			threads.push_back (boost::thread ([wallet_l, &key]() {
				for (auto i (0); i < 1000; ++i)
				{
					wallet_l->send_async (nano::test_genesis_key.pub, key.pub, 1000, [](std::shared_ptr<nano::block> block_a) {
						ASSERT_FALSE (block_a == nullptr);
						ASSERT_FALSE (block_a->hash ().is_zero ());
					});
				}
			}));
		}
		system.deadline_set (1000s);
		while (system.nodes[0]->balance (nano::test_genesis_key.pub) != (nano::genesis_amount - 20 * 1000 * 1000))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (store, load)
{
	nano::system system (24000, 1);
	std::vector<boost::thread> threads;
	for (auto i (0); i < 100; ++i)
	{
		threads.push_back (boost::thread ([&system]() {
			for (auto i (0); i != 1000; ++i)
			{
				auto transaction (system.nodes[0]->store.tx_begin_write ());
				for (auto j (0); j != 10; ++j)
				{
					nano::account account;
					nano::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
					system.nodes[0]->store.confirmation_height_put (transaction, account, 0);
					system.nodes[0]->store.account_put (transaction, account, nano::account_info ());
				}
			}
		}));
	}
	for (auto & i : threads)
	{
		i.join ();
	}
}

// ulimit -n increasing may be required
TEST (node, fork_storm)
{
	nano::system system (24000, 64);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto previous (system.nodes[0]->latest (nano::test_genesis_key.pub));
	auto balance (system.nodes[0]->balance (nano::test_genesis_key.pub));
	ASSERT_FALSE (previous.is_zero ());
	for (auto j (0); j != system.nodes.size (); ++j)
	{
		balance -= 1;
		nano::keypair key;
		nano::send_block send (previous, key.pub, balance, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0);
		system.nodes[j]->work_generate_blocking (send);
		previous = send.hash ();
		for (auto i (0); i != system.nodes.size (); ++i)
		{
			auto send_result (system.nodes[i]->process (send));
			ASSERT_EQ (nano::process_result::progress, send_result.code);
			nano::keypair rep;
			auto open (std::make_shared<nano::open_block> (previous, rep.pub, key.pub, key.prv, key.pub, 0));
			system.nodes[i]->work_generate_blocking (*open);
			auto open_result (system.nodes[i]->process (*open));
			ASSERT_EQ (nano::process_result::progress, open_result.code);
			auto transaction (system.nodes[i]->store.tx_begin_read ());
			system.nodes[i]->network.flood_block (open);
		}
	}
	auto again (true);

	int iteration (0);
	while (again)
	{
		auto empty = 0;
		auto single = 0;
		std::for_each (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<nano::node> const & node_a) {
			if (node_a->active.empty ())
			{
				++empty;
			}
			else
			{
				nano::lock_guard<std::mutex> lock (node_a->active.mutex);
				if (node_a->active.roots.begin ()->election->last_votes_size () == 1)
				{
					++single;
				}
			}
		});
		system.poll ();
		if ((iteration & 0xff) == 0)
		{
			std::cerr << "Empty: " << empty << " single: " << single << std::endl;
		}
		again = empty != 0 || single != 0;
		++iteration;
	}
	ASSERT_TRUE (true);
}

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
					assert (false);
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
						auto entry (nano::random_pool::generate_word32 (0, node_count - 1));
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
					assert (false);
					break;
			}
		}
	}
	auto count (heard_count (nodes));
	(void)count;
}

TEST (peer_container, random_set)
{
	nano::system system (24000, 1);
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
	nano::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto block (std::make_shared<nano::send_block> (0, 0, 0, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0));
	constexpr auto num_unchecked = 1000000;
	for (auto i (0); i < 1000000; ++i)
	{
		auto transaction (node.store.tx_begin_write ());
		node.store.unchecked_put (transaction, i, block);
	}
	auto transaction (node.store.tx_begin_read ());
	ASSERT_EQ (num_unchecked, node.store.unchecked_count (transaction));
}

TEST (store, vote_load)
{
	nano::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto block (std::make_shared<nano::send_block> (0, 0, 0, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0));
	for (auto i (0); i < 1000000; ++i)
	{
		auto vote (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, i, block));
		node.vote_processor.vote (vote, std::make_shared<nano::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint (), system.nodes[0]->network_params.protocol.protocol_version));
	}
}

TEST (wallets, rep_scan)
{
	nano::system system (24000, 1);
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
	node.wallets.foreach_representative ([](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
	});
	ASSERT_LT (std::chrono::steady_clock::now () - begin, std::chrono::milliseconds (5));
}

TEST (node, mass_vote_by_hash)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::genesis genesis;
	nano::block_hash previous (genesis.hash ());
	nano::keypair key;
	std::vector<std::shared_ptr<nano::state_block>> blocks;
	for (auto i (0); i < 10000; ++i)
	{
		auto block (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, nano::genesis_amount - (i + 1) * nano::Gxrb_ratio, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (previous)));
		previous = block->hash ();
		blocks.push_back (block);
	}
	for (auto i (blocks.begin ()), n (blocks.end ()); i != n; ++i)
	{
		system.nodes[0]->block_processor.add (*i, nano::seconds_since_epoch ());
	}
}

namespace nano
{
TEST (confirmation_height, many_accounts_single_confirmation)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.online_weight_minimum = 100;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);

	// As this test can take a while extend the next frontier check
	{
		nano::lock_guard<std::mutex> guard (node->active.mutex);
		node->active.next_frontier_check = std::chrono::steady_clock::now () + 7200s;
	}

	// The number of frontiers should be more than the batch_write_size to test the amount of blocks confirmed is correct.
	auto num_accounts = nano::confirmation_height_processor::batch_write_size * 2 + 50;
	nano::keypair last_keypair = nano::test_genesis_key;
	auto last_open_hash = node->latest (nano::test_genesis_key.pub);
	{
		auto transaction = node->store.tx_begin_write ();
		for (auto i = num_accounts - 1; i > 0; --i)
		{
			nano::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);

			nano::send_block send (last_open_hash, key.pub, node->config.online_weight_minimum.number (), last_keypair.prv, last_keypair.pub, *system.work.generate (last_open_hash));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send).code);
			nano::open_block open (send.hash (), last_keypair.pub, key.pub, key.prv, key.pub, *system.work.generate (key.pub));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, open).code);
			last_open_hash = open.hash ();
			last_keypair = key;
		}
	}

	// Call block confirm on the last open block which will confirm everything
	{
		auto transaction = node->store.tx_begin_read ();
		auto block = node->store.block_get (transaction, last_open_hash);
		node->block_confirm (block);
	}

	system.deadline_set (60s);
	while (true)
	{
		auto transaction = node->store.tx_begin_read ();
		if (node->ledger.block_confirmed (transaction, last_open_hash))
		{
			break;
		}

		ASSERT_NO_ERROR (system.poll ());
	}

	auto transaction (node->store.tx_begin_read ());
	// All frontiers (except last) should have 2 blocks and both should be confirmed
	for (auto i (node->store.latest_begin (transaction)), n (node->store.latest_end ()); i != n; ++i)
	{
		auto & account = i->first;
		auto & account_info = i->second;
		auto count = (account != last_keypair.pub) ? 2 : 1;
		uint64_t confirmation_height;
		ASSERT_FALSE (node->store.confirmation_height_get (transaction, account, confirmation_height));
		ASSERT_EQ (count, confirmation_height);
		ASSERT_EQ (count, account_info.block_count);
	}

	ASSERT_EQ (node->ledger.stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in), num_accounts * 2 - 2);
}

// Can take up to 10 minutes
TEST (confirmation_height, many_accounts_many_confirmations)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.online_weight_minimum = 100;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);

	// As this test can take a while extend the next frontier check
	{
		nano::lock_guard<std::mutex> guard (node->active.mutex);
		node->active.next_frontier_check = std::chrono::steady_clock::now () + 7200s;
	}

	auto num_accounts = 10000;
	auto latest_genesis = node->latest (nano::test_genesis_key.pub);
	std::vector<std::shared_ptr<nano::open_block>> open_blocks;
	{
		auto transaction = node->store.tx_begin_write ();
		for (auto i = num_accounts - 1; i > 0; --i)
		{
			nano::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);

			nano::send_block send (latest_genesis, key.pub, node->config.online_weight_minimum.number (), nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest_genesis));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send).code);
			auto open = std::make_shared<nano::open_block> (send.hash (), nano::test_genesis_key.pub, key.pub, key.prv, key.pub, *system.work.generate (key.pub));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open).code);
			open_blocks.push_back (std::move (open));
			latest_genesis = send.hash ();
		}
	}

	// Confirm all of the accounts
	for (auto & open_block : open_blocks)
	{
		node->block_confirm (open_block);
	}

	system.deadline_set (60s);
	while (node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in) != (num_accounts - 1) * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (confirmation_height, long_chains)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	nano::keypair key1;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::block_hash latest (node->latest (nano::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key1.prv);

	// As this test can take a while extend the next frontier check
	{
		nano::lock_guard<std::mutex> guard (node->active.mutex);
		node->active.next_frontier_check = std::chrono::steady_clock::now () + 7200s;
	}

	constexpr auto num_blocks = 10000;

	// First open the other account
	nano::send_block send (latest, key1.pub, nano::genesis_amount - nano::Gxrb_ratio + num_blocks + 1, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest));
	nano::open_block open (send.hash (), nano::genesis_account, key1.pub, key1.prv, key1.pub, *system.work.generate (key1.pub));
	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, open).code);
	}

	// Bulk send from genesis account to destination account
	auto previous_genesis_chain_hash = send.hash ();
	auto previous_destination_chain_hash = open.hash ();
	{
		auto transaction = node->store.tx_begin_write ();
		for (auto i = num_blocks - 1; i > 0; --i)
		{
			nano::send_block send (previous_genesis_chain_hash, key1.pub, nano::genesis_amount - nano::Gxrb_ratio + i + 1, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (previous_genesis_chain_hash));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send).code);
			nano::receive_block receive (previous_destination_chain_hash, send.hash (), key1.prv, key1.pub, *system.work.generate (previous_destination_chain_hash));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, receive).code);

			previous_genesis_chain_hash = send.hash ();
			previous_destination_chain_hash = receive.hash ();
		}
	}

	// Send one from destination to genesis and pocket it
	nano::send_block send1 (previous_destination_chain_hash, nano::test_genesis_key.pub, nano::Gxrb_ratio - 2, key1.prv, key1.pub, *system.work.generate (previous_destination_chain_hash));
	auto receive1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous_genesis_chain_hash, nano::genesis_account, nano::genesis_amount - nano::Gxrb_ratio + 1, send1.hash (), nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (previous_genesis_chain_hash)));

	// Unpocketed. Send to a non-existing account to prevent auto receives from the wallet adjusting expected confirmation height
	nano::keypair key2;
	nano::state_block send2 (nano::genesis_account, receive1->hash (), nano::genesis_account, nano::genesis_amount - nano::Gxrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (receive1->hash ()));

	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send1).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive1).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send2).code);
	}

	// Call block confirm on the existing receive block on the genesis account which will confirm everything underneath on both accounts
	node->block_confirm (receive1);

	system.deadline_set (10s);
	while (true)
	{
		auto transaction = node->store.tx_begin_read ();
		if (node->ledger.block_confirmed (transaction, receive1->hash ()))
		{
			break;
		}

		ASSERT_NO_ERROR (system.poll ());
	}

	auto transaction (node->store.tx_begin_read ());
	nano::account_info account_info;
	ASSERT_FALSE (node->store.account_get (transaction, nano::test_genesis_key.pub, account_info));
	uint64_t confirmation_height;
	ASSERT_FALSE (node->store.confirmation_height_get (transaction, nano::test_genesis_key.pub, confirmation_height));
	ASSERT_EQ (num_blocks + 2, confirmation_height);
	ASSERT_EQ (num_blocks + 3, account_info.block_count); // Includes the unpocketed send

	ASSERT_FALSE (node->store.account_get (transaction, key1.pub, account_info));
	ASSERT_FALSE (node->store.confirmation_height_get (transaction, key1.pub, confirmation_height));
	ASSERT_EQ (num_blocks + 1, confirmation_height);
	ASSERT_EQ (num_blocks + 1, account_info.block_count);

	ASSERT_EQ (node->ledger.stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in), num_blocks * 2 + 2);
}

// Can take up to 1 hour
TEST (confirmation_height, prioritize_frontiers_overwrite)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);

	// As this test can take a while extend the next frontier check
	{
		nano::lock_guard<std::mutex> guard (node->active.mutex);
		node->active.next_frontier_check = std::chrono::steady_clock::now () + 7200s;
	}

	auto num_accounts = node->active.max_priority_cementable_frontiers * 2;
	nano::keypair last_keypair = nano::test_genesis_key;
	auto last_open_hash = node->latest (nano::test_genesis_key.pub);
	// Clear confirmation height so that the genesis account has the same amount of uncemented blocks as the other frontiers
	{
		auto transaction = node->store.tx_begin_write ();
		node->store.confirmation_height_clear (transaction);
	}

	{
		auto transaction = node->store.tx_begin_write ();
		for (auto i = num_accounts - 1; i > 0; --i)
		{
			nano::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);

			nano::send_block send (last_open_hash, key.pub, nano::Gxrb_ratio - 1, last_keypair.prv, last_keypair.pub, *system.work.generate (last_open_hash));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send).code);
			nano::open_block open (send.hash (), last_keypair.pub, key.pub, key.prv, key.pub, *system.work.generate (key.pub));
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, open).code);
			last_open_hash = open.hash ();
			last_keypair = key;
		}
	}

	auto transaction = node->store.tx_begin_read ();
	{
		// Fill both priority frontier collections.
		node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (60), std::chrono::seconds (60));
		ASSERT_EQ (node->active.priority_cementable_frontiers_size () + node->active.priority_wallet_cementable_frontiers_size (), num_accounts);

		// Confirm the last frontier has the least number of uncemented blocks
		auto last_frontier_it = node->active.priority_cementable_frontiers.get<1> ().end ();
		--last_frontier_it;
		ASSERT_EQ (last_frontier_it->account, last_keypair.pub);
		ASSERT_EQ (last_frontier_it->blocks_uncemented, 1);
	}

	// Add a new frontier with 1 block, it should not be added to the frontier container because it is not higher than any already in the maxed out container
	nano::keypair key;
	auto latest_genesis = node->latest (nano::test_genesis_key.pub);
	nano::send_block send (latest_genesis, key.pub, nano::Gxrb_ratio - 1, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest_genesis));
	nano::open_block open (send.hash (), nano::test_genesis_key.pub, key.pub, key.prv, key.pub, *system.work.generate (key.pub));
	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, open).code);
	}
	transaction.refresh ();
	node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (60), std::chrono::seconds (60));
	ASSERT_EQ (node->active.priority_cementable_frontiers_size (), num_accounts / 2);
	ASSERT_EQ (node->active.priority_wallet_cementable_frontiers_size (), num_accounts / 2);

	// The account now has an extra block (2 in total) so has 1 more uncemented block than the next smallest frontier in the collection.
	nano::send_block send1 (send.hash (), key.pub, nano::Gxrb_ratio - 2, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send.hash ()));
	nano::receive_block receive (open.hash (), send1.hash (), key.prv, key.pub, *system.work.generate (open.hash ()));
	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send1).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, receive).code);
	}

	// Confirm that it gets replaced
	transaction.refresh ();
	node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (60), std::chrono::seconds (60));
	ASSERT_EQ (node->active.priority_cementable_frontiers_size (), num_accounts / 2);
	ASSERT_EQ (node->active.priority_wallet_cementable_frontiers_size (), num_accounts / 2);
	ASSERT_EQ (node->active.priority_cementable_frontiers.find (last_keypair.pub), node->active.priority_cementable_frontiers.end ());
	ASSERT_NE (node->active.priority_cementable_frontiers.find (key.pub), node->active.priority_cementable_frontiers.end ());

	// Check there are no matching accounts found in both containers
	for (auto it = node->active.priority_cementable_frontiers.begin (); it != node->active.priority_cementable_frontiers.end (); ++it)
	{
		ASSERT_EQ (node->active.priority_wallet_cementable_frontiers.find (it->account), node->active.priority_wallet_cementable_frontiers.end ());
	}
}
}
