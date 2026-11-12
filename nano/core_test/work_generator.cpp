#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/work_generator.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/fake_work_peer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <chrono>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace
{
// No work reaches this difficulty, so local generation for it runs until it is cancelled
constexpr auto unreachable_difficulty = std::numeric_limits<uint64_t>::max ();

// The results the callbacks of a test's requests were called with. Declare it before the context, callbacks run until the generator stops
class result_log final
{
public:
	nano::work_callback callback ()
	{
		return [this] (nano::work_result const & result) {
			std::lock_guard<std::mutex> lock{ mutex };
			results.push_back (result);
		};
	}

	size_t size () const
	{
		std::lock_guard<std::mutex> lock{ mutex };
		return results.size ();
	}

	size_t count (nano::work_status status) const
	{
		std::lock_guard<std::mutex> lock{ mutex };
		return std::count_if (results.begin (), results.end (), [status] (auto const & result) {
			return result.status == status;
		});
	}

	nano::work_result at (size_t position) const
	{
		std::lock_guard<std::mutex> lock{ mutex };
		return results.at (position);
	}

private:
	mutable std::mutex mutex;
	std::vector<nano::work_result> results;
};

// The summaries the generator reported for a test's requests. Declare it before the context, like the result log
class summary_log final
{
public:
	void listen (nano::work_generator & generator)
	{
		generator.request_finished.add ([this] (nano::work_summary const & summary) {
			std::lock_guard<std::mutex> lock{ mutex };
			summaries.push_back (summary);
		});
	}

	size_t size () const
	{
		std::lock_guard<std::mutex> lock{ mutex };
		return summaries.size ();
	}

	nano::work_summary at (size_t position) const
	{
		std::lock_guard<std::mutex> lock{ mutex };
		return summaries.at (position);
	}

private:
	mutable std::mutex mutex;
	std::vector<nano::work_summary> summaries;
};

// A generator without a node: an io_context with its threads, a work pool, and a node config to set up before `start`
class generator_context final
{
public:
	generator_context ()
	{
		// The first pool thread of a process takes a while to come up under a sanitizer, which the tests' deadlines must not pay for
		pool.generate (nano::work_version::work_1, nano::root{ 1 }, nano::dev::network_params.work.base);
	}

	~generator_context ()
	{
		if (generator)
		{
			generator->stop ();
		}
	}

	// Creates the generator from the config as the test has set it up
	nano::work_generator & start ()
	{
		generator = std::make_unique<nano::work_generator> (config, pool, *io_ctx, stats, logger);
		return *generator;
	}

	// Lists a fake peer among the node's work peers, to be called before `start`
	void add_peer (nano::test::fake_work_peer const & peer)
	{
		config.work_peers.emplace_back (peer.peer ().address, peer.port ());
	}

	// A request for the root at the base difficulty, to the primary work peers
	nano::work_request request (uint64_t root = 1) const
	{
		return nano::work_request{ .root = nano::root{ root }, .difficulty = nano::dev::network_params.work.base };
	}

	bool valid (nano::work_request const & request, nano::work_result const & result) const
	{
		return result.status == nano::work_status::success && nano::dev::network_params.work.difficulty (request.version, request.root, result.work) >= request.difficulty;
	}

	std::shared_ptr<asio::io_context> io_ctx{ std::make_shared<asio::io_context> () };
	nano::logger logger;
	nano::stats stats{ logger };
	nano::thread_runner runner{ io_ctx, logger, /* threads */ 2 };
	nano::work_pool pool{ nano::dev::network_params.network, /* max threads */ 1 };
	nano::node_config config{ nano::dev::network_params };
	std::unique_ptr<nano::work_generator> generator;
};
}

/*
 * A node without work peers.
 * The work is generated on the node's own pool and the request ends with it
 */
TEST (work_generator, local_without_peers)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	auto & generator = ctx.start ();
	ASSERT_TRUE (generator.local_enabled ());
	ASSERT_TRUE (generator.enabled ());

	// Ask for work
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());

	// The request ends with work that reaches the difficulty, and nothing is left in progress
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_TIMELY_EQ (5s, generator.size (), 0);
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::request));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::local));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::success));

	// The blocking variant returns the same kind of result
	auto const blocking = generator.generate_blocking (ctx.request (2));
	ASSERT_TRUE (ctx.valid (ctx.request (2), blocking));
}

/*
 * A node that generates no work itself and has a work peer.
 * The peer delivers the work and is not told to cancel anything
 */
TEST (work_generator, peer_delivers)
{
	nano::test::system system;
	result_log results;
	summary_log summaries;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	ctx.add_peer (peer);
	ctx.config.work_threads = 0;
	auto & generator = ctx.start ();
	summaries.listen (generator);
	ASSERT_FALSE (generator.local_enabled ());
	ASSERT_TRUE (generator.enabled ());
	ASSERT_FALSE (generator.enabled (std::nullopt));

	// Ask for work
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());

	// The peer's work ends the request
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_EQ (1, peer.generate_count ());
	ASSERT_EQ (0, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::local));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::success));

	// The summary names the peer as the source
	ASSERT_TIMELY_EQ (5s, summaries.size (), 1);
	auto const summary = summaries.at (0);
	ASSERT_EQ (nano::work_status::success, summary.result.status);
	ASSERT_EQ (peer.peer ().to_string (), summary.source);
	ASSERT_TRUE (summary.bad_peers.empty ());
	ASSERT_EQ (0, peer.cancel_count ());
}

/*
 * A work peer that answers with an HTTP error, on a node that can generate work itself.
 * The failure is seen as one, and the node generates the work without waiting for the fallback delay
 */
TEST (work_generator, peer_http_error_falls_back)
{
	nano::test::system system;
	result_log results;
	summary_log summaries;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::http_error };
	ctx.add_peer (peer);
	// A delay the test would notice if the fallback waited for it, and a rest long enough that the peer is asked only once
	ctx.config.work_generator->local_delay = 30s;
	ctx.config.work_generator->retry_backoff = 30s;
	auto & generator = ctx.start ();
	summaries.listen (generator);

	// Ask for work
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());

	// The request ends with locally generated work
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::http_error));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::local));

	// The summary names the node as the source and the peer as failed
	ASSERT_TIMELY_EQ (5s, summaries.size (), 1);
	auto const summary = summaries.at (0);
	ASSERT_EQ ("local", summary.source);
	ASSERT_EQ (1, summary.bad_peers.size ());
	ASSERT_EQ (peer.peer ().to_string (), summary.bad_peers[0]);
}

/*
 * A work peer that takes the request and never answers, on a node that can generate work itself.
 * The node starts generating when the fallback delay is over, and the peer is told to cancel once the request has ended
 */
TEST (work_generator, peer_silent_falls_back)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };
	ctx.add_peer (peer);
	ctx.config.work_generator->local_delay = 300ms;
	auto & generator = ctx.start ();

	// Ask for work
	auto const start = std::chrono::steady_clock::now ();
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());

	// The request ends with locally generated work, not earlier than the delay
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_GE (std::chrono::steady_clock::now () - start, 300ms);
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::local));

	// The peer that was left working is told to stop
	ASSERT_TIMELY_EQ (5s, peer.cancel_count (), 1);
	ASSERT_EQ (1, peer.generate_count ());
}

/*
 * A node that only has its work peers, and the peer never answers.
 * The request fails when its time is over, and the peer is told to cancel
 */
TEST (work_generator, peer_silent_times_out)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };
	ctx.add_peer (peer);
	ctx.config.work_generator->local_generation = nano::work_local_mode::never;
	ctx.config.work_generator->work_timeout = 500ms;
	auto & generator = ctx.start ();
	ASSERT_FALSE (generator.local_enabled ());

	// Ask for work
	auto const start = std::chrono::steady_clock::now ();
	generator.generate (ctx.request (), results.callback ());

	// The request fails once its time is over
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_EQ (nano::work_status::failed, results.at (0).status);
	ASSERT_GE (std::chrono::steady_clock::now () - start, 500ms);
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::failed));
	ASSERT_EQ (0, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::local));

	// The peer is told to stop, and nothing is left in progress
	ASSERT_TIMELY_EQ (5s, peer.cancel_count (), 1);
	ASSERT_TIMELY_EQ (5s, generator.size (), 0);
}

/*
 * A node that only has its work peers, and the peer fails at first.
 * The peer is asked again after a pause, and the request ends with the work it delivers once it has recovered
 */
TEST (work_generator, peer_recovers)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::refusing };
	ctx.add_peer (peer);
	ctx.config.work_threads = 0;
	ctx.config.work_generator->retry_backoff = 100ms;
	ctx.config.work_generator->retry_backoff_max = 100ms;
	auto & generator = ctx.start ();

	// Ask for work and let the peer refuse it twice
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());
	ASSERT_TIMELY (5s, peer.generate_count () >= 2);
	ASSERT_EQ (0, results.size ());

	// The peer recovers, and the next attempt delivers
	peer.set_mode (nano::test::fake_work_peer_mode::good);
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_GE (ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::refused), 2);
}

/*
 * A node that only has its work peers, and the peers keep failing.
 * The request retries them until its time is over and then fails
 */
TEST (work_generator, peers_fail_until_timeout)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer malformed{ *ctx.io_ctx, nano::test::fake_work_peer_mode::malformed };
	nano::test::fake_work_peer insufficient{ *ctx.io_ctx, nano::test::fake_work_peer_mode::insufficient };
	ctx.add_peer (malformed);
	ctx.add_peer (insufficient);
	ctx.config.work_threads = 0;
	ctx.config.work_generator->work_timeout = 500ms;
	ctx.config.work_generator->retry_backoff = 100ms;
	ctx.config.work_generator->retry_backoff_max = 100ms;
	auto & generator = ctx.start ();

	// Ask for work
	generator.generate (ctx.request (), results.callback ());

	// The request fails, after both peers were asked more than once
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_EQ (nano::work_status::failed, results.at (0).status);
	ASSERT_GE (malformed.generate_count (), 2);
	ASSERT_GE (insufficient.generate_count (), 2);
	ASSERT_GE (ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::malformed), 2);
	ASSERT_GE (ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::insufficient), 2);
}

/*
 * Two work peers, one that delivers and one that never answers.
 * The request ends with the work of the first, and only the peer left working is told to cancel
 */
TEST (work_generator, peer_left_behind_is_cancelled)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer silent{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };
	nano::test::fake_work_peer slow{ *ctx.io_ctx, nano::test::fake_work_peer_mode::slow, /* delay */ 200ms };
	ctx.add_peer (silent);
	ctx.add_peer (slow);
	ctx.config.work_threads = 0;
	auto & generator = ctx.start ();

	// Ask for work
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());

	// Both peers were asked, and the one that answers ends the request
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_EQ (1, silent.generate_count ());
	ASSERT_EQ (1, slow.generate_count ());

	// The silent peer is told to stop, the one that delivered is not
	ASSERT_TIMELY_EQ (5s, silent.cancel_count (), 1);
	ASSERT_EQ (0, slow.cancel_count ());
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::work_cancel));
}

/*
 * A request is cancelled while it waits to ask its failed work peer again.
 * The cancel reaches it: it ends as cancelled, once, and the peer is not asked again
 */
TEST (work_generator, cancel_while_waiting_to_retry)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::refusing };
	ctx.add_peer (peer);
	ctx.config.work_threads = 0;
	// Long enough for the cancel to land in it, short enough that a retry would show up in the test
	ctx.config.work_generator->retry_backoff = 500ms;
	ctx.config.work_generator->retry_backoff_max = 500ms;
	auto & generator = ctx.start ();

	// Ask for work and wait until the peer has refused it
	generator.generate (ctx.request (), results.callback ());
	ASSERT_TIMELY_EQ (5s, ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::refused), 1);
	ASSERT_EQ (0, results.size ());
	ASSERT_EQ (1, generator.size ());

	// Cancel during the wait
	generator.cancel (nano::root{ 1 });
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_EQ (nano::work_status::cancelled, results.at (0).status);
	ASSERT_TIMELY_EQ (5s, generator.size (), 0);

	// The wait runs out and nothing happens: the peer is not asked again, and no second result arrives
	auto const asked = peer.generate_count ();
	ASSERT_ALWAYS_EQ (1s, peer.generate_count (), asked);
	ASSERT_EQ (1, results.size ());
}

/*
 * Two requests for the same root, one that stays on the node and one that a work peer answers.
 * The peer's work ends its own request only, the local one keeps running until it is cancelled itself
 */
TEST (work_generator, requests_for_same_root_are_independent)
{
	nano::test::system system;
	result_log local_results;
	result_log remote_results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	ctx.add_peer (peer);
	auto & generator = ctx.start ();

	// A request that stays on the node and cannot finish
	auto local = ctx.request ();
	local.difficulty = unreachable_difficulty;
	local.peers = std::nullopt;
	generator.generate (local, local_results.callback ());
	ASSERT_TIMELY_EQ (5s, ctx.pool.size (), 1);

	// A request for the same root that the peer answers
	auto const remote = ctx.request ();
	generator.generate (remote, remote_results.callback ());
	ASSERT_TIMELY_EQ (5s, remote_results.size (), 1);
	ASSERT_TRUE (ctx.valid (remote, remote_results.at (0)));

	// The local request is still being generated
	ASSERT_ALWAYS_EQ (500ms, local_results.size (), 0);
	ASSERT_EQ (1, ctx.pool.size ());
	ASSERT_EQ (1, generator.size ());

	// A cancel for the root ends it
	generator.cancel (local.root);
	ASSERT_TIMELY_EQ (5s, local_results.size (), 1);
	ASSERT_EQ (nano::work_status::cancelled, local_results.at (0).status);
	ASSERT_TIMELY_EQ (5s, ctx.pool.size (), 0);
}

/*
 * Callbacks that call back into the generator: reading its size, cancelling, and asking for more work.
 * Neither a cancel nor a stop holds a lock while a callback runs, so a callback is free to do all of it
 */
TEST (work_generator, callback_reentry)
{
	nano::test::system system;
	result_log followup_results;
	generator_context ctx;
	auto & generator = ctx.start ();

	// A request that cannot finish, whose callback reads the size, cancels another root and asks for work that can finish
	std::atomic<int> answers{ 0 };
	auto request = ctx.request (1);
	request.difficulty = unreachable_difficulty;
	generator.generate (request, [&] (nano::work_result const & result) {
		ASSERT_EQ (nano::work_status::cancelled, result.status);
		generator.size ();
		generator.cancel (nano::root{ 3 });
		generator.generate (ctx.request (2), followup_results.callback ());
		++answers;
	});
	ASSERT_TIMELY_EQ (5s, ctx.pool.size (), 1);

	// Cancelling runs the callback, and the request it made is answered
	generator.cancel (request.root);
	ASSERT_TIMELY_EQ (5s, answers, 1);
	ASSERT_TIMELY_EQ (5s, followup_results.size (), 1);
	ASSERT_TRUE (ctx.valid (ctx.request (2), followup_results.at (0)));

	// The same from a stop: the callback's own request is refused and answered on the spot
	std::atomic<int> stopped_answers{ 0 };
	generator.generate (request, [&] (nano::work_result const & result) {
		ASSERT_EQ (nano::work_status::cancelled, result.status);
		generator.size ();
		generator.generate (ctx.request (4), followup_results.callback ());
		++stopped_answers;
	});
	ASSERT_TIMELY_EQ (5s, ctx.pool.size (), 1);
	generator.stop ();
	ASSERT_EQ (1, stopped_answers);
	ASSERT_EQ (2, followup_results.size ());
	ASSERT_EQ (nano::work_status::cancelled, followup_results.at (1).status);
}

/*
 * Stopping the generator with requests in different states: generating locally, waiting for a silent peer, and generating locally while its failed peer rests.
 * Each request ends as cancelled exactly once before the stop returns, nothing is left running, and later requests are refused
 */
TEST (work_generator, stop)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer silent{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };
	nano::test::fake_work_peer refusing{ *ctx.io_ctx, nano::test::fake_work_peer_mode::refusing };
	ctx.config.work_peers.emplace_back (silent.peer ().address, silent.port ());
	ctx.config.secondary_work_peers.clear ();
	ctx.config.secondary_work_peers.emplace_back (refusing.peer ().address, refusing.port ());
	ctx.config.work_generator->local_delay = 30s;
	ctx.config.work_generator->retry_backoff = 30s;
	auto & generator = ctx.start ();

	// One request generating locally, one held by the silent peer, one generating locally after its peer refused
	auto local = ctx.request (1);
	local.difficulty = unreachable_difficulty;
	local.peers = std::nullopt;
	generator.generate (local, results.callback ());
	generator.generate (ctx.request (2), results.callback ());
	auto retrying = ctx.request (3);
	retrying.difficulty = unreachable_difficulty;
	retrying.peers = nano::work_peer_group::secondary;
	generator.generate (retrying, results.callback ());
	ASSERT_TIMELY_EQ (5s, silent.generate_count (), 1);
	ASSERT_TIMELY_EQ (5s, refusing.generate_count (), 1);
	ASSERT_TIMELY_EQ (5s, ctx.pool.size (), 2);
	ASSERT_EQ (3, generator.size ());
	ASSERT_EQ (0, results.size ());

	// Stop: every request has its result by the time the call returns
	generator.stop ();
	ASSERT_EQ (3, results.size ());
	ASSERT_EQ (3, results.count (nano::work_status::cancelled));
	ASSERT_EQ (0, generator.size ());
	ASSERT_EQ (0, ctx.pool.size ());

	// A request after the stop is refused on the spot
	generator.generate (ctx.request (4), results.callback ());
	ASSERT_EQ (4, results.size ());
	ASSERT_EQ (nano::work_status::cancelled, results.at (3).status);

	// Stopping again is harmless, and no result arrives late
	generator.stop ();
	ASSERT_ALWAYS_EQ (300ms, results.size (), 4);
}

/*
 * Requests come in from several threads while the generator is being stopped.
 * Whichever way a request and the stop are ordered, the request gets exactly one result, and all of them have theirs once the stop and the threads are done
 */
TEST (work_generator, stop_while_requests_come_in)
{
	size_t const rounds = 50;
	size_t const threads_count = 4;
	size_t const requests_per_thread = 5;

	for (size_t round = 0; round < rounds; ++round)
	{
		result_log results;
		generator_context ctx;
		auto & generator = ctx.start ();

		// Every thread waits at the gate, then asks for work that cannot finish on its own
		std::latch gate{ 1 };
		std::vector<std::thread> threads;
		for (size_t thread = 0; thread < threads_count; ++thread)
		{
			threads.emplace_back ([&, thread] {
				gate.wait ();
				for (size_t count = 0; count < requests_per_thread; ++count)
				{
					auto request = ctx.request (1 + thread * requests_per_thread + count);
					request.difficulty = unreachable_difficulty;
					generator.generate (request, results.callback ());
				}
			});
		}

		// Open the gate and stop at the same time
		gate.count_down ();
		generator.stop ();

		for (auto & thread : threads)
		{
			thread.join ();
		}

		// Requests that made it in were ended by the stop, the others were refused: none is left without a result
		ASSERT_EQ (threads_count * requests_per_thread, results.size ());
		ASSERT_EQ (threads_count * requests_per_thread, results.count (nano::work_status::cancelled));
		ASSERT_EQ (0, generator.size ());
		ASSERT_EQ (0, ctx.pool.size ());
	}
}

/*
 * The work peer lists change while requests run and other threads read them.
 * The lists are the generator's own and every access takes its lock, so the requests keep being answered
 */
TEST (work_generator, peers_change_while_requests_run)
{
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	auto & generator = ctx.start ();

	// One thread adds and clears peers, another one reads the list
	std::atomic<bool> done{ false };
	std::thread writer{ [&] {
		while (!done)
		{
			generator.add_peer (peer.peer ());
			generator.add_peer (nano::work_peer{ "127.0.0.1", /* port */ 1 });
			generator.clear_peers ();
		}
	} };
	std::atomic<size_t> listed{ 0 };
	std::thread reader{ [&] {
		while (!done)
		{
			listed += generator.peers ().size ();
		}
	} };

	// Meanwhile requests are answered, by the peer or by the node itself
	for (uint64_t count = 0; count < 20; ++count)
	{
		auto const request = ctx.request (1 + count);
		auto const result = generator.generate_blocking (request);
		ASSERT_TRUE (ctx.valid (request, result));
	}

	done = true;
	writer.join ();
	reader.join ();
}

/*
 * The peer groups and the `never` mode.
 * A request to the secondary group asks the secondary peers only, a request that has to stay on the node fails when the node never generates,
 * and a request with nothing that could generate it fails right away
 */
TEST (work_generator, groups_and_never_mode)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer primary{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	nano::test::fake_work_peer secondary{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	ctx.add_peer (primary);
	ctx.config.secondary_work_peers.clear ();
	ctx.config.secondary_work_peers.emplace_back (secondary.peer ().address, secondary.port ());
	ctx.config.work_generator->local_generation = nano::work_local_mode::never;
	auto & generator = ctx.start ();
	ASSERT_FALSE (generator.local_enabled ());
	ASSERT_EQ (1, generator.peers (nano::work_peer_group::primary).size ());
	ASSERT_EQ (1, generator.peers (nano::work_peer_group::secondary).size ());

	// A request to the secondary group
	auto request = ctx.request (1);
	request.peers = nano::work_peer_group::secondary;
	ASSERT_TRUE (ctx.valid (request, generator.generate_blocking (request)));
	ASSERT_EQ (0, primary.generate_count ());
	ASSERT_EQ (1, secondary.generate_count ());

	// A request to the primary group
	ASSERT_TRUE (ctx.valid (ctx.request (2), generator.generate_blocking (ctx.request (2))));
	ASSERT_EQ (1, primary.generate_count ());
	ASSERT_EQ (1, secondary.generate_count ());

	// A request that has to stay on the node has nothing to generate it
	auto local = ctx.request (3);
	local.peers = std::nullopt;
	ASSERT_FALSE (generator.enabled (std::nullopt));
	ASSERT_EQ (nano::work_status::failed, generator.generate_blocking (local).status);

	// Neither has a request to a group that lost its peers
	generator.clear_peers (nano::work_peer_group::primary);
	ASSERT_FALSE (generator.enabled (nano::work_peer_group::primary));
	ASSERT_EQ (nano::work_status::failed, generator.generate_blocking (ctx.request (4)).status);
	ASSERT_EQ (0, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::local));
}

/*
 * The `always` mode with a work peer that never answers.
 * The node generates from the start, so the request ends without waiting for the fallback delay
 */
TEST (work_generator, always_mode)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };
	ctx.add_peer (peer);
	ctx.config.work_generator->local_generation = nano::work_local_mode::always;
	ctx.config.work_generator->local_delay = 30s;
	auto & generator = ctx.start ();

	// Ask for work
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());

	// The node's own work ends the request
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::local));
}

/*
 * A node that only has its work peers, and the peer never answers.
 * Each attempt ends when the peer's time for it is over, the peer is told to cancel and asked again after a rest, until the request's own time is over
 */
TEST (work_generator, peer_timeout_ends_attempt)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };
	ctx.add_peer (peer);
	ctx.config.work_threads = 0;
	ctx.config.work_generator->peer_timeout = 200ms;
	ctx.config.work_generator->work_timeout = 1s;
	ctx.config.work_generator->retry_backoff = 100ms;
	ctx.config.work_generator->retry_backoff_max = 100ms;
	auto & generator = ctx.start ();

	// Ask for work
	auto const start = std::chrono::steady_clock::now ();
	generator.generate (ctx.request (), results.callback ());

	// The request fails when its time is over, after more than one attempt has run out
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_EQ (nano::work_status::failed, results.at (0).status);
	ASSERT_GE (std::chrono::steady_clock::now () - start, 1s);
	ASSERT_GE (peer.generate_count (), 2);
	ASSERT_GE (ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::timeout), 2);

	// The peer was told to stop after the attempts it did not answer
	ASSERT_TIMELY (5s, peer.cancel_count () >= 2);
}

/*
 * A request that the node is generating work for itself, with a work peer that takes longer than the request's time limit.
 * The limit ends neither the request nor its exchange with the peer, and the peer's work ends the request
 */
TEST (work_generator, peers_kept_while_generating)
{
	nano::test::system system;
	result_log results;
	generator_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::slow, /* delay */ 600ms };
	ctx.add_peer (peer);
	ctx.config.work_generator->local_generation = nano::work_local_mode::always;
	ctx.config.work_generator->work_timeout = 200ms;
	auto & generator = ctx.start ();

	// Keep the pool busy with work that cannot finish, so that the request's own generation waits behind it
	nano::root const blocker{ 100 };
	ctx.pool.generate (nano::work_version::work_1, blocker, unreachable_difficulty, [] (std::optional<uint64_t> const &) {});
	ASSERT_EQ (1, ctx.pool.size ());

	// Ask for work
	auto const start = std::chrono::steady_clock::now ();
	auto const request = ctx.request ();
	generator.generate (request, results.callback ());

	// The peer delivers after the limit has passed, and its work ends the request
	ASSERT_TIMELY_EQ (5s, results.size (), 1);
	ASSERT_TRUE (ctx.valid (request, results.at (0)));
	ASSERT_GE (std::chrono::steady_clock::now () - start, 600ms);
	ASSERT_EQ (1, peer.generate_count ());
	ASSERT_EQ (0, ctx.stats.count (nano::stat::type::work_generator, nano::stat::detail::failed));

	// Free the pool again
	ctx.pool.cancel (blocker);
	ASSERT_TIMELY_EQ (5s, ctx.pool.size (), 0);
}

/*
 * Two work peers that are asked one at first.
 * The requests alternate between the peers, one peer generates for each request, and neither is told to cancel
 */
TEST (work_generator, one_peer_at_a_time)
{
	generator_context ctx;
	nano::test::fake_work_peer first{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	nano::test::fake_work_peer second{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	ctx.add_peer (first);
	ctx.add_peer (second);
	ctx.config.work_threads = 0;
	ctx.config.work_generator->peer_selection = nano::work_peer_selection::one;
	// Longer than any of the requests takes
	ctx.config.work_generator->peer_delay = 30s;
	auto & generator = ctx.start ();

	// Four requests, one after the other
	for (uint64_t count = 0; count < 4; ++count)
	{
		auto const request = ctx.request (1 + count);
		ASSERT_TRUE (ctx.valid (request, generator.generate_blocking (request)));
	}

	// Each peer generated for two of them
	ASSERT_EQ (2, first.generate_count ());
	ASSERT_EQ (2, second.generate_count ());

	// No peer was left working, so none was told to stop
	ASSERT_EQ (0, first.cancel_count ());
	ASSERT_EQ (0, second.cancel_count ());
	ASSERT_EQ (0, ctx.stats.count (nano::stat::type::work_generator_peer, nano::stat::detail::work_cancel));
}

/*
 * Two work peers asked one at first, and the one whose turn it is refuses.
 * The request moves on to the other peer at once, and the next request goes to that peer directly while the first one rests
 */
TEST (work_generator, one_peer_failover)
{
	generator_context ctx;
	nano::test::fake_work_peer refusing{ *ctx.io_ctx, nano::test::fake_work_peer_mode::refusing };
	nano::test::fake_work_peer good{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	ctx.add_peer (refusing);
	ctx.add_peer (good);
	ctx.config.work_threads = 0;
	ctx.config.work_generator->peer_selection = nano::work_peer_selection::one;
	// A delay and a rest that outlast the test
	ctx.config.work_generator->peer_delay = 30s;
	ctx.config.work_generator->retry_backoff = 30s;
	auto & generator = ctx.start ();

	// The first request is refused by the first peer and answered by the second
	ASSERT_TRUE (ctx.valid (ctx.request (1), generator.generate_blocking (ctx.request (1))));
	ASSERT_EQ (1, refusing.generate_count ());
	ASSERT_EQ (1, good.generate_count ());

	// The second request does not go to the resting peer
	ASSERT_TRUE (ctx.valid (ctx.request (2), generator.generate_blocking (ctx.request (2))));
	ASSERT_EQ (1, refusing.generate_count ());
	ASSERT_EQ (2, good.generate_count ());
}

/*
 * Two work peers asked one at first, and the one whose turn it is never answers.
 * When `peer_delay` has passed the other peer is asked as well and delivers, and the silent one is told to cancel
 */
TEST (work_generator, one_peer_delay)
{
	nano::test::system system;
	generator_context ctx;
	nano::test::fake_work_peer silent{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };
	nano::test::fake_work_peer good{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };
	ctx.add_peer (silent);
	ctx.add_peer (good);
	ctx.config.work_threads = 0;
	ctx.config.work_generator->peer_selection = nano::work_peer_selection::one;
	ctx.config.work_generator->peer_delay = 300ms;
	auto & generator = ctx.start ();

	// Ask for work
	auto const start = std::chrono::steady_clock::now ();
	auto const request = ctx.request ();
	auto const result = generator.generate_blocking (request);

	// The second peer delivered, after the first one had its time
	ASSERT_TRUE (ctx.valid (request, result));
	ASSERT_GE (std::chrono::steady_clock::now () - start, 300ms);
	ASSERT_EQ (1, silent.generate_count ());
	ASSERT_EQ (1, good.generate_count ());

	// The silent peer, which was still working, is told to stop
	ASSERT_TIMELY_EQ (5s, silent.cancel_count (), 1);
}
