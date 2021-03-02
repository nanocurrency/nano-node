#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (work_watcher, update)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config, node_flags);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev_genesis_key.prv);
	nano::keypair key;
	auto const block1 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100));
	auto difficulty1 (block1->difficulty ());
	auto multiplier1 (nano::normalized_multiplier (nano::difficulty::to_multiplier (difficulty1, nano::work_threshold (block1->work_version (), nano::block_details (nano::epoch::epoch_0, true, false, false))), node.network_params.network.publish_thresholds.epoch_1));
	auto const block2 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 200));
	auto difficulty2 (block2->difficulty ());
	auto multiplier2 (nano::normalized_multiplier (nano::difficulty::to_multiplier (difficulty2, nano::work_threshold (block2->work_version (), nano::block_details (nano::epoch::epoch_0, true, false, false))), node.network_params.network.publish_thresholds.epoch_1));
	double updated_multiplier1{ multiplier1 }, updated_multiplier2{ multiplier2 }, target_multiplier{ std::max (multiplier1, multiplier2) + 1e-6 };
	{
		nano::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.trended_active_multiplier = target_multiplier;
	}
	system.deadline_set (20s);
	while (updated_multiplier1 == multiplier1 || updated_multiplier2 == multiplier2)
	{
		{
			nano::lock_guard<std::mutex> guard (node.active.mutex);
			{
				auto const existing (node.active.roots.find (block1->qualified_root ()));
				//if existing is junk the block has been confirmed already
				ASSERT_NE (existing, node.active.roots.end ());
				updated_multiplier1 = existing->multiplier;
			}
			{
				auto const existing (node.active.roots.find (block2->qualified_root ()));
				//if existing is junk the block has been confirmed already
				ASSERT_NE (existing, node.active.roots.end ());
				updated_multiplier2 = existing->multiplier;
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GT (updated_multiplier1, multiplier1);
	ASSERT_GT (updated_multiplier2, multiplier2);
}

TEST (work_watcher, propagate)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config, node_flags);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev_genesis_key.prv);
	node_config.peering_port = nano::get_available_port ();
	auto & node_passive = *system.add_node (node_config);
	nano::keypair key;
	auto const block (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100));
	ASSERT_TIMELY (5s, node_passive.ledger.block_exists (block->hash ()));
	auto const multiplier (nano::normalized_multiplier (nano::difficulty::to_multiplier (block->difficulty (), nano::work_threshold (block->work_version (), nano::block_details (nano::epoch::epoch_0, false, false, false))), node.network_params.network.publish_thresholds.epoch_1));
	auto updated_multiplier{ multiplier };
	auto propagated_multiplier{ multiplier };
	{
		nano::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.trended_active_multiplier = multiplier * 1.001;
	}
	bool updated{ false };
	bool propagated{ false };
	system.deadline_set (10s);
	while (!(updated && propagated))
	{
		{
			nano::lock_guard<std::mutex> guard (node.active.mutex);
			{
				auto const existing (node.active.roots.find (block->qualified_root ()));
				ASSERT_NE (existing, node.active.roots.end ());
				updated_multiplier = existing->multiplier;
			}
		}
		{
			nano::lock_guard<std::mutex> guard (node_passive.active.mutex);
			{
				auto const existing (node_passive.active.roots.find (block->qualified_root ()));
				ASSERT_NE (existing, node_passive.active.roots.end ());
				propagated_multiplier = existing->multiplier;
			}
		}
		updated = updated_multiplier != multiplier;
		propagated = propagated_multiplier != multiplier;
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GT (updated_multiplier, multiplier);
	ASSERT_EQ (propagated_multiplier, updated_multiplier);
}

TEST (work_watcher, removed_after_win)
{
	nano::system system (1);
	auto & node (*system.nodes[0]);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev_genesis_key.prv);
	nano::keypair key;
	ASSERT_EQ (0, wallet.wallets.watcher->size ());
	auto const block1 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100));
	ASSERT_EQ (1, wallet.wallets.watcher->size ());
	ASSERT_TIMELY (5s, !node.wallets.watcher->is_watched (block1->qualified_root ()));
	ASSERT_EQ (0, node.wallets.watcher->size ());
}

TEST (work_watcher, removed_after_lose)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev_genesis_key.prv);
	nano::keypair key;
	auto const block1 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100));
	ASSERT_TRUE (node.wallets.watcher->is_watched (block1->qualified_root ()));
	nano::genesis genesis;
	auto fork1 (std::make_shared<nano::state_block> (nano::dev_genesis_key.pub, genesis.hash (), nano::dev_genesis_key.pub, nano::genesis_amount - nano::xrb_ratio, nano::dev_genesis_key.pub, nano::dev_genesis_key.prv, nano::dev_genesis_key.pub, *system.work.generate (genesis.hash ())));
	node.process_active (fork1);
	node.block_processor.flush ();
	auto vote (std::make_shared<nano::vote> (nano::dev_genesis_key.pub, nano::dev_genesis_key.prv, 0, fork1));
	nano::confirm_ack message (vote);
	node.network.process_message (message, std::make_shared<nano::transport::channel_loopback> (node));
	ASSERT_TIMELY (5s, !node.wallets.watcher->is_watched (block1->qualified_root ()));
	ASSERT_EQ (0, node.wallets.watcher->size ());
}

TEST (work_watcher, generation_disabled)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	node_config.work_threads = 0;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config);
	ASSERT_FALSE (node.work_generation_enabled ());
	nano::work_pool pool (std::numeric_limits<unsigned>::max ());
	nano::genesis genesis;
	nano::keypair key;
	auto block (std::make_shared<nano::state_block> (nano::dev_genesis_key.pub, genesis.hash (), nano::dev_genesis_key.pub, nano::genesis_amount - nano::Mxrb_ratio, key.pub, nano::dev_genesis_key.prv, nano::dev_genesis_key.pub, *pool.generate (genesis.hash ())));
	auto difficulty (block->difficulty ());
	node.wallets.watcher->add (block);
	ASSERT_FALSE (node.process_local (block).code != nano::process_result::progress);
	ASSERT_TRUE (node.wallets.watcher->is_watched (block->qualified_root ()));
	auto multiplier = nano::normalized_multiplier (nano::difficulty::to_multiplier (difficulty, nano::work_threshold (block->work_version (), nano::block_details (nano::epoch::epoch_0, true, false, false))), node.network_params.network.publish_thresholds.epoch_1);
	double updated_multiplier{ multiplier };
	{
		nano::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.trended_active_multiplier = multiplier * 10;
	}
	std::this_thread::sleep_for (2s);
	ASSERT_TRUE (node.wallets.watcher->is_watched (block->qualified_root ()));
	{
		nano::lock_guard<std::mutex> guard (node.active.mutex);
		auto const existing (node.active.roots.find (block->qualified_root ()));
		ASSERT_NE (existing, node.active.roots.end ());
		updated_multiplier = existing->multiplier;
	}
	ASSERT_EQ (updated_multiplier, multiplier);
	ASSERT_EQ (0, node.distributed_work.size ());
}

TEST (work_watcher, cancel)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	node_config.enable_voting = false;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev_genesis_key.prv, false);
	nano::keypair key;
	auto work1 (node.work_generate_blocking (nano::dev_genesis_key.pub));
	auto const block1 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100, *work1, false));
	{
		nano::unique_lock<std::mutex> lock (node.active.mutex);
		// Prevent active difficulty repopulating multipliers
		node.network_params.network.request_interval_ms = 10000;
		// Fill multipliers_cb and update active difficulty;
		for (auto i (0); i < node.active.multipliers_cb.size (); i++)
		{
			node.active.multipliers_cb.push_back (node.config.max_work_generate_multiplier);
		}
		node.active.update_active_multiplier (lock);
	}
	// Wait for work generation to start
	ASSERT_TIMELY (5s, 0 != node.work.size ());
	// Cancel the ongoing work
	ASSERT_EQ (1, node.work.size ());
	node.work.cancel (block1->root ());
	ASSERT_EQ (0, node.work.size ());
	{
		auto watched = wallet.wallets.watcher->list_watched ();
		auto existing = watched.find (block1->qualified_root ());
		ASSERT_NE (watched.end (), existing);
		auto block2 (existing->second);
		// Block must be the same
		ASSERT_NE (nullptr, block1);
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (*block1, *block2);
		// but should still be under watch
		ASSERT_TRUE (wallet.wallets.watcher->is_watched (block1->qualified_root ()));
	}
}

TEST (work_watcher, confirm_while_generating)
{
	// Ensure proper behavior when confirmation happens during work generation
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.work_threads = 1;
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	node_config.enable_voting = false;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev_genesis_key.prv, false);
	nano::keypair key;
	auto work1 (node.work_generate_blocking (nano::dev_genesis_key.pub));
	auto const block1 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100, *work1, false));
	{
		nano::unique_lock<std::mutex> lock (node.active.mutex);
		// Prevent active difficulty repopulating multipliers
		node.network_params.network.request_interval_ms = 10000;
		// Fill multipliers_cb and update active difficulty;
		for (auto i (0); i < node.active.multipliers_cb.size (); i++)
		{
			node.active.multipliers_cb.push_back (node.config.max_work_generate_multiplier);
		}
		node.active.update_active_multiplier (lock);
	}
	// Wait for work generation to start
	ASSERT_TIMELY (5s, 0 != node.work.size ());
	// Attach a callback to work cancellations
	std::atomic<bool> notified{ false };
	node.observers.work_cancel.add ([&notified, &block1](nano::root const & root_a) {
		EXPECT_EQ (root_a, block1->root ());
		notified = true;
	});
	// Confirm the block
	ASSERT_EQ (1, node.active.size ());
	auto election (node.active.election (block1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	// Verify post conditions
	ASSERT_NO_ERROR (system.poll_until_true (10s, [&node, &notified, &block1] {
		return node.block_confirmed (block1->hash ()) && node.work.size () == 0 && notified && !node.wallets.watcher->is_watched (block1->qualified_root ());
	}));
}

TEST (work_watcher, list_watched)
{
	nano::system system;
	nano::node_config config (nano::get_available_port (), system.logging);
	config.enable_voting = false;
	system.add_node (config);
	auto & wallet = *system.wallet (0);
	nano::keypair key;
	wallet.insert_adhoc (nano::dev_genesis_key.prv);
	ASSERT_TRUE (wallet.wallets.watcher->list_watched ().empty ());
	auto const block1 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100));
	auto watched1 = wallet.wallets.watcher->list_watched ();
	ASSERT_EQ (1, watched1.size ());
	ASSERT_NE (watched1.end (), watched1.find (block1->qualified_root ()));
	auto const block2 (wallet.send_action (nano::dev_genesis_key.pub, key.pub, 100));
	auto watched2 = wallet.wallets.watcher->list_watched ();
	ASSERT_EQ (2, watched2.size ());
	ASSERT_NE (watched2.end (), watched2.find (block1->qualified_root ()));
	ASSERT_NE (watched2.end (), watched2.find (block2->qualified_root ()));
	wallet.wallets.watcher->remove (*block1);
	auto watched3 = wallet.wallets.watcher->list_watched ();
	ASSERT_EQ (1, watched3.size ());
	ASSERT_NE (watched3.end (), watched3.find (block2->qualified_root ()));
	wallet.wallets.watcher->remove (*block2);
	auto watched4 = wallet.wallets.watcher->list_watched ();
	ASSERT_TRUE (watched4.empty ());
}
