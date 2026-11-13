#include <nano/lib/numbers.hpp>
#include <nano/node/bootstrap/ondemand_index.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
using index_t = nano::bootstrap::ondemand_index;

struct test_context
{
	nano::ondemand_config config;
	nano::bootstrap::ondemand_index index;

	test_context () :
		index{ config }
	{
		config.max_blocks = 16;
		config.delay = 500ms;
		config.request_timeout = 2s;
		config.max_attempts = 4;
		config.retry_cooldown = 250ms;
		config.max_age = 5min;
	}
};

nano::block_hash make_hash (uint64_t value)
{
	return nano::block_hash{ value };
}

nano::account make_peer (uint64_t value)
{
	return nano::account{ value };
}
}

/*
 * A wanted block is not requested until its delay has passed, so that a copy still on its way can arrive first.
 */
TEST (bootstrap_ondemand_index, insert_waits_for_delay)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto const now = std::chrono::steady_clock::now ();

	// A new block is tracked right away
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_TRUE (ctx.index.contains (hash));
	ASSERT_EQ (ctx.index.size (), 1);

	// It is not offered for a request before the delay is over
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now));
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + ctx.config.delay - 1ms));

	// Once the delay is over it is offered, with no peer to avoid yet
	auto const request = ctx.index.next (index_t::reason::dependency, now + ctx.config.delay);
	ASSERT_TRUE (request);
	ASSERT_EQ (request->hash, hash);
	ASSERT_TRUE (request->exclude.empty ());
}

/*
 * Wanting a tracked block again changes nothing: it stays a single entry and keeps its original schedule.
 */
TEST (bootstrap_ondemand_index, insert_duplicate_keeps_schedule)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto const now = std::chrono::steady_clock::now ();

	// The same block is wanted twice, the second time well into its delay
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now + 400ms), index_t::insert_result::duplicate);
	ASSERT_EQ (ctx.index.size (), 1);

	// The delay still counts from the first time
	ASSERT_TRUE (ctx.index.next (index_t::reason::dependency, now + ctx.config.delay));
}

/*
 * Blocks backed by votes are requested before blocks that are only a dependency of a live block, whatever their age.
 */
TEST (bootstrap_ondemand_index, next_prefers_stronger_reason)
{
	test_context ctx;
	auto const dependency = make_hash (1);
	auto const voted = make_hash (2);
	auto const now = std::chrono::steady_clock::now ();

	// The dependency becomes wanted first, the voted block a little later
	ASSERT_EQ (ctx.index.insert (dependency, index_t::reason::dependency, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (voted, index_t::reason::votes, now + 10ms), index_t::insert_result::inserted);

	// While only the dependency is due, it is the one offered
	auto const first = ctx.index.next (index_t::reason::dependency, now + ctx.config.delay);
	ASSERT_TRUE (first);
	ASSERT_EQ (first->hash, dependency);

	// Once both are due the voted block goes first
	auto const due = now + ctx.config.delay + 10ms;
	auto const second = ctx.index.next (index_t::reason::dependency, due);
	ASSERT_TRUE (second);
	ASSERT_EQ (second->hash, voted);

	// With the voted block requested the dependency is next
	ASSERT_TRUE (ctx.index.dispatch (voted, /* id */ 1, make_peer (1), due));
	auto const third = ctx.index.next (index_t::reason::dependency, due);
	ASSERT_TRUE (third);
	ASSERT_EQ (third->hash, dependency);
}

/*
 * Among blocks wanted for the same reason the one that has been due for longest is requested first.
 */
TEST (bootstrap_ondemand_index, next_orders_by_due_time)
{
	test_context ctx;
	auto const older = make_hash (2);
	auto const newer = make_hash (1);
	auto const now = std::chrono::steady_clock::now ();

	// Two voted blocks, inserted out of hash order to rule the hash out as the tie breaker
	ASSERT_EQ (ctx.index.insert (older, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (newer, index_t::reason::votes, now + 1ms), index_t::insert_result::inserted);

	// The one wanted first is offered first
	auto const due = now + ctx.config.delay + 1ms;
	auto const first = ctx.index.next (index_t::reason::dependency, due);
	ASSERT_TRUE (first);
	ASSERT_EQ (first->hash, older);

	// And the other one right after it
	ASSERT_TRUE (ctx.index.dispatch (older, /* id */ 1, make_peer (1), due));
	auto const second = ctx.index.next (index_t::reason::dependency, due);
	ASSERT_TRUE (second);
	ASSERT_EQ (second->hash, newer);
}

/*
 * The caller can leave the weaker reasons out, which is how blocks wanted as a dependency are held to a budget of their own
 * without holding back the blocks that representatives voted for.
 */
TEST (bootstrap_ondemand_index, next_skips_weaker_reasons)
{
	test_context ctx;
	auto const dependency = make_hash (1);
	auto const voted = make_hash (2);
	auto const now = std::chrono::steady_clock::now ();
	auto const due = now + ctx.config.delay;

	// One block of each kind, both due
	ASSERT_EQ (ctx.index.insert (dependency, index_t::reason::dependency, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (voted, index_t::reason::votes, now), index_t::insert_result::inserted);

	// Asked for voted blocks only, the voted block is offered, and with its reason
	auto const first = ctx.index.next (index_t::reason::votes, due);
	ASSERT_TRUE (first);
	ASSERT_EQ (first->hash, voted);
	ASSERT_EQ (first->reason, index_t::reason::votes);

	// With the voted block requested nothing is left for that question, while the dependency is still there for the wider one
	ASSERT_TRUE (ctx.index.dispatch (voted, /* id */ 1, make_peer (1), due));
	ASSERT_FALSE (ctx.index.next (index_t::reason::votes, due));
	auto const second = ctx.index.next (index_t::reason::dependency, due);
	ASSERT_TRUE (second);
	ASSERT_EQ (second->hash, dependency);
	ASSERT_EQ (second->reason, index_t::reason::dependency);
}

/*
 * A tracked dependency that gathers votes is treated as a voted block from then on.
 */
TEST (bootstrap_ondemand_index, insert_upgrades_reason)
{
	test_context ctx;
	auto const first = make_hash (1);
	auto const second = make_hash (2);
	auto const now = std::chrono::steady_clock::now ();

	// Two dependencies, the second one wanted later and therefore behind the first
	ASSERT_EQ (ctx.index.insert (first, index_t::reason::dependency, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (second, index_t::reason::dependency, now + 1ms), index_t::insert_result::inserted);

	// Votes arrive for the second one
	ASSERT_EQ (ctx.index.find_reason (second), index_t::reason::dependency);
	ASSERT_EQ (ctx.index.insert (second, index_t::reason::votes, now + 2ms), index_t::insert_result::duplicate);
	ASSERT_EQ (ctx.index.find_reason (second), index_t::reason::votes);

	// It now goes ahead of the older dependency
	auto const request = ctx.index.next (index_t::reason::dependency, now + ctx.config.delay + 1ms);
	ASSERT_TRUE (request);
	ASSERT_EQ (request->hash, second);

	// A weaker reason never downgrades it
	ASSERT_EQ (ctx.index.insert (second, index_t::reason::dependency, now + 3ms), index_t::insert_result::duplicate);
	ASSERT_EQ (ctx.index.find_reason (second), index_t::reason::votes);
	ASSERT_FALSE (ctx.index.find_reason (make_hash (3)));
	auto const again = ctx.index.next (index_t::reason::dependency, now + ctx.config.delay + 1ms);
	ASSERT_TRUE (again);
	ASSERT_EQ (again->hash, second);
}

/*
 * A full index makes room by dropping the oldest block with the weakest reason, and refuses a newcomer weaker than everything it tracks.
 * This is what keeps dependencies, which anyone can fabricate, from pushing out blocks that representatives voted for.
 */
TEST (bootstrap_ondemand_index, insert_replaces_oldest_weakest)
{
	test_context ctx;
	ctx.config.max_blocks = 2;
	auto const now = std::chrono::steady_clock::now ();

	// Fill the index with one dependency and one voted block
	ASSERT_EQ (ctx.index.insert (make_hash (1), index_t::reason::dependency, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (make_hash (2), index_t::reason::votes, now + 1ms), index_t::insert_result::inserted);

	// Another voted block takes the place of the dependency
	ASSERT_EQ (ctx.index.insert (make_hash (3), index_t::reason::votes, now + 2ms), index_t::insert_result::replaced);
	ASSERT_FALSE (ctx.index.contains (make_hash (1)));
	ASSERT_TRUE (ctx.index.contains (make_hash (2)));
	ASSERT_TRUE (ctx.index.contains (make_hash (3)));

	// A dependency cannot replace a voted block
	ASSERT_EQ (ctx.index.insert (make_hash (4), index_t::reason::dependency, now + 3ms), index_t::insert_result::overfill);
	ASSERT_FALSE (ctx.index.contains (make_hash (4)));

	// A voted block replaces the oldest voted block
	ASSERT_EQ (ctx.index.insert (make_hash (5), index_t::reason::votes, now + 4ms), index_t::insert_result::replaced);
	ASSERT_FALSE (ctx.index.contains (make_hash (2)));
	ASSERT_TRUE (ctx.index.contains (make_hash (3)));
	ASSERT_TRUE (ctx.index.contains (make_hash (5)));
	ASSERT_EQ (ctx.index.size (), 2);
}

/*
 * An index capped at zero blocks tracks nothing.
 */
TEST (bootstrap_ondemand_index, insert_refused_when_closed)
{
	test_context ctx;
	ctx.config.max_blocks = 0;
	auto const now = std::chrono::steady_clock::now ();

	// Even the strongest reason is refused
	ASSERT_EQ (ctx.index.insert (make_hash (1), index_t::reason::votes, now), index_t::insert_result::overfill);
	ASSERT_EQ (ctx.index.size (), 0);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + ctx.config.delay));
}

/*
 * A block is requested from one peer at a time: once a request is out it is not offered again until that request concludes.
 */
TEST (bootstrap_ondemand_index, dispatch_takes_block_out_of_line)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto const now = std::chrono::steady_clock::now ();
	auto const due = now + ctx.config.delay;

	// A request for an unknown block is not recorded
	ASSERT_FALSE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), due));

	// A waiting block takes the request
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), due));

	// While the request is out the block is neither offered nor does it take a second request
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, due));
	ASSERT_FALSE (ctx.index.dispatch (hash, /* id */ 2, make_peer (2), due));
}

/*
 * A failed request sends the block back in line behind a cooldown that doubles with every failure,
 * and every peer that failed is excluded from the following requests.
 */
TEST (bootstrap_ondemand_index, fail_retries_with_doubling_cooldown)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// First request fails: the block waits one cooldown and the peer is remembered
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now), index_t::fail_result::retry);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 249ms));
	now += 250ms;
	auto const second = ctx.index.next (index_t::reason::dependency, now);
	ASSERT_TRUE (second);
	ASSERT_EQ (second->exclude.size (), 1);
	ASSERT_EQ (second->exclude[0], make_peer (1));

	// Second request fails: the cooldown is twice as long and both peers are remembered
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 2, make_peer (2), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 2, now), index_t::fail_result::retry);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 499ms));
	now += 500ms;
	auto const third = ctx.index.next (index_t::reason::dependency, now);
	ASSERT_TRUE (third);
	ASSERT_EQ (third->exclude.size (), 2);
	ASSERT_EQ (third->exclude[0], make_peer (1));
	ASSERT_EQ (third->exclude[1], make_peer (2));

	// Third request fails: the cooldown doubles once more
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 3, make_peer (3), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 3, now), index_t::fail_result::retry);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 999ms));
	ASSERT_TRUE (ctx.index.next (index_t::reason::dependency, now + 1000ms));
}

/*
 * After the configured number of requests the block is abandoned: it stays tracked, so that wanting it again
 * does not start another round, but it is never offered for a request.
 */
TEST (bootstrap_ondemand_index, fail_abandons_after_max_attempts)
{
	test_context ctx;
	ctx.config.max_attempts = 2;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// The first failure leaves one more attempt
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now), index_t::fail_result::retry);
	now += ctx.config.retry_cooldown;

	// The second failure uses up the attempts
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 2, make_peer (2), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 2, now), index_t::fail_result::abandoned);

	// The block is never offered again, however long it waits
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 1min));
	ASSERT_FALSE (ctx.index.dispatch (hash, /* id */ 3, make_peer (3), now + 1min));

	// Wanting it again does not revive it
	ASSERT_TRUE (ctx.index.contains (hash));
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now + 1min), index_t::insert_result::duplicate);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 2min));
}

/*
 * A block abandoned while only a live block spoke for it starts over once representatives vote for it.
 * Peers that lacked the block at first may hold it by then, and the author of a block must not be able to
 * make nodes give up on it ahead of time. The same reason a second time revives nothing.
 */
TEST (bootstrap_ondemand_index, insert_revives_abandoned_block)
{
	test_context ctx;
	ctx.config.max_attempts = 2;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::dependency, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// Both attempts fail and the dependency is abandoned
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now), index_t::fail_result::retry);
	now += ctx.config.retry_cooldown;
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 2, make_peer (2), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 2, now), index_t::fail_result::abandoned);

	// Being wanted as a dependency again changes nothing
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::dependency, now), index_t::insert_result::duplicate);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 1min));

	// Votes revive it: it waits out the delay again and is then offered with no peer excluded
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::revived);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + ctx.config.delay - 1ms));
	now += ctx.config.delay;
	auto const request = ctx.index.next (index_t::reason::dependency, now);
	ASSERT_TRUE (request);
	ASSERT_EQ (request->reason, index_t::reason::votes);
	ASSERT_TRUE (request->exclude.empty ());

	// It has all of its attempts again
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 3, make_peer (1), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 3, now), index_t::fail_result::retry);
}

/*
 * Only the request a block currently waits on can fail it. An answer to an earlier request that already
 * timed out, or to a request for a block that is not tracked, changes nothing.
 */
TEST (bootstrap_ondemand_index, fail_ignores_stale_request)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();

	// A failure for an untracked block is stale
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now), index_t::fail_result::stale);

	// The first request times out, which already counts as its failure
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	now += ctx.config.request_timeout;
	ASSERT_EQ (ctx.index.timeout (now).size (), 1);

	// Its late failure does not count a second time while the block waits for the next request
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now), index_t::fail_result::stale);

	// Nor once the next request is out
	now += ctx.config.retry_cooldown;
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 2, make_peer (2), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now), index_t::fail_result::stale);

	// The request the block waits on does fail it
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 2, now), index_t::fail_result::retry);
}

/*
 * A request that gets no answer in time is failed by the index itself, so that another peer can be asked
 * long before the request tag expires. Requests still within their time are left alone.
 */
TEST (bootstrap_ondemand_index, timeout_fails_overdue_requests)
{
	test_context ctx;
	auto const early = make_hash (1);
	auto const late = make_hash (2);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (early, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (late, index_t::reason::dependency, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// Two requests, the second sent one second after the first
	ASSERT_TRUE (ctx.index.dispatch (early, /* id */ 1, make_peer (1), now));
	ASSERT_TRUE (ctx.index.dispatch (late, /* id */ 2, make_peer (1), now + 1s));

	// Nothing is overdue just before the first deadline
	ASSERT_TRUE (ctx.index.timeout (now + ctx.config.request_timeout - 1ms).empty ());

	// At the first deadline only the first request fails
	now += ctx.config.request_timeout;
	auto const results = ctx.index.timeout (now);
	ASSERT_EQ (results.size (), 1);
	ASSERT_EQ (results[0].hash, early);
	ASSERT_EQ (results[0].result, index_t::fail_result::retry);

	// The failed block is back in line after its cooldown, avoiding the silent peer
	auto const request = ctx.index.next (index_t::reason::dependency, now + ctx.config.retry_cooldown);
	ASSERT_TRUE (request);
	ASSERT_EQ (request->hash, early);
	ASSERT_EQ (request->exclude.size (), 1);

	// A second sweep at the same time finds nothing new
	ASSERT_TRUE (ctx.index.timeout (now).empty ());

	// The other request fails at its own deadline
	ASSERT_EQ (ctx.index.timeout (now + 1s).size (), 1);
}

/*
 * A timeout on the last allowed request abandons the block like any other failure.
 */
TEST (bootstrap_ondemand_index, timeout_abandons_last_attempt)
{
	test_context ctx;
	ctx.config.max_attempts = 1;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// The only allowed request goes unanswered
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	now += ctx.config.request_timeout;
	auto const results = ctx.index.timeout (now);
	ASSERT_EQ (results.size (), 1);
	ASSERT_EQ (results[0].result, index_t::fail_result::abandoned);

	// The block is not offered again
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 1min));
}

/*
 * A fetched block is handed to the block processor exactly once. A copy that arrives after its request
 * timed out is still taken, a second copy is not.
 */
TEST (bootstrap_ondemand_index, submit_takes_first_copy_only)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();

	// A copy of an untracked block is not wanted
	ASSERT_FALSE (ctx.index.submit (hash));

	// The request times out and the block waits for its next one
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	now += ctx.config.request_timeout;
	ASSERT_EQ (ctx.index.timeout (now).size (), 1);

	// The late copy is taken all the same
	ASSERT_TRUE (ctx.index.submit (hash));

	// The block is not offered for another request, and a second copy is turned away
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 1min));
	ASSERT_FALSE (ctx.index.submit (hash));
}

/*
 * A copy that arrives after the block was abandoned is taken all the same: giving up on requesting a block
 * is no reason to throw away the block when a slow peer delivers it after all.
 */
TEST (bootstrap_ondemand_index, submit_takes_copy_of_abandoned_block)
{
	test_context ctx;
	ctx.config.max_attempts = 1;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// The only allowed request times out, which abandons the block
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	now += ctx.config.request_timeout;
	auto const results = ctx.index.timeout (now);
	ASSERT_EQ (results.size (), 1);
	ASSERT_EQ (results[0].result, index_t::fail_result::abandoned);

	// The answer arrives late and its block is taken, once
	ASSERT_TRUE (ctx.index.submit (hash));
	ASSERT_FALSE (ctx.index.submit (hash));

	// A copy the block processor refuses leaves the block abandoned, with no attempts left to request it again
	ASSERT_EQ (ctx.index.reject (hash, now), index_t::fail_result::abandoned);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 1min));
}

/*
 * A copy the block processor could not use counts as a failed request: the block is requested again
 * from a peer other than the one that delivered the copy.
 */
TEST (bootstrap_ondemand_index, reject_retries_other_peer)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// Neither an untracked block nor one that was not handed over can be rejected
	ASSERT_EQ (ctx.index.reject (make_hash (2), now), index_t::fail_result::stale);
	ASSERT_EQ (ctx.index.reject (hash, now), index_t::fail_result::stale);

	// The delivered copy turns out to be unusable
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	ASSERT_TRUE (ctx.index.submit (hash));
	ASSERT_EQ (ctx.index.reject (hash, now), index_t::fail_result::retry);

	// The block is requested again after the cooldown, avoiding the peer that delivered it
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now));
	auto const request = ctx.index.next (index_t::reason::dependency, now + ctx.config.retry_cooldown);
	ASSERT_TRUE (request);
	ASSERT_EQ (request->exclude.size (), 1);
	ASSERT_EQ (request->exclude[0], make_peer (1));
}

/*
 * A refused copy of a block that has no request to its name still sends the block back in line after the first cooldown.
 * That happens when the copy answers a request made before the block was forgotten and wanted again.
 */
TEST (bootstrap_ondemand_index, reject_without_request)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto const now = std::chrono::steady_clock::now ();

	// The block is tracked afresh and a copy arrives before any request was recorded for it
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_TRUE (ctx.index.submit (hash));

	// The copy is refused, and the block is due again after one cooldown
	ASSERT_EQ (ctx.index.reject (hash, now), index_t::fail_result::retry);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + ctx.config.retry_cooldown - 1ms));
	ASSERT_TRUE (ctx.index.next (index_t::reason::dependency, now + ctx.config.retry_cooldown));
}

/*
 * A fetched block that cannot enter the ledger for now, a fork or one with a missing dependency, is held:
 * it is not requested again and wanting it again changes nothing.
 */
TEST (bootstrap_ondemand_index, hold_stops_requests)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();

	// An untracked block cannot be held
	ASSERT_FALSE (ctx.index.hold (hash));

	// The block is fetched and handed over, then reported as not applicable
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	ASSERT_TRUE (ctx.index.submit (hash));
	ASSERT_TRUE (ctx.index.hold (hash));

	// It stays tracked but is neither requested nor revived by a new demand
	ASSERT_TRUE (ctx.index.contains (hash));
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 1min));
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now + 1min), index_t::insert_result::duplicate);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 2min));
	ASSERT_FALSE (ctx.index.submit (hash));
}

/*
 * Erasing a block reports when it became wanted, which is what the time to repair is measured from.
 */
TEST (bootstrap_ondemand_index, erase_returns_arrival)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto const now = std::chrono::steady_clock::now ();

	// Nothing to report for an untracked block
	ASSERT_FALSE (ctx.index.erase (hash));

	// A tracked block reports its arrival, whatever happened to it in between
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now + ctx.config.delay));
	auto const arrival = ctx.index.erase (hash);
	ASSERT_TRUE (arrival);
	ASSERT_EQ (*arrival, now);

	// It is gone afterwards, and its pending request no longer matters
	ASSERT_FALSE (ctx.index.contains (hash));
	ASSERT_EQ (ctx.index.size (), 0);
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now + ctx.config.delay), index_t::fail_result::stale);
	ASSERT_FALSE (ctx.index.erase (hash));
}

/*
 * Blocks are forgotten once they have been tracked for the maximum age, whatever their state. An abandoned
 * or held block can then be wanted afresh, which bounds how often a block that nobody serves is requested.
 */
TEST (bootstrap_ondemand_index, expire_forgets_old_blocks)
{
	test_context ctx;
	ctx.config.max_attempts = 1;
	auto const abandoned = make_hash (1);
	auto const waiting = make_hash (2);
	auto const now = std::chrono::steady_clock::now ();

	// One block is abandoned, another becomes wanted a minute later
	ASSERT_EQ (ctx.index.insert (abandoned, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_TRUE (ctx.index.dispatch (abandoned, /* id */ 1, make_peer (1), now + ctx.config.delay));
	ASSERT_EQ (ctx.index.fail (abandoned, /* id */ 1, now + ctx.config.delay), index_t::fail_result::abandoned);
	ASSERT_EQ (ctx.index.insert (waiting, index_t::reason::dependency, now + 1min), index_t::insert_result::inserted);

	// Nothing expires before the maximum age
	ASSERT_EQ (ctx.index.expire (now + ctx.config.max_age - 1ms), 0);
	ASSERT_EQ (ctx.index.size (), 2);

	// Each block expires at its own age
	ASSERT_EQ (ctx.index.expire (now + ctx.config.max_age), 1);
	ASSERT_FALSE (ctx.index.contains (abandoned));
	ASSERT_TRUE (ctx.index.contains (waiting));
	ASSERT_EQ (ctx.index.expire (now + ctx.config.max_age + 1min), 1);
	ASSERT_EQ (ctx.index.size (), 0);

	// The abandoned block can be wanted again
	ASSERT_EQ (ctx.index.insert (abandoned, index_t::reason::votes, now + ctx.config.max_age + 1min), index_t::insert_result::inserted);
}

/*
 * When every available peer has been tried the exclusions are dropped, so that the remaining attempts
 * can go to the same peers again; one of them may have received the block in the meantime.
 */
TEST (bootstrap_ondemand_index, exhausted_clears_tried_peers)
{
	test_context ctx;
	auto const hash = make_hash (1);
	auto now = std::chrono::steady_clock::now ();
	ASSERT_EQ (ctx.index.insert (hash, index_t::reason::votes, now), index_t::insert_result::inserted);
	now += ctx.config.delay;

	// The only peer fails, so the next request would exclude it
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 1, make_peer (1), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 1, now), index_t::fail_result::retry);
	now += ctx.config.retry_cooldown;
	auto const before = ctx.index.next (index_t::reason::dependency, now);
	ASSERT_TRUE (before);
	ASSERT_EQ (before->exclude.size (), 1);

	// With the peers exhausted the block is offered without exclusions, the attempt count is kept
	ctx.index.exhausted (hash);
	auto const after = ctx.index.next (index_t::reason::dependency, now);
	ASSERT_TRUE (after);
	ASSERT_TRUE (after->exclude.empty ());
	ASSERT_TRUE (ctx.index.dispatch (hash, /* id */ 2, make_peer (1), now));
	ASSERT_EQ (ctx.index.fail (hash, /* id */ 2, now), index_t::fail_result::retry);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, now + 499ms));
	ASSERT_TRUE (ctx.index.next (index_t::reason::dependency, now + 500ms));
}

/*
 * Clearing the index forgets every block and makes requests still in flight harmless.
 */
TEST (bootstrap_ondemand_index, clear_forgets_everything)
{
	test_context ctx;
	auto const requested = make_hash (1);
	auto const waiting = make_hash (2);
	auto const now = std::chrono::steady_clock::now ();
	auto const due = now + ctx.config.delay;

	// One block with a request out, one still waiting
	ASSERT_EQ (ctx.index.insert (requested, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_EQ (ctx.index.insert (waiting, index_t::reason::votes, now), index_t::insert_result::inserted);
	ASSERT_TRUE (ctx.index.dispatch (requested, /* id */ 1, make_peer (1), due));

	ctx.index.clear ();

	// Nothing is tracked or offered, and the answer to the old request is neither a failure nor a wanted copy
	ASSERT_EQ (ctx.index.size (), 0);
	ASSERT_FALSE (ctx.index.next (index_t::reason::dependency, due));
	ASSERT_EQ (ctx.index.fail (requested, /* id */ 1, due), index_t::fail_result::stale);
	ASSERT_FALSE (ctx.index.submit (requested));
	ASSERT_TRUE (ctx.index.timeout (due + ctx.config.request_timeout).empty ());
}
