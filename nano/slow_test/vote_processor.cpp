#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using namespace std::chrono_literals;

// Tests the normal behavior is more votes getting into the vote_processor than getting processed,
// so the producer always wins. Also exercises the flush operation, so it must never deadlock.
TEST (vote_processor, producer_consumer)
{
	nano::system system (1);
	auto & node (*system.nodes[0]);
	auto channel (std::make_shared<nano::transport::inproc::channel> (node, node));

	unsigned number_of_producers{ 40 }; // Enough to overwhelm any vote processing threads
	unsigned number_of_votes{ 25'000 };
	unsigned consumer_wins{ 0 };
	unsigned producer_wins{ 0 };

	auto producer = [&node, &channel, &number_of_votes] () -> void {
		for (unsigned i = 0; i < number_of_votes; ++i)
		{
			auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::vote::timestamp_min * (1 + i), 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
			node.vote_processor.vote (vote, channel);
		}
	};

	auto consumer = [&node, &number_of_votes] () -> void {
		while (node.vote_processor.total_processed.load () < number_of_votes)
		{
			if (node.vote_processor.size () >= number_of_votes / 100)
			{
				node.vote_processor.flush ();
			}
		}
	};

	auto monitor = [&node, &number_of_votes, &producer_wins, &consumer_wins] () -> void {
		while (node.vote_processor.total_processed.load () < number_of_votes)
		{
			std::this_thread::sleep_for (std::chrono::milliseconds (50));
			if (node.vote_processor.empty ())
			{
				++consumer_wins;
			}
			else
			{
				++producer_wins;
			}
		}
	};

	// Run multiple producers in parallel
	std::vector<std::thread> producers;
	for (int n = 0; n < number_of_producers; ++n)
	{
		producers.emplace_back (producer);
	}

	std::thread consumer_thread{ consumer };
	std::thread monitor_thread{ monitor };

	ASSERT_TIMELY (30s, node.vote_processor.total_processed.load () >= number_of_votes);

	for (auto & producer : producers)
	{
		producer.join ();
	}
	consumer_thread.join ();
	monitor_thread.join ();

	ASSERT_TRUE (producer_wins > consumer_wins);
}
