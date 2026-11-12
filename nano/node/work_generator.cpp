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
	toml.put ("local_generation", std::string{ to_string (local_generation) }, "When this node generates work itself, on its work threads or OpenCL device, for requests that work peers can serve as well. Without work peers the node always generates the work itself, unless this is `never`.\n`fallback`: the work peers are asked, and the node starts generating once every one of them has failed or has not answered within `peer_timeout`.\n`delayed`: like `fallback`, and the node also starts generating once `local_delay` has passed without work, while the work peers keep working.\n`always`: the node generates right away, while the work peers are asked.\n`never`: only the work peers generate work.\ntype:string,{fallback,delayed,always,never}");
	toml.put ("local_delay", local_delay.count (), "With `local_generation = \"delayed\"`, how long the work peers get to deliver before the node starts generating the work itself as well.\ntype:milliseconds");
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
			toml.get_error ().set ("Unknown work_generator.local_generation: \"" + text + "\", expected `fallback`, `delayed`, `always` or `never`");
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

/*
 * work_generator_index
 */

nano::work_generator_index::work_generator_index (limits const & limits_a) :
	config{ limits_a }
{
}

nano::root nano::work_generator_index::request_entry::root () const
{
	return work.root;
}

auto nano::work_generator_index::insert (nano::work_request const & work, nano::work_callback callback, time_point now) -> std::optional<id_t>
{
	if (stopped_m)
	{
		return std::nullopt;
	}

	auto const id = next_id++;
	requests.push_back (request_entry{ id, work, std::move (callback), now });
	return id;
}

size_t nano::work_generator_index::cancel (nano::root const & root)
{
	auto & by_root = requests.get<tag_root> ();
	auto const range = by_root.equal_range (root);

	size_t count{ 0 };
	for (auto it = range.first; it != range.second; ++it)
	{
		by_root.modify (it, [] (request_entry & entry) {
			entry.cancelled = true;
		});
		++count;
	}

	return count;
}

auto nano::work_generator_index::advance (time_point now) -> actions
{
	actions result;

	// The exchanges are recorded here and not when they were asked for, so that none exists without the generator knowing its id
	for (auto & cancel : pending_cancels)
	{
		cancel.id = next_id++;
		exchanges.push_back (exchange{ cancel.id, /* request */ 0, cancel.root, cancel.peer, nullptr, /* finished */ true });
		result.cancels.push_back (cancel);
	}

	pending_cancels.clear ();

	for (auto it = requests.begin (); it != requests.end ();)
	{
		auto const current = it++;

		if (auto const status = advance_request (*current, now, result))
		{
			finish (*current, *status, now, result);
		}
	}

	return result;
}

auto nano::work_generator_index::advance_request (request_entry const & request, time_point now, actions & result) -> std::optional<nano::work_status>
{
	if (request.cancelled)
	{
		return nano::work_status::cancelled;
	}

	if (request.found)
	{
		return nano::work_status::success;
	}

	// A request that depends on the work peers alone gives up when its time is over, one this node generates for goes on
	auto local_running = locals.contains (request.id);
	auto const local_possible = local_running || (!request.local_spent && local_enabled ());
	auto const expired = now >= request.start + config.work_timeout;
	if (!local_possible && expired)
	{
		return nano::work_status::failed;
	}

	// Ask the work peers of the request's group the way the config says
	bool peers_resting{ false };
	if (request.work.peers)
	{
		switch (config.peer_selection)
		{
			case nano::work_peer_selection::all:
				peers_resting = ask_all_peers (request, now, result);
				break;
			case nano::work_peer_selection::one:
				peers_resting = ask_next_peer (request, now, result);
				break;
		}
	}

	auto const in_flight = count_exchanges (request.id);

	// Generate on this node once every work peer has failed for the request, or when the mode does not wait for that
	if (!local_running && !request.local_spent && local_enabled ())
	{
		auto const always = config.local_generation == nano::work_local_mode::always;
		auto const delayed = config.local_generation == nano::work_local_mode::delayed;
		auto const waited = now >= request.start + config.local_delay;
		auto const exhausted = peers_exhausted (request, now);
		if (exhausted || always || (delayed && waited))
		{
			locals.insert (local_entry{ request.id });
			result.locals.push_back (local_start{ request.id, request.work });
			local_running = true;
		}
	}

	// Nothing is working on the request, and no work peer is left to ask later
	if (!local_running && in_flight == 0 && !peers_resting)
	{
		return nano::work_status::failed;
	}

	return std::nullopt;
}

bool nano::work_generator_index::ask_all_peers (request_entry const & request, time_point now, actions & result)
{
	bool resting{ false };

	// Every work peer that is not being asked already and is not left alone after a failure
	for (auto const & entry : peers_of (*request.work.peers))
	{
		if (has_exchange (request.id, entry.peer))
		{
			continue;
		}

		if (now < entry.retry_timestamp)
		{
			resting = true;
			continue;
		}

		auto const id = next_id++;
		exchanges.push_back (exchange{ id, request.id, request.work.root, entry.peer });
		result.exchanges.push_back (exchange_start{ id, request.work, entry.peer, now + config.peer_timeout });
	}

	return resting;
}

bool nano::work_generator_index::ask_next_peer (request_entry const & request, time_point now, actions & result)
{
	// One more peer is due when none is working for the request, or when the last one asked has had its time
	auto const working = count_exchanges (request.id);
	auto const waited = now >= request.asked_timestamp + config.peer_delay;
	if (working > 0 && !waited)
	{
		return false;
	}

	auto const group = *request.work.peers;
	auto const & peers = peers_of (group);
	auto & rotation = rotation_of (group);

	// A new request starts one peer further than the last new request to its group did, a request that asked before goes on from there
	auto const first = request.next_peer.value_or (rotation);
	bool resting{ false };

	for (size_t offset = 0; offset < peers.size (); ++offset)
	{
		auto const position = (first + offset) % peers.size ();
		auto const & entry = peers[position];

		if (now < entry.retry_timestamp)
		{
			resting = true;
			continue;
		}

		// A peer asked earlier keeps working for the request
		if (has_exchange (request.id, entry.peer))
		{
			continue;
		}

		auto const id = next_id++;
		exchanges.push_back (exchange{ id, request.id, request.work.root, entry.peer });
		result.exchanges.push_back (exchange_start{ id, request.work, entry.peer, now + config.peer_timeout });

		if (!request.next_peer)
		{
			++rotation;
		}

		auto & by_id = requests.get<tag_id> ();
		by_id.modify (by_id.find (request.id), [position, now] (request_entry & asking) {
			asking.next_peer = position + 1;
			asking.asked_timestamp = now;
		});

		break;
	}

	return resting;
}

void nano::work_generator_index::finish (request_entry const & request, nano::work_status status, time_point now, actions & result)
{
	// The work pool item is cancelled by its id, which leaves other requests for the root alone
	if (auto const local = locals.find (request.id); local != locals.end ())
	{
		if (local->pool_id)
		{
			result.abandoned_locals.push_back (*local->pool_id);
		}
		else
		{
			locals.modify (local, [] (local_entry & entry) {
				entry.abandoned = true;
			});
		}
	}

	auto const range = exchanges.get<tag_request> ().equal_range (request.id);
	for (auto it = range.first; it != range.second; ++it)
	{
		if (!it->finished && it->task)
		{
			result.abandoned.push_back (it->task);
		}
	}

	completion ended;
	ended.callback = request.callback;
	ended.summary.request = request.work;
	ended.summary.result.status = status;

	if (status == nano::work_status::success)
	{
		ended.summary.result.work = request.found.value_or (0);
		ended.summary.source = request.source;
	}

	ended.summary.duration = std::chrono::duration_cast<std::chrono::milliseconds> (now - request.start);
	ended.summary.bad_peers = request.bad_peers;
	result.completions.push_back (std::move (ended));

	requests.get<tag_id> ().erase (request.id);
}

auto nano::work_generator_index::stop (time_point now) -> actions
{
	stopped_m = true;

	actions result;

	// No work peer is told about roots any more
	pending_cancels.clear ();

	while (!requests.empty ())
	{
		finish (requests.front (), nano::work_status::cancelled, now, result);
	}

	return result;
}

bool nano::work_generator_index::stopped () const
{
	return stopped_m;
}

void nano::work_generator_index::attach (id_t exchange_id, std::shared_ptr<nano::async::task> task)
{
	auto & by_id = exchanges.get<tag_id> ();
	auto const existing = by_id.find (exchange_id);
	release_assert (existing != by_id.end ());

	by_id.modify (existing, [&task] (exchange & entry) {
		entry.task = std::move (task);
	});
}

void nano::work_generator_index::exchange_finished (id_t exchange_id, nano::work_peer_response const & response, time_point now)
{
	auto & by_id = exchanges.get<tag_id> ();
	auto const existing = by_id.find (exchange_id);
	if (existing == by_id.end () || existing->finished)
	{
		return;
	}

	by_id.modify (existing, [] (exchange & entry) {
		entry.finished = true;
	});

	auto const & peer = existing->peer;
	auto const succeeded = response.status == nano::work_peer_status::success;
	auto const cancelled = response.status == nano::work_peer_status::cancelled;

	// A peer that failed is left alone for a while, longer with every failure in a row
	if (!cancelled)
	{
		for (auto group : { nano::work_peer_group::primary, nano::work_peer_group::secondary })
		{
			for (auto & entry : peers_of (group))
			{
				if (entry.peer != peer)
				{
					continue;
				}

				if (succeeded)
				{
					entry.failures = 0;
					entry.retry_timestamp = {};
				}
				else if (now >= entry.retry_timestamp)
				{
					// Exchanges that fail while the peer already rests belong to the failure that sent it resting
					auto const doublings = std::min (entry.failures, 16u);
					auto const backoff = std::min (config.retry_backoff * (1u << doublings), config.retry_backoff_max);
					entry.failures += 1;
					entry.retry_timestamp = now + backoff;
				}
			}
		}
	}

	// The request may have ended already, which is what cancelled the exchange
	auto & requests_by_id = requests.get<tag_id> ();
	if (auto const request = requests_by_id.find (existing->request); request != requests_by_id.end ())
	{
		requests_by_id.modify (request, [&] (request_entry & entry) {
			if (succeeded)
			{
				if (!entry.found)
				{
					entry.found = response.work;
					entry.source = peer.to_string ();
				}
			}
			else if (!cancelled)
			{
				auto const name = peer.to_string ();
				if (std::find (entry.bad_peers.begin (), entry.bad_peers.end (), name) == entry.bad_peers.end ())
				{
					entry.bad_peers.push_back (name);
				}
			}
		});
	}

	// A peer that got the request and was left before it answered is told to stop, unless another request has it working on the same root
	auto const timed_out = response.status == nano::work_peer_status::timeout;
	auto const disconnected = response.status == nano::work_peer_status::io_error;
	auto const left = cancelled || timed_out || disconnected;
	if (!stopped_m && left && response.request_sent && !has_exchange (existing->root, peer))
	{
		pending_cancels.push_back (cancel_start{ /* id */ 0, peer, existing->root });
	}
}

auto nano::work_generator_index::erase_exchanges_if (std::function<bool (exchange const &)> const & predicate) -> std::deque<exchange>
{
	std::deque<exchange> result;

	for (auto it = exchanges.begin (); it != exchanges.end ();)
	{
		if (predicate (*it))
		{
			result.push_back (*it);
			it = exchanges.erase (it);
		}
		else
		{
			++it;
		}
	}

	return result;
}

auto nano::work_generator_index::list_exchanges () const -> std::deque<exchange>
{
	return { exchanges.begin (), exchanges.end () };
}

bool nano::work_generator_index::local_started (id_t request, uint64_t pool_id)
{
	// The work pool may have answered already
	auto const existing = locals.find (request);
	if (existing == locals.end ())
	{
		return false;
	}

	if (existing->abandoned)
	{
		return false;
	}

	locals.modify (existing, [pool_id] (local_entry & entry) {
		entry.pool_id = pool_id;
	});
	return true;
}

void nano::work_generator_index::local_finished (id_t request_id, std::optional<uint64_t> const & work)
{
	if (locals.erase (request_id) == 0)
	{
		return;
	}

	// The request may have ended already, which is what cancelled the work pool item
	auto & by_id = requests.get<tag_id> ();
	if (auto const request = by_id.find (request_id); request != by_id.end ())
	{
		by_id.modify (request, [&work] (request_entry & entry) {
			if (!work)
			{
				entry.local_spent = true;
			}
			else if (!entry.found)
			{
				entry.found = work;
				entry.source = "local";
			}
		});
	}
}

void nano::work_generator_index::add_peer (nano::work_peer_group group, nano::work_peer const & peer)
{
	auto & peers = peers_of (group);
	auto const existing = std::find_if (peers.begin (), peers.end (), [&peer] (auto const & entry) {
		return entry.peer == peer;
	});

	if (existing == peers.end ())
	{
		peers.push_back (peer_entry{ peer });
	}
}

void nano::work_generator_index::clear_peers (nano::work_peer_group group)
{
	peers_of (group).clear ();
}

std::deque<nano::work_peer> nano::work_generator_index::peers (nano::work_peer_group group) const
{
	std::deque<nano::work_peer> result;
	for (auto const & entry : peers_of (group))
	{
		result.push_back (entry.peer);
	}

	return result;
}

bool nano::work_generator_index::local_enabled () const
{
	return config.local_available && config.local_generation != nano::work_local_mode::never;
}

bool nano::work_generator_index::enabled (std::optional<nano::work_peer_group> const & group) const
{
	auto const has_peers = group.has_value () && !peers_of (*group).empty ();
	return has_peers || local_enabled ();
}

size_t nano::work_generator_index::size () const
{
	return requests.size ();
}

size_t nano::work_generator_index::exchange_count () const
{
	return exchanges.size ();
}

size_t nano::work_generator_index::local_count () const
{
	return locals.size ();
}

nano::container_info nano::work_generator_index::container_info () const
{
	nano::container_info info;
	info.put ("requests", requests.size ());
	info.put ("exchanges", exchanges.size ());
	info.put ("locals", locals.size ());
	info.put ("primary_peers", primary.size ());
	info.put ("secondary_peers", secondary.size ());
	return info;
}

auto nano::work_generator_index::peers_of (nano::work_peer_group group) -> ordered_peers &
{
	return group == nano::work_peer_group::primary ? primary : secondary;
}

auto nano::work_generator_index::peers_of (nano::work_peer_group group) const -> ordered_peers const &
{
	return group == nano::work_peer_group::primary ? primary : secondary;
}

size_t & nano::work_generator_index::rotation_of (nano::work_peer_group group)
{
	return group == nano::work_peer_group::primary ? primary_rotation : secondary_rotation;
}

size_t nano::work_generator_index::count_exchanges (id_t request) const
{
	auto const range = exchanges.get<tag_request> ().equal_range (request);
	return std::count_if (range.first, range.second, [] (auto const & entry) {
		return !entry.finished;
	});
}

bool nano::work_generator_index::has_exchange (id_t request, nano::work_peer const & peer) const
{
	auto const range = exchanges.get<tag_request> ().equal_range (request);
	return std::any_of (range.first, range.second, [&peer] (auto const & entry) {
		return !entry.finished && entry.peer == peer;
	});
}

bool nano::work_generator_index::has_exchange (nano::root const & root, nano::work_peer const & peer) const
{
	return std::any_of (exchanges.begin (), exchanges.end (), [&root, &peer] (auto const & entry) {
		return !entry.finished && entry.root == root && entry.peer == peer;
	});
}

bool nano::work_generator_index::peers_exhausted (request_entry const & request, time_point now) const
{
	// A request that stays on this node has no work peers to go through
	if (!request.work.peers)
	{
		return true;
	}

	// A peer that failed for the request stays counted while it is asked again, so that retries do not put the decision off
	auto const & peers = peers_of (*request.work.peers);
	return std::all_of (peers.begin (), peers.end (), [&request, now] (auto const & entry) {
		auto const resting = now < entry.retry_timestamp;
		auto const failed = std::find (request.bad_peers.begin (), request.bad_peers.end (), entry.peer.to_string ()) != request.bad_peers.end ();
		return resting || failed;
	});
}

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

		if (index.stopped ())
		{
			return;
		}

		// Closes the index to new requests in the same step that ends the ones in progress
		actions = index.stop (std::chrono::steady_clock::now ());
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

		if (auto const id = index.insert (request, callback, std::chrono::steady_clock::now ()))
		{
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
	// The loop ends by seeing the index stopped, a cancellation only wakes it
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

	if (index.stopped ())
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

std::string_view nano::to_string (nano::work_local_mode mode)
{
	return nano::enum_to_string (mode);
}

std::string_view nano::to_string (nano::work_peer_selection selection)
{
	return nano::enum_to_string (selection);
}

nano::stat::detail nano::to_stat_detail (nano::work_status status)
{
	return nano::enum_convert<nano::stat::detail> (status);
}
