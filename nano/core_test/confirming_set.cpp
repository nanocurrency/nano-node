#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <latch>

using namespace std::chrono_literals;

TEST (confirming_set, construction)
{
	auto ctx = nano::test::context::ledger_empty ();
	nano::write_database_queue write_queue{ false };
	nano::confirming_set confirming_set (ctx.ledger (), write_queue);
}

TEST (confirming_set, add_exists)
{
	auto ctx = nano::test::context::ledger_send_receive ();
	nano::write_database_queue write_queue{ false };
	nano::confirming_set confirming_set (ctx.ledger (), write_queue);
	auto send = ctx.blocks ()[0];
	confirming_set.add (send->hash ());
	ASSERT_TRUE (confirming_set.exists (send->hash ()));
}

TEST (confirming_set, process_one)
{
	auto ctx = nano::test::context::ledger_send_receive ();
	nano::write_database_queue write_queue{ false };
	nano::confirming_set confirming_set (ctx.ledger (), write_queue);
	std::atomic<int> count = 0;
	std::mutex mutex;
	std::condition_variable condition;
	confirming_set.cemented_observers.add ([&] (auto const &) { ++count; condition.notify_all (); });
	confirming_set.add (ctx.blocks ()[0]->hash ());
	nano::test::start_stop_guard guard{ confirming_set };
	std::unique_lock lock{ mutex };
	ASSERT_TRUE (condition.wait_for (lock, 5s, [&] () { return count == 1; }));
	ASSERT_EQ (1, ctx.stats ().count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	ASSERT_EQ (2, ctx.ledger ().cache.cemented_count);
}

TEST (confirming_set, process_multiple)
{
	auto ctx = nano::test::context::ledger_send_receive ();
	nano::write_database_queue write_queue{ false };
	nano::confirming_set confirming_set (ctx.ledger (), write_queue);
	std::atomic<int> count = 0;
	std::mutex mutex;
	std::condition_variable condition;
	confirming_set.cemented_observers.add ([&] (auto const &) { ++count; condition.notify_all (); });
	confirming_set.add (ctx.blocks ()[0]->hash ());
	confirming_set.add (ctx.blocks ()[1]->hash ());
	nano::test::start_stop_guard guard{ confirming_set };
	std::unique_lock lock{ mutex };
	ASSERT_TRUE (condition.wait_for (lock, 5s, [&] () { return count == 2; }));
	ASSERT_EQ (2, ctx.stats ().count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	ASSERT_EQ (3, ctx.ledger ().cache.cemented_count);
}
