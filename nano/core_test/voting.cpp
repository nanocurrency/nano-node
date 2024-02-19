#include <nano/node/common.hpp>
#include <nano/node/voting.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace nano
{
TEST (local_vote_history, basic)
{
	nano::local_vote_history history{ nano::dev::network_params.voting };
	ASSERT_FALSE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	ASSERT_TRUE (history.votes (1).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	auto vote1a (std::make_shared<nano::vote> ());
	ASSERT_EQ (0, history.size ());
	history.add (1, 2, vote1a);
	ASSERT_EQ (1, history.size ());
	ASSERT_TRUE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	auto votes1a (history.votes (1));
	ASSERT_FALSE (votes1a.empty ());
	ASSERT_EQ (1, history.votes (1, 2).size ());
	ASSERT_TRUE (history.votes (1, 1).empty ());
	ASSERT_TRUE (history.votes (1, 3).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	ASSERT_EQ (1, votes1a.size ());
	ASSERT_EQ (vote1a, votes1a[0]);
	auto vote1b (std::make_shared<nano::vote> ());
	history.add (1, 2, vote1b);
	auto votes1b (history.votes (1));
	ASSERT_EQ (1, votes1b.size ());
	ASSERT_EQ (vote1b, votes1b[0]);
	ASSERT_NE (vote1a, votes1b[0]);
	auto vote2 (std::make_shared<nano::vote> ());
	vote2->account.dwords[0]++;
	ASSERT_EQ (1, history.size ());
	history.add (1, 2, vote2);
	ASSERT_EQ (2, history.size ());
	auto votes2 (history.votes (1));
	ASSERT_EQ (2, votes2.size ());
	ASSERT_TRUE (vote1b == votes2[0] || vote1b == votes2[1]);
	ASSERT_TRUE (vote2 == votes2[0] || vote2 == votes2[1]);
	auto vote3 (std::make_shared<nano::vote> ());
	vote3->account.dwords[1]++;
	history.add (1, 3, vote3);
	ASSERT_EQ (1, history.size ());
	auto votes3 (history.votes (1));
	ASSERT_EQ (1, votes3.size ());
	ASSERT_EQ (vote3, votes3[0]);
}
}

TEST (vote_generator, cache)
{
	nano::test::system system (1);
	auto & node (*system.nodes[0]);
	auto epoch1 = system.upgrade_genesis_epoch (node, nano::epoch::epoch_1);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.generator.add (epoch1->root (), epoch1->hash ());
	ASSERT_TIMELY (1s, !node.history.votes (epoch1->root (), epoch1->hash ()).empty ());
	auto votes (node.history.votes (epoch1->root (), epoch1->hash ()));
	ASSERT_FALSE (votes.empty ());
	ASSERT_TRUE (std::any_of (votes[0]->hashes.begin (), votes[0]->hashes.end (), [hash = epoch1->hash ()] (nano::block_hash const & hash_a) { return hash_a == hash; }));
}

TEST (vote_generator, multiple_representatives)
{
	nano::test::system system (1);
	auto & node (*system.nodes[0]);
	nano::keypair key1, key2, key3;
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev::genesis_key.prv);
	wallet.insert_adhoc (key1.prv);
	wallet.insert_adhoc (key2.prv);
	wallet.insert_adhoc (key3.prv);
	auto const amount = 100 * nano::Gxrb_ratio;
	wallet.send_sync (nano::dev::genesis_key.pub, key1.pub, amount);
	wallet.send_sync (nano::dev::genesis_key.pub, key2.pub, amount);
	wallet.send_sync (nano::dev::genesis_key.pub, key3.pub, amount);
	ASSERT_TIMELY (3s, node.balance (key1.pub) == amount && node.balance (key2.pub) == amount && node.balance (key3.pub) == amount);
	wallet.change_sync (key1.pub, key1.pub);
	wallet.change_sync (key2.pub, key2.pub);
	wallet.change_sync (key3.pub, key3.pub);
	ASSERT_EQ (node.weight (key1.pub), amount);
	ASSERT_EQ (node.weight (key2.pub), amount);
	ASSERT_EQ (node.weight (key3.pub), amount);
	node.wallets.compute_reps ();
	ASSERT_EQ (4, node.wallets.reps ().voting);
	auto hash = wallet.send_sync (nano::dev::genesis_key.pub, nano::dev::genesis_key.pub, 1);
	auto send = node.block (hash);
	ASSERT_NE (nullptr, send);
	ASSERT_TIMELY_EQ (5s, node.history.votes (send->root (), send->hash ()).size (), 4);
	auto votes (node.history.votes (send->root (), send->hash ()));
	for (auto const & account : { key1.pub, key2.pub, key3.pub, nano::dev::genesis_key.pub })
	{
		auto existing (std::find_if (votes.begin (), votes.end (), [&account] (std::shared_ptr<nano::vote> const & vote_a) -> bool {
			return vote_a->account == account;
		}));
		ASSERT_NE (votes.end (), existing);
	}
}

TEST (vote_spacing, basic)
{
	nano::vote_spacing spacing{ std::chrono::milliseconds{ 100 } };
	nano::root root1{ 1 };
	nano::root root2{ 2 };
	nano::block_hash hash3{ 3 };
	nano::block_hash hash4{ 4 };
	nano::block_hash hash5{ 5 };
	ASSERT_EQ (0, spacing.size ());
	ASSERT_TRUE (spacing.votable (root1, hash3));
	spacing.flag (root1, hash3);
	ASSERT_EQ (1, spacing.size ());
	ASSERT_TRUE (spacing.votable (root1, hash3));
	ASSERT_FALSE (spacing.votable (root1, hash4));
	spacing.flag (root2, hash5);
	ASSERT_EQ (2, spacing.size ());
}

TEST (vote_spacing, prune)
{
	auto length = std::chrono::milliseconds{ 100 };
	nano::vote_spacing spacing{ length };
	nano::root root1{ 1 };
	nano::root root2{ 2 };
	nano::block_hash hash3{ 3 };
	nano::block_hash hash4{ 4 };
	spacing.flag (root1, hash3);
	ASSERT_EQ (1, spacing.size ());
	std::this_thread::sleep_for (length);
	spacing.flag (root2, hash4);
	ASSERT_EQ (1, spacing.size ());
}

TEST (vote_spacing, vote_generator)
{
	nano::node_config config;
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	config.active_elections_hinted_limit_percentage = 0; // Disable election hinting
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_search_pending = true;
	auto & node = *system.add_node (config, node_flags);
	auto & wallet = *system.wallet (0);
	wallet.insert_adhoc (nano::dev::genesis_key.prv);
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio - 1)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.store.tx_begin_write (), *send1));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts));
	node.generator.add (nano::dev::genesis->hash (), send1->hash ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts), 1);
	ASSERT_FALSE (node.ledger.rollback (node.store.tx_begin_write (), send1->hash ()));
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.store.tx_begin_write (), *send2));
	node.generator.add (nano::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_spacing), 1);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts));
	std::this_thread::sleep_for (config.network_params.voting.delay);
	node.generator.add (nano::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts), 2);
}

TEST (vote_spacing, rapid)
{
	nano::node_config config;
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	config.active_elections_hinted_limit_percentage = 0; // Disable election hinting
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_search_pending = true;
	auto & node = *system.add_node (config, node_flags);
	auto & wallet = *system.wallet (0);
	wallet.insert_adhoc (nano::dev::genesis_key.prv);
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio - 1)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.store.tx_begin_write (), *send1));
	node.generator.add (nano::dev::genesis->hash (), send1->hash ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts), 1);
	ASSERT_FALSE (node.ledger.rollback (node.store.tx_begin_write (), send1->hash ()));
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.store.tx_begin_write (), *send2));
	node.generator.add (nano::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_spacing), 1);
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts));
	std::this_thread::sleep_for (config.network_params.voting.delay);
	node.generator.add (nano::dev::genesis->hash (), send2->hash ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::generator_broadcasts), 2);
}
