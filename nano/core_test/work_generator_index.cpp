#include <nano/node/work_generator.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
using index_t = nano::work_generator_index;

nano::work_peer const peer_a{ "10.0.0.1", 7076 };
nano::work_peer const peer_b{ "10.0.0.2", 7076 };

// A request to the primary work peers for the root
nano::work_request make_request (uint64_t root = 1)
{
	return nano::work_request{ .root = nano::root{ root }, .difficulty = 100 };
}

// What an exchange reports when the peer delivered work
nano::work_peer_response delivered (uint64_t work)
{
	return nano::work_peer_response{ nano::work_peer_status::success, work, /* request sent */ true };
}

// What an exchange reports when it ended without work
nano::work_peer_response ended (nano::work_peer_status status, bool request_sent = true)
{
	return nano::work_peer_response{ status, /* work */ 0, request_sent };
}
}

/*
 * A request on a node without work peers.
 * It is generated locally right away and ends with the work the pool delivers
 */
TEST (work_generator_index, local_without_peers)
{
	index_t index{ index_t::limits{} };
	auto const now = std::chrono::steady_clock::now ();

	// The request starts local generation and nothing else
	auto const id = index.insert (make_request (), nano::work_callback{}, now);
	ASSERT_TRUE (id.has_value ());
	auto const started = index.advance (now);
	ASSERT_EQ (1, started.locals.size ());
	ASSERT_EQ (*id, started.locals[0].request);
	ASSERT_TRUE (started.exchanges.empty ());
	ASSERT_TRUE (started.completions.empty ());
	ASSERT_TRUE (index.local_started (*id, /* pool id */ 7));
	ASSERT_EQ (1, index.local_count ());

	// It is not started a second time
	ASSERT_TRUE (index.advance (now + 1s).locals.empty ());

	// The pool delivers, which ends the request
	index.local_finished (*id, 42);
	auto const finished = index.advance (now + 2s);
	ASSERT_EQ (1, finished.completions.size ());
	auto const & summary = finished.completions[0].summary;
	ASSERT_EQ (nano::work_status::success, summary.result.status);
	ASSERT_EQ (42, summary.result.work);
	ASSERT_EQ ("local", summary.source);
	ASSERT_EQ (2000ms, summary.duration);
	ASSERT_TRUE (finished.abandoned_locals.empty ());
	ASSERT_EQ (0, index.size ());
	ASSERT_EQ (0, index.local_count ());
}

/*
 * Requests that nothing can generate: no work peers and no local generation, by the node's hardware or by the `never` mode,
 * and a request that has to stay on a node whose only source is its work peers.
 * Each one fails on the first round instead of waiting for anything
 */
TEST (work_generator_index, nothing_to_generate_with)
{
	auto const now = std::chrono::steady_clock::now ();

	// A node that has no work threads
	{
		index_t index{ index_t::limits{ .local_available = false } };
		ASSERT_FALSE (index.enabled (nano::work_peer_group::primary));
		index.insert (make_request (), nano::work_callback{}, now);
		auto const actions = index.advance (now);
		ASSERT_EQ (1, actions.completions.size ());
		ASSERT_EQ (nano::work_status::failed, actions.completions[0].summary.result.status);
		ASSERT_EQ (0, index.size ());
	}

	// A node that is configured never to generate
	{
		index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never } };
		ASSERT_FALSE (index.local_enabled ());
		index.insert (make_request (), nano::work_callback{}, now);
		auto const actions = index.advance (now);
		ASSERT_TRUE (actions.locals.empty ());
		ASSERT_EQ (1, actions.completions.size ());
		ASSERT_EQ (nano::work_status::failed, actions.completions[0].summary.result.status);
	}

	// A request without a peer group does not go to the work peers the node has
	{
		index_t index{ index_t::limits{ .local_available = false } };
		index.add_peer (nano::work_peer_group::primary, peer_a);
		ASSERT_TRUE (index.enabled (nano::work_peer_group::primary));
		ASSERT_FALSE (index.enabled (std::nullopt));
		auto request = make_request ();
		request.peers = std::nullopt;
		index.insert (request, nano::work_callback{}, now);
		auto const actions = index.advance (now);
		ASSERT_TRUE (actions.exchanges.empty ());
		ASSERT_EQ (1, actions.completions.size ());
		ASSERT_EQ (nano::work_status::failed, actions.completions[0].summary.result.status);
	}
}

/*
 * The `fallback` mode with work peers that neither deliver nor fail.
 * Every peer is asked at once, and local generation joins when `local_delay` has passed, not before
 */
TEST (work_generator_index, fallback_after_delay)
{
	index_t index{ index_t::limits{ .local_delay = 5s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	auto const now = std::chrono::steady_clock::now ();

	// Both peers are asked, each with the time one attempt gets, and nothing is generated locally
	auto const id = index.insert (make_request (), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (2, started.exchanges.size ());
	ASSERT_EQ (peer_a, started.exchanges[0].peer);
	ASSERT_EQ (peer_b, started.exchanges[1].peer);
	ASSERT_EQ (now + 30s, started.exchanges[0].deadline);
	ASSERT_TRUE (started.locals.empty ());

	// Still nothing just before the delay is over, and the peers are not asked again
	auto const waiting = index.advance (now + 4999ms);
	ASSERT_TRUE (waiting.exchanges.empty ());
	ASSERT_TRUE (waiting.locals.empty ());

	// Local generation starts when the delay is over
	auto const joined = index.advance (now + 5s);
	ASSERT_EQ (1, joined.locals.size ());
	ASSERT_EQ (*id, joined.locals[0].request);
}

/*
 * The `fallback` mode with work peers that fail.
 * Local generation starts as soon as the last peer has failed, without waiting for `local_delay`, and the failed peers are named in the summary
 */
TEST (work_generator_index, fallback_after_failures)
{
	index_t index{ index_t::limits{ .local_delay = 5s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	auto const now = std::chrono::steady_clock::now ();

	auto const id = index.insert (make_request (), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (2, started.exchanges.size ());

	// One failure leaves a peer working, so nothing starts yet
	index.exchange_finished (started.exchanges[0].id, ended (nano::work_peer_status::http_error), now + 10ms);
	ASSERT_TRUE (index.advance (now + 10ms).locals.empty ());

	// The second failure leaves nobody
	index.exchange_finished (started.exchanges[1].id, ended (nano::work_peer_status::unreachable, /* request sent */ false), now + 20ms);
	auto const joined = index.advance (now + 20ms);
	ASSERT_EQ (1, joined.locals.size ());
	ASSERT_TRUE (joined.completions.empty ());

	// The request ends with the local work and names both peers
	ASSERT_TRUE (index.local_started (*id, /* pool id */ 1));
	index.local_finished (*id, 42);
	auto const finished = index.advance (now + 30ms);
	ASSERT_EQ (1, finished.completions.size ());
	auto const & summary = finished.completions[0].summary;
	ASSERT_EQ (nano::work_status::success, summary.result.status);
	ASSERT_EQ ("local", summary.source);
	ASSERT_EQ (2, summary.bad_peers.size ());
	ASSERT_EQ (peer_a.to_string (), summary.bad_peers[0]);
	ASSERT_EQ (peer_b.to_string (), summary.bad_peers[1]);
}

/*
 * The `always` mode.
 * Local generation starts in the same round as the exchanges with the work peers
 */
TEST (work_generator_index, always_generates_locally)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::always } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	auto const now = std::chrono::steady_clock::now ();

	index.insert (make_request (), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (1, started.exchanges.size ());
	ASSERT_EQ (1, started.locals.size ());
}

/*
 * A work peer delivers while another peer and local generation are still working on the request.
 * The request ends with the peer's work, its local generation is cancelled by its pool item, and the peer left behind is told to stop
 */
TEST (work_generator_index, peer_delivers)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::always } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	auto const now = std::chrono::steady_clock::now ();

	auto const id = index.insert (make_request (), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (2, started.exchanges.size ());
	ASSERT_TRUE (index.local_started (*id, /* pool id */ 9));

	// The first peer delivers, a second report for the same exchange changes nothing
	index.exchange_finished (started.exchanges[0].id, delivered (42), now + 1s);
	index.exchange_finished (started.exchanges[0].id, ended (nano::work_peer_status::http_error), now + 1s);
	auto const finished = index.advance (now + 1s);
	ASSERT_EQ (1, finished.completions.size ());
	auto const & summary = finished.completions[0].summary;
	ASSERT_EQ (nano::work_status::success, summary.result.status);
	ASSERT_EQ (42, summary.result.work);
	ASSERT_EQ (peer_a.to_string (), summary.source);
	ASSERT_TRUE (summary.bad_peers.empty ());

	// Its pool item is the one to cancel
	ASSERT_EQ (1, finished.abandoned_locals.size ());
	ASSERT_EQ (9, finished.abandoned_locals[0]);
	ASSERT_EQ (0, index.size ());

	// The pool answers for the cancelled item, which nothing waits for any more
	index.local_finished (*id, std::nullopt);
	ASSERT_EQ (0, index.local_count ());

	// The other exchange ends as cancelled, and the next round has a `work_cancel` for its peer
	index.exchange_finished (started.exchanges[1].id, ended (nano::work_peer_status::cancelled), now + 1s);
	auto const after = index.advance (now + 1s);
	ASSERT_EQ (1, after.cancels.size ());
	ASSERT_EQ (peer_b, after.cancels[0].peer);
	ASSERT_EQ (nano::root{ 1 }, after.cancels[0].root);
	ASSERT_TRUE (after.completions.empty ());
}

/*
 * When a work peer left behind is told to stop.
 * Not while another request has the same peer working on the same root, since a `work_cancel` names only the root,
 * not when the request never reached the peer, and not when the peer answered
 */
TEST (work_generator_index, work_cancel_rule)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	auto const now = std::chrono::steady_clock::now ();

	// Two requests for the same root, each with both peers working
	auto const first = index.insert (make_request (), nano::work_callback{}, now);
	auto const second = index.insert (make_request (), nano::work_callback{}, now);
	ASSERT_NE (*first, *second);
	auto const started = index.advance (now);
	ASSERT_EQ (4, started.exchanges.size ());
	auto const & first_a = started.exchanges[0];
	auto const & first_b = started.exchanges[1];
	auto const & second_a = started.exchanges[2];
	auto const & second_b = started.exchanges[3];
	ASSERT_EQ (peer_a, first_a.peer);
	ASSERT_EQ (peer_a, second_a.peer);

	// The second peer delivers for the first request, whose exchange with the first peer is left:
	// that peer still works on the root for the second request, so it is not told to stop
	index.exchange_finished (first_b.id, delivered (42), now + 1s);
	ASSERT_EQ (1, index.advance (now + 1s).completions.size ());
	index.exchange_finished (first_a.id, ended (nano::work_peer_status::cancelled), now + 1s);
	ASSERT_TRUE (index.advance (now + 1s).cancels.empty ());

	// The second request ends the same way, and now nobody waits for the root
	index.exchange_finished (second_b.id, delivered (43), now + 2s);
	ASSERT_EQ (1, index.advance (now + 2s).completions.size ());
	index.exchange_finished (second_a.id, ended (nano::work_peer_status::cancelled), now + 2s);
	auto const told = index.advance (now + 2s);
	ASSERT_EQ (1, told.cancels.size ());
	ASSERT_EQ (peer_a, told.cancels[0].peer);
	ASSERT_EQ (nano::root{ 1 }, told.cancels[0].root);

	// A request for another root is cancelled: one exchange had not sent it yet, the other got an answer, neither has anything to stop
	index.insert (make_request (2), nano::work_callback{}, now + 3s);
	auto const restarted = index.advance (now + 3s);
	ASSERT_EQ (2, restarted.exchanges.size ());
	index.cancel (nano::root{ 2 });
	ASSERT_EQ (1, index.advance (now + 3s).completions.size ());
	index.exchange_finished (restarted.exchanges[0].id, ended (nano::work_peer_status::cancelled, /* request sent */ false), now + 3s);
	index.exchange_finished (restarted.exchanges[1].id, ended (nano::work_peer_status::http_error), now + 3s);
	ASSERT_TRUE (index.advance (now + 3s).cancels.empty ());
}

/*
 * A node that only has its work peers, and the peers keep failing.
 * A failed peer is left alone for a time that doubles with each failure, then asked again with a new time limit for the attempt,
 * and the request fails when its own time is over, leaving the attempt that is still running
 */
TEST (work_generator_index, retry_until_timeout)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never, .peer_timeout = 4s, .work_timeout = 10s, .retry_backoff = 1s, .retry_backoff_max = 30s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	auto const now = std::chrono::steady_clock::now ();

	index.insert (make_request (), nano::work_callback{}, now);
	auto const first = index.advance (now);
	ASSERT_EQ (1, first.exchanges.size ());
	ASSERT_EQ (now + 4s, first.exchanges[0].deadline);

	// The first failure rests the peer for one backoff, the request waits
	index.exchange_finished (first.exchanges[0].id, ended (nano::work_peer_status::http_error), now);
	auto const resting = index.advance (now + 999ms);
	ASSERT_TRUE (resting.exchanges.empty ());
	ASSERT_TRUE (resting.completions.empty ());
	auto const second = index.advance (now + 1s);
	ASSERT_EQ (1, second.exchanges.size ());
	ASSERT_EQ (now + 5s, second.exchanges[0].deadline);

	// The second failure in a row rests it for two
	index.exchange_finished (second.exchanges[0].id, ended (nano::work_peer_status::refused), now + 1s);
	ASSERT_TRUE (index.advance (now + 2999ms).exchanges.empty ());
	auto const third = index.advance (now + 3s);
	ASSERT_EQ (1, third.exchanges.size ());
	ASSERT_TRUE (index.advance (now + 9999ms).completions.empty ());

	// The request's time is over while the third attempt is running: the request fails
	auto const finished = index.advance (now + 10s);
	ASSERT_TRUE (finished.exchanges.empty ());
	ASSERT_EQ (1, finished.completions.size ());
	auto const & summary = finished.completions[0].summary;
	ASSERT_EQ (nano::work_status::failed, summary.result.status);
	ASSERT_EQ (1, summary.bad_peers.size ());
	ASSERT_EQ (0, index.size ());

	// The attempt it left ends as cancelled, and the peer is told to stop
	index.exchange_finished (third.exchanges[0].id, ended (nano::work_peer_status::cancelled), now + 10s);
	ASSERT_EQ (1, index.advance (now + 10s).cancels.size ());
}

/*
 * How a work peer has been doing is shared by the requests.
 * A request that comes in while every peer rests after a failure generates locally right away,
 * the peer is asked again when its rest is over, and a delivery clears its failures
 */
TEST (work_generator_index, peer_rest_is_shared)
{
	index_t index{ index_t::limits{ .local_delay = 5s, .retry_backoff = 1s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	auto const now = std::chrono::steady_clock::now ();

	// The peer fails for a first request
	auto const first = index.insert (make_request (1), nano::work_callback{}, now);
	auto const started = index.advance (now);
	index.exchange_finished (started.exchanges[0].id, ended (nano::work_peer_status::io_error), now);
	index.advance (now);

	// A second request does not ask the resting peer and does not wait for the delay either
	auto const second = index.insert (make_request (2), nano::work_callback{}, now + 100ms);
	auto const joined = index.advance (now + 100ms);
	ASSERT_TRUE (joined.exchanges.empty ());
	ASSERT_EQ (1, joined.locals.size ());
	ASSERT_EQ (*second, joined.locals[0].request);

	// Once the rest is over the peer is asked for both requests
	auto const retried = index.advance (now + 1s);
	ASSERT_EQ (2, retried.exchanges.size ());

	// It delivers for the first one, then fails for the second: its rest starts from one backoff again
	index.exchange_finished (retried.exchanges[0].id, delivered (42), now + 2s);
	index.exchange_finished (retried.exchanges[1].id, ended (nano::work_peer_status::io_error), now + 2s);
	auto const finished = index.advance (now + 2s);
	ASSERT_EQ (1, finished.completions.size ());
	ASSERT_EQ (nano::work_status::success, finished.completions[0].summary.result.status);
	ASSERT_TRUE (index.advance (now + 2999ms).exchanges.empty ());
	ASSERT_EQ (1, index.advance (now + 3s).exchanges.size ());
	ASSERT_TRUE (first.has_value ());
}

/*
 * A cancel for a root.
 * It ends every request for that root and no other, also one that is waiting to ask its peers again, and such a request asks nobody afterwards
 */
TEST (work_generator_index, cancel)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never, .retry_backoff = 1s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	auto const now = std::chrono::steady_clock::now ();

	// Two requests for one root and one for another, all waiting for the peer to be asked again
	index.insert (make_request (1), nano::work_callback{}, now);
	index.insert (make_request (1), nano::work_callback{}, now);
	index.insert (make_request (2), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (3, started.exchanges.size ());
	for (auto const & exchange : started.exchanges)
	{
		index.exchange_finished (exchange.id, ended (nano::work_peer_status::unreachable, /* request sent */ false), now);
	}
	ASSERT_TRUE (index.advance (now).completions.empty ());

	// Cancelling the first root ends its two requests
	ASSERT_EQ (2, index.cancel (nano::root{ 1 }));
	auto const cancelled = index.advance (now + 500ms);
	ASSERT_EQ (2, cancelled.completions.size ());
	ASSERT_EQ (nano::work_status::cancelled, cancelled.completions[0].summary.result.status);
	ASSERT_EQ (nano::work_status::cancelled, cancelled.completions[1].summary.result.status);
	ASSERT_EQ (1, index.size ());

	// When the peer's rest is over it is asked for the remaining request only
	auto const retried = index.advance (now + 1s);
	ASSERT_EQ (1, retried.exchanges.size ());
	ASSERT_EQ (nano::root{ 2 }, retried.exchanges[0].request.root);

	// A root nobody asked for cancels nothing
	ASSERT_EQ (0, index.cancel (nano::root{ 3 }));
}

/*
 * Local generation that ends without the request having asked for that.
 * A pool that answers with no work has spent the request's local generation: it is not started again, and with no peer left the request fails.
 * A request that ended while its pool item was being queued tells the generator to cancel the item
 */
TEST (work_generator_index, local_edge_cases)
{
	index_t index{ index_t::limits{} };
	auto const now = std::chrono::steady_clock::now ();

	// The pool gives up on a request
	auto const spent = index.insert (make_request (1), nano::work_callback{}, now);
	ASSERT_EQ (1, index.advance (now).locals.size ());
	ASSERT_TRUE (index.local_started (*spent, /* pool id */ 1));
	index.local_finished (*spent, std::nullopt);
	auto const failed = index.advance (now);
	ASSERT_TRUE (failed.locals.empty ());
	ASSERT_EQ (1, failed.completions.size ());
	ASSERT_EQ (nano::work_status::failed, failed.completions[0].summary.result.status);

	// A request is cancelled between the round that started its generation and the pool id coming in
	auto const raced = index.insert (make_request (2), nano::work_callback{}, now);
	ASSERT_EQ (1, index.advance (now).locals.size ());
	index.cancel (nano::root{ 2 });
	auto const cancelled = index.advance (now);
	ASSERT_EQ (1, cancelled.completions.size ());
	ASSERT_TRUE (cancelled.abandoned_locals.empty ());
	ASSERT_FALSE (index.local_started (*raced, /* pool id */ 2));

	// The item counts as outstanding until the pool has answered for it
	ASSERT_EQ (1, index.local_count ());
	index.local_finished (*raced, std::nullopt);
	ASSERT_EQ (0, index.local_count ());

	// An answer for a request that is unknown changes nothing
	index.local_finished (/* request */ 1000, 42);
	ASSERT_FALSE (index.local_started (/* request */ 1000, /* pool id */ 3));
}

/*
 * Stopping the index.
 * Every request in progress ends as cancelled with its local generation to cancel, later requests are refused,
 * and exchanges that end afterwards tell no peer to stop
 */
TEST (work_generator_index, stop)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::always } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	auto const now = std::chrono::steady_clock::now ();

	// A request with an exchange and local generation running
	auto const id = index.insert (make_request (), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (1, started.exchanges.size ());
	ASSERT_TRUE (index.local_started (*id, /* pool id */ 5));

	// Stopping ends it
	ASSERT_FALSE (index.stopped ());
	auto const stopped = index.stop (now + 1s);
	ASSERT_TRUE (index.stopped ());
	ASSERT_EQ (1, stopped.completions.size ());
	ASSERT_EQ (nano::work_status::cancelled, stopped.completions[0].summary.result.status);
	ASSERT_EQ (1, stopped.abandoned_locals.size ());
	ASSERT_EQ (5, stopped.abandoned_locals[0]);
	ASSERT_EQ (0, index.size ());

	// New requests are refused
	ASSERT_FALSE (index.insert (make_request (), nano::work_callback{}, now + 1s).has_value ());

	// The exchange ends as cancelled, and no `work_cancel` follows
	index.exchange_finished (started.exchanges[0].id, ended (nano::work_peer_status::cancelled), now + 1s);
	ASSERT_TRUE (index.advance (now + 1s).cancels.empty ());

	// The exchanges stay until the generator removes them
	ASSERT_EQ (1, index.exchange_count ());
	auto const removed = index.erase_exchanges_if ([] (auto const & exchange) {
		return exchange.finished;
	});
	ASSERT_EQ (1, removed.size ());
	ASSERT_EQ (0, index.exchange_count ());
}

/*
 * The work peer lists.
 * A peer is listed once per group, the groups are separate, a peer added while a request runs is asked in the next round,
 * and a request whose group lost its peers falls back or fails like one that never had any
 */
TEST (work_generator_index, peers)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never } };
	auto const now = std::chrono::steady_clock::now ();

	// Adding a peer twice lists it once
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::secondary, peer_b);
	ASSERT_EQ (1, index.peers (nano::work_peer_group::primary).size ());
	ASSERT_EQ (peer_a, index.peers (nano::work_peer_group::primary)[0]);
	ASSERT_EQ (1, index.peers (nano::work_peer_group::secondary).size ());
	ASSERT_EQ (peer_b, index.peers (nano::work_peer_group::secondary)[0]);

	// A request to the secondary group asks its peer only
	auto request = make_request ();
	request.peers = nano::work_peer_group::secondary;
	index.insert (request, nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (1, started.exchanges.size ());
	ASSERT_EQ (peer_b, started.exchanges[0].peer);

	// A peer added to that group is asked in the next round
	index.add_peer (nano::work_peer_group::secondary, peer_a);
	auto const added = index.advance (now + 1s);
	ASSERT_EQ (1, added.exchanges.size ());
	ASSERT_EQ (peer_a, added.exchanges[0].peer);

	// Clearing the group leaves the running exchanges, and the request fails once they have
	index.clear_peers (nano::work_peer_group::secondary);
	ASSERT_TRUE (index.peers (nano::work_peer_group::secondary).empty ());
	ASSERT_FALSE (index.enabled (nano::work_peer_group::secondary));
	ASSERT_TRUE (index.enabled (nano::work_peer_group::primary));
	index.exchange_finished (started.exchanges[0].id, ended (nano::work_peer_status::http_error), now + 2s);
	ASSERT_TRUE (index.advance (now + 2s).completions.empty ());
	index.exchange_finished (added.exchanges[0].id, ended (nano::work_peer_status::http_error), now + 3s);
	auto const finished = index.advance (now + 3s);
	ASSERT_EQ (1, finished.completions.size ());
	ASSERT_EQ (nano::work_status::failed, finished.completions[0].summary.result.status);
}

/*
 * A task handed to the index for an exchange it does not know.
 * The index stops the node instead of dropping the task, which nothing would cancel and join afterwards
 */
TEST (work_generator_index_DeathTest, attach_unknown_exchange)
{
	index_t index{ index_t::limits{} };
	ASSERT_DEATH_IF_SUPPORTED (index.attach (/* exchange */ 1, nullptr), "existing != by_id.end");
}

/*
 * A request that the node generates work for itself, with a work peer that keeps failing.
 * The request's time limit does not apply to it: it does not fail, and the peer is asked again for as long as the request runs
 */
TEST (work_generator_index, peers_asked_while_generating)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::always, .work_timeout = 10s, .retry_backoff = 1s, .retry_backoff_max = 1s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	auto const now = std::chrono::steady_clock::now ();

	// The peer is asked and the node generates
	auto const id = index.insert (make_request (), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (1, started.exchanges.size ());
	ASSERT_EQ (1, started.locals.size ());
	ASSERT_TRUE (index.local_started (*id, /* pool id */ 1));

	// The peer fails long after the request's time limit, and the request goes on
	index.exchange_finished (started.exchanges[0].id, ended (nano::work_peer_status::io_error), now + 20s);
	auto const waiting = index.advance (now + 20s);
	ASSERT_TRUE (waiting.exchanges.empty ());
	ASSERT_TRUE (waiting.completions.empty ());

	// The peer is asked again once it has rested
	auto const retried = index.advance (now + 21s);
	ASSERT_EQ (1, retried.exchanges.size ());
	ASSERT_EQ (peer_a, retried.exchanges[0].peer);
	ASSERT_TRUE (retried.completions.empty ());

	// The node's own work ends the request
	index.local_finished (*id, 42);
	auto const finished = index.advance (now + 22s);
	ASSERT_EQ (1, finished.completions.size ());
	ASSERT_EQ (nano::work_status::success, finished.completions[0].summary.result.status);
	ASSERT_EQ ("local", finished.completions[0].summary.source);
}

/*
 * Work peers asked one at first.
 * Each new request starts with the next peer in turn, and no other peer is asked while that one has not had its time
 */
TEST (work_generator_index, one_peer_rotation)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never, .peer_selection = nano::work_peer_selection::one, .peer_delay = 15s, .peer_timeout = 30s } };
	nano::work_peer const peer_c{ "10.0.0.3", 7076 };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	index.add_peer (nano::work_peer_group::primary, peer_c);
	auto const now = std::chrono::steady_clock::now ();

	// Three requests are each given a peer of their own, with the time one attempt gets
	index.insert (make_request (1), nano::work_callback{}, now);
	index.insert (make_request (2), nano::work_callback{}, now);
	index.insert (make_request (3), nano::work_callback{}, now);
	auto const started = index.advance (now);
	ASSERT_EQ (3, started.exchanges.size ());
	ASSERT_EQ (peer_a, started.exchanges[0].peer);
	ASSERT_EQ (peer_b, started.exchanges[1].peer);
	ASSERT_EQ (peer_c, started.exchanges[2].peer);
	ASSERT_EQ (nano::root{ 1 }, started.exchanges[0].request.root);
	ASSERT_EQ (now + 30s, started.exchanges[0].deadline);

	// No other peer is asked while a request's peer has not had its time
	ASSERT_TRUE (index.advance (now + 1s).exchanges.empty ());

	// A fourth request starts over with the first peer
	index.insert (make_request (4), nano::work_callback{}, now + 1s);
	auto const fourth = index.advance (now + 1s);
	ASSERT_EQ (1, fourth.exchanges.size ());
	ASSERT_EQ (peer_a, fourth.exchanges[0].peer);
	ASSERT_EQ (nano::root{ 4 }, fourth.exchanges[0].request.root);
}

/*
 * A work peer that is asked on its own fails.
 * The next peer is asked at once, the first one again once it has rested, and the request fails when its time is over
 */
TEST (work_generator_index, one_peer_failover)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never, .peer_selection = nano::work_peer_selection::one, .peer_delay = 15s, .work_timeout = 60s, .retry_backoff = 1s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	auto const now = std::chrono::steady_clock::now ();

	// The first peer is asked
	index.insert (make_request (), nano::work_callback{}, now);
	auto const first = index.advance (now);
	ASSERT_EQ (1, first.exchanges.size ());
	ASSERT_EQ (peer_a, first.exchanges[0].peer);

	// It fails: the second peer is asked without waiting for the delay
	index.exchange_finished (first.exchanges[0].id, ended (nano::work_peer_status::http_error), now + 2s);
	auto const second = index.advance (now + 2s);
	ASSERT_EQ (1, second.exchanges.size ());
	ASSERT_EQ (peer_b, second.exchanges[0].peer);
	ASSERT_TRUE (second.completions.empty ());

	// The second peer fails too: the first one has rested and is asked again
	index.exchange_finished (second.exchanges[0].id, ended (nano::work_peer_status::refused), now + 4s);
	auto const third = index.advance (now + 4s);
	ASSERT_EQ (1, third.exchanges.size ());
	ASSERT_EQ (peer_a, third.exchanges[0].peer);

	// The request's time is over, which ends it
	auto const finished = index.advance (now + 60s);
	ASSERT_TRUE (finished.exchanges.empty ());
	ASSERT_EQ (1, finished.completions.size ());
	ASSERT_EQ (nano::work_status::failed, finished.completions[0].summary.result.status);
	ASSERT_EQ (2, finished.completions[0].summary.bad_peers.size ());
}

/*
 * A work peer that is asked on its own does not deliver.
 * Each time `peer_delay` has passed the next peer is asked as well, the peers asked before keep working,
 * and the ones left working when another delivers are told to stop
 */
TEST (work_generator_index, one_peer_delay)
{
	index_t index{ index_t::limits{ .local_generation = nano::work_local_mode::never, .peer_selection = nano::work_peer_selection::one, .peer_delay = 15s } };
	nano::work_peer const peer_c{ "10.0.0.3", 7076 };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	index.add_peer (nano::work_peer_group::primary, peer_c);
	auto const now = std::chrono::steady_clock::now ();

	// The first peer is asked
	index.insert (make_request (), nano::work_callback{}, now);
	auto const first = index.advance (now);
	ASSERT_EQ (1, first.exchanges.size ());
	ASSERT_EQ (peer_a, first.exchanges[0].peer);

	// The second one joins when the delay is over, not before
	ASSERT_TRUE (index.advance (now + 14999ms).exchanges.empty ());
	auto const second = index.advance (now + 15s);
	ASSERT_EQ (1, second.exchanges.size ());
	ASSERT_EQ (peer_b, second.exchanges[0].peer);

	// The third one joins a delay after the second
	ASSERT_TRUE (index.advance (now + 29999ms).exchanges.empty ());
	auto const third = index.advance (now + 30s);
	ASSERT_EQ (1, third.exchanges.size ());
	ASSERT_EQ (peer_c, third.exchanges[0].peer);

	// Every peer is working, so nothing is left to ask
	ASSERT_TRUE (index.advance (now + 45s).exchanges.empty ());

	// The second peer delivers, which ends the request
	index.exchange_finished (second.exchanges[0].id, delivered (42), now + 46s);
	auto const finished = index.advance (now + 46s);
	ASSERT_EQ (1, finished.completions.size ());
	ASSERT_EQ (nano::work_status::success, finished.completions[0].summary.result.status);
	ASSERT_EQ (peer_b.to_string (), finished.completions[0].summary.source);

	// The two peers left working are told to stop
	index.exchange_finished (first.exchanges[0].id, ended (nano::work_peer_status::cancelled), now + 46s);
	index.exchange_finished (third.exchanges[0].id, ended (nano::work_peer_status::cancelled), now + 46s);
	ASSERT_EQ (2, index.advance (now + 46s).cancels.size ());
}

/*
 * Work peers asked one at first on a node that generates work itself in `fallback` mode.
 * Local generation waits while a peer is left that can be asked, and starts once every peer has failed
 */
TEST (work_generator_index, one_peer_local_fallback)
{
	index_t index{ index_t::limits{ .local_delay = 5s, .peer_selection = nano::work_peer_selection::one, .retry_backoff = 10s } };
	index.add_peer (nano::work_peer_group::primary, peer_a);
	index.add_peer (nano::work_peer_group::primary, peer_b);
	auto const now = std::chrono::steady_clock::now ();

	// The first peer is asked and nothing is generated locally
	auto const id = index.insert (make_request (), nano::work_callback{}, now);
	auto const first = index.advance (now);
	ASSERT_EQ (1, first.exchanges.size ());
	ASSERT_TRUE (first.locals.empty ());

	// It fails: the second peer takes over, still without local generation
	index.exchange_finished (first.exchanges[0].id, ended (nano::work_peer_status::http_error), now + 1s);
	auto const second = index.advance (now + 1s);
	ASSERT_EQ (1, second.exchanges.size ());
	ASSERT_EQ (peer_b, second.exchanges[0].peer);
	ASSERT_TRUE (second.locals.empty ());

	// The second peer fails as well: both rest, and the node generates the work itself
	index.exchange_finished (second.exchanges[0].id, ended (nano::work_peer_status::http_error), now + 2s);
	auto const third = index.advance (now + 2s);
	ASSERT_TRUE (third.exchanges.empty ());
	ASSERT_EQ (1, third.locals.size ());
	ASSERT_EQ (*id, third.locals[0].request);
}
