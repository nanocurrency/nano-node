#include <boost/asio/spawn.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

TEST (coroutines, multithreaded_insert)
{
	size_t threads = 16;
	size_t inserts = 100;
	boost::asio::io_context ctx;
	std::vector<std::thread> consumers;
	auto guard = boost::asio::make_work_guard (ctx);
	for (auto i = 0; i < threads; ++i)
	{
		consumers.emplace_back ([&ctx] () {
			ctx.run ();
		});
	}
	std::vector<std::thread> producers;
	std::atomic<uint64_t> items = 0;
	std::atomic<uint64_t> runs = 0;
	for (auto i = 0; i < threads; ++i)
	{
		producers.emplace_back ([&ctx, &inserts, &items, &runs] () {
			for (auto i = 0; i < inserts; ++i)
			{
				++items;
				boost::asio::spawn (ctx, [&runs] (boost::asio::yield_context yield) {
					++runs;
				});
			}
		});
	}
	for (auto & i: producers)
	{
		i.join ();
	}
	guard.reset ();
	for (auto & i : consumers)
	{
		i.join ();
	}
	ASSERT_EQ (threads * inserts, items);
	ASSERT_EQ (threads * inserts, runs);
}
