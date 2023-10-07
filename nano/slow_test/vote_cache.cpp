#include <nano/test_common/rate_observer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <thread>

using namespace std::chrono_literals;

namespace
{
nano::keypair setup_rep (nano::test::system & system, nano::node & node, nano::uint128_t amount)
{
	auto latest = node.latest (nano::dev::genesis_key.pub);
	auto balance = node.balance (nano::dev::genesis_key.pub);

	nano::keypair key;
	nano::block_builder builder;

	auto send = builder
				.send ()
				.previous (latest)
				.destination (key.pub)
				.balance (balance - amount)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build_shared ();

	auto open = builder
				.open ()
				.source (send->hash ())
				.representative (key.pub)
				.account (key.pub)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();

	EXPECT_TRUE (nano::test::process (node, { send, open }));
	EXPECT_TRUE (nano::test::start_elections (system, node, { send, open }, true));
	EXPECT_TIMELY (5s, nano::test::confirmed (node, { send, open }));

	return key;
}

std::vector<nano::keypair> setup_reps (nano::test::system & system, nano::node & node, int count)
{
	const nano::uint128_t weight = nano::Gxrb_ratio * 1000;
	std::vector<nano::keypair> reps;
	for (int n = 0; n < count; ++n)
	{
		reps.push_back (setup_rep (system, node, weight));
	}
	return reps;
}

/*
 * Creates `count` number of unconfirmed blocks with their dependencies confirmed, each directly sent from genesis
 */
std::vector<std::shared_ptr<nano::block>> setup_blocks (nano::test::system & system, nano::node & node, int count)
{
	auto latest = node.latest (nano::dev::genesis_key.pub);
	auto balance = node.balance (nano::dev::genesis_key.pub);

	std::vector<std::shared_ptr<nano::block>> sends;
	std::vector<std::shared_ptr<nano::block>> receives;
	for (int n = 0; n < count; ++n)
	{
		if (n % 10000 == 0)
			std::cout << "setup_blocks: " << n << std::endl;

		nano::keypair key;
		nano::block_builder builder;

		balance -= 1;
		auto send = builder
					.send ()
					.previous (latest)
					.destination (key.pub)
					.balance (balance)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();

		auto open = builder
					.open ()
					.source (send->hash ())
					.representative (key.pub)
					.account (key.pub)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build_shared ();

		latest = send->hash ();

		sends.push_back (send);
		receives.push_back (open);
	}

	std::cout << "setup_blocks confirming" << std::endl;

	EXPECT_TRUE (nano::test::process (node, sends));
	EXPECT_TRUE (nano::test::process (node, receives));

	// Confirm whole genesis chain at once
	EXPECT_TRUE (nano::test::start_elections (system, node, { sends.back () }, true));
	EXPECT_TIMELY (5s, nano::test::confirmed (node, { sends }));

	std::cout << "setup_blocks done" << std::endl;

	return receives;
}

void run_parallel (int thread_count, std::function<void (int)> func)
{
	std::vector<std::thread> threads;
	for (int n = 0; n < thread_count; ++n)
	{
		threads.emplace_back ([func, n] () {
			func (n);
		});
	}
	for (auto & thread : threads)
	{
		thread.join ();
	}
}
}

TEST (vote_cache, perf_singlethreaded)
{
	nano::test::system system;
	nano::node_flags flags;
	nano::node_config config = system.default_config ();
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config, flags);

	const int rep_count = 50;
	const int block_count = 1024 * 128 * 2; // 2x the inactive vote cache size
	const int vote_count = 100000;
	const int single_vote_size = 7;
	const int single_vote_reps = 7;

	auto reps = setup_reps (system, node, rep_count);
	auto blocks = setup_blocks (system, node, block_count);

	std::cout << "preparation done" << std::endl;

	// Start monitoring rate of blocks processed by vote cache
	nano::test::rate_observer rate;
	rate.observe (node, nano::stat::type::vote_cache, nano::stat::detail::vote_processed, nano::stat::dir::in);
	rate.background_print (3s);

	// Ensure votes are not inserted into active elections
	node.active.clear ();

	int block_idx = 0;
	int rep_idx = 0;
	std::vector<nano::block_hash> hashes;
	for (int n = 0; n < vote_count; ++n)
	{
		// Fill block hashes for this vote
		hashes.clear ();
		for (int i = 0; i < single_vote_size; ++i)
		{
			block_idx = (block_idx + 1151) % blocks.size ();
			hashes.push_back (blocks[block_idx]->hash ());
		}

		for (int i = 0; i < single_vote_reps; ++i)
		{
			rep_idx = (rep_idx + 13) % reps.size ();
			auto vote = nano::test::make_vote (reps[rep_idx], hashes);

			// Process the vote
			node.active.vote (vote);
		}
	}

	std::cout << "total votes processed: " << node.stats.count (nano::stat::type::vote_cache, nano::stat::detail::vote_processed, nano::stat::dir::in) << std::endl;

	// Ensure we processed all the votes
	ASSERT_EQ (node.stats.count (nano::stat::type::vote_cache, nano::stat::detail::vote_processed, nano::stat::dir::in), vote_count * single_vote_size * single_vote_reps);

	// Ensure vote cache size is at max capacity
	ASSERT_EQ (node.vote_cache.size (), config.vote_cache.max_size);
}

TEST (vote_cache, perf_multithreaded)
{
	nano::test::system system;
	nano::node_flags flags;
	nano::node_config config = system.default_config ();
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config, flags);

	const int thread_count = 12;
	const int rep_count = 50;
	const int block_count = 1024 * 128 * 2; // 2x the inactive vote cache size
	const int vote_count = 200000 / thread_count;
	const int single_vote_size = 7;
	const int single_vote_reps = 7;

	auto reps = setup_reps (system, node, rep_count);
	auto blocks = setup_blocks (system, node, block_count);

	std::cout << "preparation done" << std::endl;

	// Start monitoring rate of blocks processed by vote cache
	nano::test::rate_observer rate;
	rate.observe (node, nano::stat::type::vote_cache, nano::stat::detail::vote_processed, nano::stat::dir::in);
	rate.background_print (3s);

	// Ensure our generated votes go to inactive vote cache instead of active elections
	node.active.clear ();

	run_parallel (thread_count, [&node, &reps, &blocks, &vote_count, &single_vote_size, &single_vote_reps] (int index) {
		int block_idx = index;
		int rep_idx = index;
		std::vector<nano::block_hash> hashes;

		// Each iteration generates vote with `single_vote_size` hashes in it
		// and that vote is then independently signed by `single_vote_reps` representatives
		// So total votes per thread is `vote_count` * `single_vote_reps`
		for (int n = 0; n < vote_count; ++n)
		{
			// Fill block hashes for this vote
			hashes.clear ();
			for (int i = 0; i < single_vote_size; ++i)
			{
				block_idx = (block_idx + 1151) % blocks.size ();
				hashes.push_back (blocks[block_idx]->hash ());
			}

			for (int i = 0; i < single_vote_reps; ++i)
			{
				rep_idx = (rep_idx + 13) % reps.size ();
				auto vote = nano::test::make_vote (reps[rep_idx], hashes);

				// Process the vote
				node.active.vote (vote);
			}
		}
	});

	std::cout << "total votes processed: " << node.stats.count (nano::stat::type::vote_cache, nano::stat::detail::vote_processed, nano::stat::dir::in) << std::endl;

	// Ensure vote cache size is at max capacity
	ASSERT_EQ (node.vote_cache.size (), config.vote_cache.max_size);
}
