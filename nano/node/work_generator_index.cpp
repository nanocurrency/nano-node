#include <nano/lib/assert.hpp>
#include <nano/lib/container_info.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/node/work_generator_index.hpp>

#include <algorithm>

nano::work_generator_index::work_generator_index (limits const & limits_a) :
	config{ limits_a }
{
}

nano::root nano::work_generator_index::request_entry::root () const
{
	return work.root;
}

auto nano::work_generator_index::insert (nano::work_request const & work, nano::work_callback callback, time_point now) -> id_t
{
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

auto nano::work_generator_index::cancel_all (time_point now) -> actions
{
	actions result;

	while (!requests.empty ())
	{
		finish (requests.front (), nano::work_status::cancelled, now, result);
	}

	return result;
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
	if (left && response.request_sent && !has_exchange (existing->root, peer))
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

std::string_view nano::to_string (nano::work_local_mode mode)
{
	return nano::enum_to_string (mode);
}

std::string_view nano::to_string (nano::work_peer_selection selection)
{
	return nano::enum_to_string (selection);
}
