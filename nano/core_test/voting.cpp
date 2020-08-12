#include <nano/node/common.hpp>
#include <nano/node/testing.hpp>
#include <nano/node/voting.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace nano
{
TEST (local_vote_history, basic)
{
	nano::local_vote_history history;
	ASSERT_FALSE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	ASSERT_TRUE (history.votes (1).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	auto vote1 (std::make_shared<nano::vote> ());
	ASSERT_EQ (0, history.size ());
	history.add (1, 2, vote1);
	ASSERT_EQ (1, history.size ());
	ASSERT_TRUE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	auto votes1 (history.votes (1));
	ASSERT_FALSE (votes1.empty ());
	ASSERT_EQ (1, history.votes (1, 2).size ());
	ASSERT_TRUE (history.votes (1, 1).empty ());
	ASSERT_TRUE (history.votes (1, 3).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	ASSERT_EQ (1, votes1.size ());
	ASSERT_EQ (vote1, votes1[0]);
	auto vote2 (std::make_shared<nano::vote> ());
	ASSERT_EQ (1, history.size ());
	history.add (1, 2, vote2);
	ASSERT_EQ (2, history.size ());
	auto votes2 (history.votes (1));
	ASSERT_EQ (2, votes2.size ());
	ASSERT_TRUE (vote1 == votes2[0] || vote1 == votes2[1]);
	ASSERT_TRUE (vote2 == votes2[0] || vote2 == votes2[1]);
	auto vote3 (std::make_shared<nano::vote> ());
	history.add (1, 3, vote3);
	ASSERT_EQ (1, history.size ());
	auto votes3 (history.votes (1));
	ASSERT_EQ (1, votes3.size ());
	ASSERT_TRUE (vote3 == votes3[0]);
}
}

TEST (vote_generator, cache)
{
	nano::system system (1);
	auto & node (*system.nodes[0]);
	auto epoch1 = system.upgrade_genesis_epoch (node, nano::epoch::epoch_1);
	system.wallet (0)->insert_adhoc (nano::dev_genesis_key.prv);
	node.active.generator.add (epoch1->root (), epoch1->hash ());
	ASSERT_TIMELY (1s, !node.history.votes (epoch1->root (), epoch1->hash ()).empty ());
	auto votes (node.history.votes (epoch1->root (), epoch1->hash ()));
	ASSERT_FALSE (votes.empty ());
	ASSERT_TRUE (std::any_of (votes[0]->begin (), votes[0]->end (), [hash = epoch1->hash ()](nano::block_hash const & hash_a) { return hash_a == hash; }));
}

TEST (vote_generator, multiple_representatives)
{
	nano::system system (1);
	auto & node (*system.nodes[0]);
	nano::keypair key1, key2, key3;
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::dev_genesis_key.prv);
	wallet.insert_adhoc (key1.prv);
	wallet.insert_adhoc (key2.prv);
	wallet.insert_adhoc (key3.prv);
	auto const amount = 100 * nano::Gxrb_ratio;
	wallet.send_sync (nano::dev_genesis_key.pub, key1.pub, amount);
	wallet.send_sync (nano::dev_genesis_key.pub, key2.pub, amount);
	wallet.send_sync (nano::dev_genesis_key.pub, key3.pub, amount);
	ASSERT_TIMELY (3s, node.balance (key1.pub) == amount && node.balance (key2.pub) == amount && node.balance (key3.pub) == amount);
	wallet.change_sync (key1.pub, key1.pub);
	wallet.change_sync (key2.pub, key2.pub);
	wallet.change_sync (key3.pub, key3.pub);
	ASSERT_TRUE (node.weight (key1.pub) == amount && node.weight (key2.pub) == amount && node.weight (key3.pub) == amount);
	node.wallets.compute_reps ();
	ASSERT_EQ (4, node.wallets.reps ().voting);
	auto hash = wallet.send_sync (nano::dev_genesis_key.pub, nano::dev_genesis_key.pub, 1);
	auto send = node.block (hash);
	ASSERT_NE (nullptr, send);
	ASSERT_TIMELY (5s, node.history.votes (send->root (), send->hash ()).size () == 4);
	auto votes (node.history.votes (send->root (), send->hash ()));
	for (auto const & account : { key1.pub, key2.pub, key3.pub, nano::dev_genesis_key.pub })
	{
		auto existing (std::find_if (votes.begin (), votes.end (), [&account](std::shared_ptr<nano::vote> const & vote_a) -> bool {
			return vote_a->account == account;
		}));
		ASSERT_NE (votes.end (), existing);
	}
}

TEST (vote_generator, session)
{
	nano::system system (1);
	auto node (system.nodes[0]);
	system.wallet (0)->insert_adhoc (nano::dev_genesis_key.prv);
	nano::vote_generator_session generator_session (node->active.generator);
	boost::thread thread ([node, &generator_session]() {
		nano::thread_role::set (nano::thread_role::name::request_loop);
		for (unsigned i = 0; i < 100; ++i)
		{
			generator_session.add (nano::genesis_account, nano::genesis_hash);
		}
		ASSERT_EQ (0, node->stats.count (nano::stat::type::vote, nano::stat::detail::vote_indeterminate));
		generator_session.flush ();
	});
	thread.join ();
	ASSERT_TIMELY (5s, node->stats.count (nano::stat::type::vote, nano::stat::detail::vote_indeterminate) == (100 / nano::network::confirm_ack_hashes_max));
}
