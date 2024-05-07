#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

/*
 * Ensures all not confirmed accounts get activated by backlog scan periodically
 */
TEST (backlog, population)
{
	nano::mutex mutex;
	std::unordered_set<nano::account> activated;

	nano::test::system system{};
	auto & node = *system.add_node ();

	node.backlog.activate_callback.add ([&] (nano::secure::transaction const & transaction, nano::account const & account) {
		nano::lock_guard<nano::mutex> lock{ mutex };

		activated.insert (account);
	});

	auto blocks = nano::test::setup_independent_blocks (system, node, 256);

	// Checks if `activated` set contains all accounts we previously set up
	auto all_activated = [&] () {
		nano::lock_guard<nano::mutex> lock{ mutex };
		return std::all_of (blocks.begin (), blocks.end (), [&] (auto const & item) {
			return activated.count (item->account ()) != 0;
		});
	};
	ASSERT_TIMELY (5s, all_activated ());

	// Clear activated set to ensure we activate those accounts more than once
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		activated.clear ();
	}

	ASSERT_TIMELY (5s, all_activated ());
}
