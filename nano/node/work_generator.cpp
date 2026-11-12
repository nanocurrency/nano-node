#include <nano/lib/config.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/work_generator.hpp>

#include <algorithm>
#include <future>

/*
 * work_generator
 */

nano::work_generator::work_generator (nano::node_config const & node_config_a, nano::work_pool & pool_a, asio::io_context & io_ctx_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ *node_config_a.work_generator },
	thresholds{ node_config_a.network_params.work },
	pool{ pool_a },
	stats{ stats_a },
	logger{ logger_a },
	client{ thresholds, { .connect = config.connect_timeout, .cancel = config.cancel_timeout }, logger_a },
	index{ index_limits (node_config_a, pool_a) },
	strand{ io_ctx_a.get_executor () },
	task{ strand }
{
	for (auto const & [address, port] : node_config_a.work_peers)
	{
		index.add_peer (nano::work_peer_group::primary, nano::work_peer{ address, port });
	}

	for (auto const & [address, port] : node_config_a.secondary_work_peers)
	{
		index.add_peer (nano::work_peer_group::secondary, nano::work_peer{ address, port });
	}
}

nano::work_generator::~work_generator ()
{
	debug_assert (!task.joinable () || task.ready ());
	debug_assert (index.exchange_count () == 0);
	debug_assert (index.local_count () == 0);
}

auto nano::work_generator::index_limits (nano::node_config const & node_config, nano::work_pool const & pool) -> nano::work_generator_index::limits
{
	auto const & config = *node_config.work_generator;

	nano::work_generator_index::limits limits;
	limits.local_generation = config.local_generation;
	limits.local_available = node_config.work_threads > 0 || pool.opencl;
	limits.local_delay = config.local_delay;
	limits.peer_selection = config.peer_selection;
	limits.peer_delay = config.peer_delay;
	limits.peer_timeout = config.peer_timeout;
	limits.work_timeout = config.work_timeout;
	limits.retry_backoff = config.retry_backoff;
	limits.retry_backoff_max = config.retry_backoff_max;
	return limits;
}

void nano::work_generator::stop ()
{
	// Waits for the loop, which runs the callbacks: a callback cannot stop the generator
	debug_assert (!strand.running_in_this_thread ());

	nano::work_generator_index::actions actions;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };

		// Refuse new requests in the same step that ends the ones in progress
		stopped = true;
		actions = index.cancel_all (std::chrono::steady_clock::now ());
	}

	if (task.joinable ())
	{
		task.cancel ();
		task.join ();
	}

	// End the exchanges with work peers, the loop is gone and starts no new ones
	std::deque<nano::work_generator_index::exchange> exchanges;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		exchanges = index.list_exchanges ();
	}

	for (auto const & exchange : exchanges)
	{
		if (exchange.task)
		{
			exchange.task->cancel ();
		}
	}

	for (auto const & exchange : exchanges)
	{
		if (exchange.task)
		{
			exchange.task->join ();
		}
	}

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		index.erase_exchanges_if ([] (auto const &) {
			return true;
		});
	}

	// End local generation and wait until the work pool has answered for every item, its callbacks use this object
	for (auto const pool_id : actions.abandoned_locals)
	{
		pool.cancel_item (pool_id);
	}

	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		condition.wait (lock, [this] {
			return index.local_count () == 0;
		});
	}

	for (auto const & completion : actions.completions)
	{
		complete (completion);
	}
}

void nano::work_generator::generate (nano::work_request const & request, nano::work_callback callback)
{
	stats.inc (nano::stat::type::work_generator, nano::stat::detail::request);

	{
		nano::lock_guard<nano::mutex> lock{ mutex };

		if (!stopped)
		{
			index.insert (request, callback, std::chrono::steady_clock::now ());

			// The loop starts with the first request
			if (!task.joinable ())
			{
				task = nano::async::task (strand, [this] (nano::async::condition & condition) {
					return run (condition);
				});
			}

			notify ();
			return;
		}
	}

	// Stopped, the request ends here
	nano::work_generator_index::completion refused;
	refused.callback = std::move (callback);
	refused.summary.request = request;
	refused.summary.result.status = nano::work_status::cancelled;
	complete (refused);
}

nano::work_result nano::work_generator::generate_blocking (nano::work_request const & request)
{
	debug_assert (!strand.running_in_this_thread ());

	auto promise = std::make_shared<std::promise<nano::work_result>> ();
	auto future = promise->get_future ();

	generate (request, [promise] (nano::work_result const & result) {
		promise->set_value (result);
	});

	return future.get ();
}

void nano::work_generator::cancel (nano::root const & root)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	if (index.cancel (root) > 0)
	{
		notify ();
	}
}

bool nano::work_generator::local_enabled () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.local_enabled ();
}

bool nano::work_generator::enabled (std::optional<nano::work_peer_group> const & group) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.enabled (group);
}

void nano::work_generator::add_peer (nano::work_peer const & peer, nano::work_peer_group group)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	index.add_peer (group, peer);
	notify ();
}

void nano::work_generator::clear_peers (nano::work_peer_group group)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	index.clear_peers (group);
	notify ();
}

std::deque<nano::work_peer> nano::work_generator::peers (nano::work_peer_group group) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.peers (group);
}

size_t nano::work_generator::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.size ();
}

nano::container_info nano::work_generator::container_info () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.container_info ();
}

void nano::work_generator::notify ()
{
	debug_assert (!mutex.try_lock ());

	dirty = true;
	condition.notify_all ();
	if (task.joinable ())
	{
		task.notify ();
	}
}

asio::awaitable<void> nano::work_generator::run (nano::async::condition & condition)
{
	// The loop ends by seeing `stopped`, a cancellation only wakes it
	co_await asio::this_coro::throw_if_cancelled (false);

	while (true)
	{
		auto const result = run_one ();
		if (result == run_result::stopped)
		{
			co_return;
		}

		// Something came in while the round was running, a wakeup sent from this thread is not seen by the wait
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			if (dirty)
			{
				continue;
			}
		}

		if (result == run_result::busy)
		{
			co_await condition.wait_for (poll_interval);
		}
		else
		{
			co_await condition.wait ();
		}
	}
}

auto nano::work_generator::run_one () -> run_result
{
	debug_assert (strand.running_in_this_thread ());

	nano::unique_lock<nano::mutex> lock{ mutex };

	if (stopped)
	{
		return run_result::stopped;
	}

	dirty = false;

	auto const now = std::chrono::steady_clock::now ();

	// A task that ended without reporting was cancelled before it got to run
	for (auto const & exchange : index.list_exchanges ())
	{
		if (!exchange.finished && exchange.task && exchange.task->ready ())
		{
			index.exchange_finished (exchange.id, nano::work_peer_response{ nano::work_peer_status::cancelled }, now);
		}
	}

	auto const ended = index.erase_exchanges_if ([] (auto const & exchange) {
		return exchange.finished && (!exchange.task || exchange.task->ready ());
	});

	auto const actions = index.advance (now);

	// A task does nothing before its first suspension, so it is safe to start one with the mutex held
	for (auto const & start : actions.exchanges)
	{
		index.attach (start.id, std::make_shared<nano::async::task> (strand, run_exchange (start)));
	}

	for (auto const & start : actions.cancels)
	{
		index.attach (start.id, std::make_shared<nano::async::task> (strand, run_cancel (start)));
	}

	for (auto const & abandoned : actions.abandoned)
	{
		abandoned->cancel ();
	}

	auto const result = index.size () > 0 ? run_result::busy : run_result::idle;

	lock.unlock ();

	// The work pool runs callbacks that take the mutex, some of them before these calls return
	for (auto const pool_id : actions.abandoned_locals)
	{
		pool.cancel_item (pool_id);
	}

	for (auto const & start : actions.locals)
	{
		start_local (start);
	}

	for (auto const & completion : actions.completions)
	{
		complete (completion);
	}

	return result;
}

asio::awaitable<void> nano::work_generator::run_exchange (nano::work_generator_index::exchange_start start)
{
	// The exchange always reports how it ended, a cancellation ends it through the client's result
	co_await asio::this_coro::throw_if_cancelled (false);

	// Let the loop record the task before anything can report
	co_await asio::post (strand, asio::use_awaitable);

	auto const response = co_await client.generate (start.peer, start.request, start.deadline);

	stats.inc (nano::stat::type::work_generator_peer, to_stat_detail (response.status));

	auto const failed = response.status != nano::work_peer_status::success && response.status != nano::work_peer_status::cancelled;
	if (failed)
	{
		logger.warn (nano::log::type::work_generator, "Work peer {} failed to generate work for root {}: {}",
		start.peer.to_string (),
		start.request.root.to_string (),
		to_string (response.status));
	}

	nano::lock_guard<nano::mutex> lock{ mutex };
	index.exchange_finished (start.id, response, std::chrono::steady_clock::now ());
	notify ();
}

asio::awaitable<void> nano::work_generator::run_cancel (nano::work_generator_index::cancel_start start)
{
	co_await asio::this_coro::throw_if_cancelled (false);
	co_await asio::post (strand, asio::use_awaitable);

	stats.inc (nano::stat::type::work_generator_peer, nano::stat::detail::work_cancel);

	co_await client.cancel (start.peer, start.root);
}

void nano::work_generator::start_local (nano::work_generator_index::local_start const & start)
{
	stats.inc (nano::stat::type::work_generator, nano::stat::detail::local);

	auto const pool_id = pool.generate (start.work.version, start.work.root, start.work.difficulty, [this, request = start.request] (std::optional<uint64_t> const & work) {
		// Notifies with the mutex held: `stop` returns once the last item is answered, and nothing here may touch the generator after that
		nano::lock_guard<nano::mutex> lock{ mutex };
		index.local_finished (request, work);
		notify ();
	});

	bool wanted{ false };
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		wanted = index.local_started (start.request, pool_id);
	}

	// The request ended while its item was being queued
	if (!wanted)
	{
		pool.cancel_item (pool_id);
	}
}

void nano::work_generator::complete (nano::work_generator_index::completion const & completion)
{
	auto const & summary = completion.summary;

	stats.inc (nano::stat::type::work_generator, to_stat_detail (summary.result.status));

	switch (summary.result.status)
	{
		case nano::work_status::success:
		{
			auto const multiplier = nano::difficulty::to_multiplier (summary.request.difficulty, thresholds.threshold_base (summary.request.version));

			logger.info (nano::log::type::work_generator, "Work generation for {}, with a threshold difficulty of {} (multiplier {}x) complete: {} ms ({})",
			summary.request.root.to_string (),
			nano::to_string_hex (summary.request.difficulty),
			nano::to_string (multiplier, /* precision */ 2),
			summary.duration.count (),
			summary.source);
		}
		break;
		case nano::work_status::cancelled:
		{
			logger.info (nano::log::type::work_generator, "Work generation for {} was cancelled after {} ms",
			summary.request.root.to_string (),
			summary.duration.count ());
		}
		break;
		case nano::work_status::failed:
		{
			logger.warn (nano::log::type::work_generator, "Work generation for {} failed after {} ms, nothing was left that could generate it",
			summary.request.root.to_string (),
			summary.duration.count ());
		}
		break;
	}

	if (completion.callback)
	{
		completion.callback (summary.result);
	}

	request_finished.notify (summary);
}

nano::stat::detail nano::to_stat_detail (nano::work_status status)
{
	return nano::enum_convert<nano::stat::detail> (status);
}

/*
 * work_generator_config
 */

nano::work_generator_config::work_generator_config (nano::network_constants const & network)
{
	if (network.is_dev_network ())
	{
		local_delay = 1s;
		peer_delay = 1s;
		connect_timeout = 2s;
		cancel_timeout = 1s;
		retry_backoff = 100ms;
		retry_backoff_max = 1s;
	}
}

nano::error nano::work_generator_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("local_generation", std::string{ to_string (local_generation) }, "When this node generates work itself, on its work threads or OpenCL device, for requests that work peers can serve as well. Without work peers the node always generates the work itself, unless this is `never`.\n`fallback`: the work peers are asked first, and the node starts generating once they have all failed or `local_delay` has passed without work.\n`always`: the node generates right away, while the work peers are asked.\n`never`: only the work peers generate work.\ntype:string,{fallback,always,never}");
	toml.put ("local_delay", local_delay.count (), "With `local_generation = \"fallback\"`, how long the work peers get to deliver before the node starts generating the work itself as well.\ntype:milliseconds");
	toml.put ("peer_selection", std::string{ to_string (peer_selection) }, "How the work peers are asked for a request.\n`all`: every work peer is asked at once and the first work that arrives is used, the other work peers are told to cancel.\n`one`: one work peer is asked at first, and each request starts with the next work peer in turn. The next one is asked as well when a work peer fails or `peer_delay` has passed without work.\ntype:string,{all,one}");
	toml.put ("peer_delay", peer_delay.count (), "With `peer_selection = \"one\"`, how long a work peer gets to deliver before the next work peer is asked as well.\ntype:milliseconds");
	toml.put ("peer_timeout", peer_timeout.count (), "How long one work peer gets to answer one attempt. A work peer that has not answered by then counts as failed for the attempt and is asked again after a pause.\ntype:milliseconds");
	toml.put ("work_timeout", work_timeout.count (), "How long a work request waits for work before it fails. It applies while the request depends on the work peers alone: a request the node is generating the work for itself does not fail, and its work peers keep being asked.\ntype:milliseconds");

	return toml.get_error ();
}

nano::error nano::work_generator_config::deserialize (nano::tomlconfig & toml)
{
	if (toml.has_key ("local_generation"))
	{
		auto const text = toml.get<std::string> ("local_generation");
		if (auto const mode = nano::enum_try_parse<nano::work_local_mode> (text))
		{
			local_generation = *mode;
		}
		else
		{
			toml.get_error ().set ("Unknown work_generator.local_generation: \"" + text + "\", expected `fallback`, `always` or `never`");
		}
	}

	if (toml.has_key ("peer_selection"))
	{
		auto const text = toml.get<std::string> ("peer_selection");
		if (auto const selection = nano::enum_try_parse<nano::work_peer_selection> (text))
		{
			peer_selection = *selection;
		}
		else
		{
			toml.get_error ().set ("Unknown work_generator.peer_selection: \"" + text + "\", expected `all` or `one`");
		}
	}

	toml.get_duration ("local_delay", local_delay);
	toml.get_duration ("peer_delay", peer_delay);
	toml.get_duration ("peer_timeout", peer_timeout);
	toml.get_duration ("work_timeout", work_timeout);

	return toml.get_error ();
}
