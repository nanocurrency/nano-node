#include <nano/lib/blocks.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/block_context.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <future>

using namespace std::chrono_literals;

/*
 * Tests that stopping drops the notifications still queued, so the result promise of a processed block they carry breaks
 * instead of staying pending until the component is destroyed.
 */
TEST (ledger_notifications, stop_drops_queued)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Never started, so a queued notification stays queued until stop
	nano::ledger_notifications notifications{ node.config, node.stats, node.logger };

	// A processed block whose result is awaited, as it is for a blocking add to the block processor
	nano::block_context context{ nano::dev::genesis, nano::block_source::local };
	auto future = context.get_future ();
	nano::ledger_notifications::processed_batch_t processed;
	processed.emplace_back (nano::block_status::old, std::move (context));

	// Queued under a write transaction, the way the block processor queues its batches
	{
		auto transaction = node.ledger.tx_begin_write (nano::store::writer::testing);
		notifications.notify_processed (transaction, std::move (processed));
	}
	ASSERT_EQ (std::future_status::timeout, future.wait_for (0s));

	// Stopping drops the notification, which breaks the promise instead of leaving it pending
	notifications.stop ();
	ASSERT_EQ (std::future_status::ready, future.wait_for (0s));
	ASSERT_THROW (future.get (), std::future_error);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::ledger_notifications, nano::stat::detail::stopped));
}
