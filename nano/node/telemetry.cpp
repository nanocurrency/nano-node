#include <nano/lib/alarm.hpp>
#include <nano/lib/worker.hpp>
#include <nano/node/network.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/buffer.hpp>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <future>
#include <numeric>
#include <set>

std::chrono::seconds constexpr nano::telemetry_impl::alarm_cutoff;

nano::telemetry::telemetry (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a) :
network (network_a),
alarm (alarm_a),
worker (worker_a),
batch_request (std::make_shared<nano::telemetry_impl> (network, alarm, worker))
{
	// Before callbacks are called with the batch request, check if any of the single request data can be appended to give
	batch_request->pre_callback_callback = [this](std::unordered_map<nano::endpoint, telemetry_data_time_pair> & data_a, std::mutex & mutex_a) {
		nano::lock_guard<std::mutex> guard (this->mutex);
		for (auto & single_request : single_requests)
		{
			nano::lock_guard<std::mutex> guard (single_request.second.impl->mutex);
			if (!single_request.second.impl->cached_telemetry_data.empty ())
			{
				nano::lock_guard<std::mutex> batch_request_guard (mutex_a);
				auto it = this->batch_request->cached_telemetry_data.find (single_request.first);
				if (it != this->batch_request->cached_telemetry_data.cend () && single_request.second.last_updated > it->second.last_updated)
				{
					it->second = single_request.second.impl->cached_telemetry_data.begin ()->second;
				}
				else
				{
					data_a.emplace (single_request.first, single_request.second.impl->cached_telemetry_data.begin ()->second);
				}
			}
		}

		for (auto & pending : finished_single_requests)
		{
			nano::lock_guard<std::mutex> batch_request_guard (mutex_a);
			auto it = this->batch_request->cached_telemetry_data.find (pending.first);
			if (it != this->batch_request->cached_telemetry_data.cend () && pending.second.last_updated > it->second.last_updated)
			{
				it->second = pending.second;
			}
			else
			{
				data_a.emplace (pending.first, pending.second);
			}
		}
		finished_single_requests.clear ();
	};

	ongoing_req_all_peers ();
}

void nano::telemetry::stop ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	batch_request = nullptr;
	single_requests.clear ();
	stopped = true;
}

void nano::telemetry::add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a, bool is_empty_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	if (!stopped)
	{
		batch_request->add (telemetry_data_a, endpoint_a, is_empty_a);

		for (auto & request : single_requests)
		{
			request.second.impl->add (telemetry_data_a, endpoint_a, is_empty_a);
		}
	}
}

void nano::telemetry::ongoing_req_all_peers ()
{
	alarm.add (std::chrono::steady_clock::now () + batch_request->cache_cutoff + batch_request->alarm_cutoff, [this, telemetry_impl_w = std::weak_ptr<nano::telemetry_impl> (batch_request)]() {
		if (auto batch_telemetry_impl = telemetry_impl_w.lock ())
		{
			nano::lock_guard<std::mutex> guard (this->mutex);
			if (!this->stopped)
			{
				auto peers = this->network.list (std::numeric_limits<size_t>::max (), false, network_params.protocol.telemetry_protocol_version_min);
				// If exists in single_requests don't request because they will just be rejected by other peers until the next round
				auto const & single_requests = this->single_requests;
				peers.erase (std::remove_if (peers.begin (), peers.end (), [&single_requests](auto const & channel_a) {
					return single_requests.count (channel_a->get_endpoint ()) > 0;
				}),
				peers.cend ());
				if (!peers.empty ())
				{
					batch_telemetry_impl->get_metrics_async (peers, [](nano::telemetry_data_responses const &) {
						// Intentionally empty, just using to refresh the cache
					});
				}

				this->ongoing_req_all_peers ();
			}
		}
	});
}

void nano::telemetry::get_metrics_peers_async (std::function<void(telemetry_data_responses const &)> const & callback_a)
{
	auto peers = network.list (std::numeric_limits<size_t>::max (), network_params.protocol.telemetry_protocol_version_min, false);
	nano::lock_guard<std::mutex> guard (mutex);
	if (!stopped && !peers.empty ())
	{
		// If exists in single_requests, don't request because they will just be rejected by other nodes, instead all it as additional values
		peers.erase (std::remove_if (peers.begin (), peers.end (), [& single_requests = this->single_requests](auto const & channel_a) {
			return single_requests.count (channel_a->get_endpoint ()) > 0;
		}),
		peers.cend ());

		batch_request->get_metrics_async (peers, [callback_a](nano::telemetry_data_responses const & telemetry_data_responses) {
			callback_a (telemetry_data_responses);
		});
	}
	else
	{
		const auto all_received = false;
		callback_a (nano::telemetry_data_responses{ {}, all_received });
	}
}

nano::telemetry_data_responses nano::telemetry::get_metrics_peers ()
{
	std::promise<telemetry_data_responses> promise;
	get_metrics_peers_async ([&promise](telemetry_data_responses const & telemetry_data_responses_a) {
		promise.set_value (telemetry_data_responses_a);
	});

	return promise.get_future ().get ();
}

// After a request is made to a single peer we want to remove it from the container after the peer has not been requested for a while (cache_cutoff).
void nano::telemetry::ongoing_single_request_cleanup (nano::endpoint const & endpoint_a, nano::telemetry::single_request_data const & single_request_data_a)
{
	alarm.add (std::chrono::steady_clock::now () + single_request_data_a.impl->cache_cutoff, [this, telemetry_impl_w = std::weak_ptr<nano::telemetry_impl> (single_request_data_a.impl), &single_request_data_a, &endpoint_a]() {
		if (auto telemetry_impl = telemetry_impl_w.lock ())
		{
			nano::lock_guard<std::mutex> guard (this->mutex);
			if (std::chrono::steady_clock::now () - telemetry_impl->cache_cutoff > single_request_data_a.last_updated && telemetry_impl->callbacks.empty ())
			{
				//  This will be picked up by the batch request next round
				this->finished_single_requests[endpoint_a] = telemetry_impl->cached_telemetry_data.begin ()->second;
				this->single_requests.erase (endpoint_a);
			}
			else
			{
				// Request is still active, so call again
				this->ongoing_single_request_cleanup (endpoint_a, single_request_data_a);
			}
		}
	});
}

void nano::telemetry::update_cleanup_data (nano::endpoint const & endpoint_a, nano::telemetry::single_request_data & single_request_data_a, bool is_new_a)
{
	if (is_new_a)
	{
		// Clean this request up when it isn't being used anymore
		ongoing_single_request_cleanup (endpoint_a, single_request_data_a);
	}
	else
	{
		// Ensure that refreshed flag is reset so we don't delete it before processing
		single_request_data_a.last_updated = std::chrono::steady_clock::now ();
	}
}

void nano::telemetry::get_metrics_single_peer_async (std::shared_ptr<nano::transport::channel> const & channel_a, std::function<void(telemetry_data_response const &)> const & callback_a)
{
	auto invoke_callback_with_error = [&callback_a, &worker = this->worker, channel_a]() {
		nano::endpoint endpoint;
		if (channel_a)
		{
			endpoint = channel_a->get_endpoint ();
		}
		worker.push_task ([callback_a, endpoint]() {
			auto const error = true;
			callback_a ({ nano::telemetry_data_time_pair{}, endpoint, error });
		});
	};

	nano::lock_guard<std::mutex> guard (mutex);
	if (!stopped)
	{
		if (channel_a && (channel_a->get_network_version () >= network_params.protocol.telemetry_protocol_version_min))
		{
			auto add_callback_async = [& worker = this->worker, &callback_a](telemetry_data_time_pair const & telemetry_data_time_pair_a, nano::endpoint const & endpoint_a) {
				telemetry_data_response telemetry_data_response_l{ telemetry_data_time_pair_a, endpoint_a, false };
				worker.push_task ([telemetry_data_response_l, callback_a]() {
					callback_a (telemetry_data_response_l);
				});
			};

			// First check if the batched metrics have processed this endpoint.
			{
				nano::lock_guard<std::mutex> guard (batch_request->mutex);
				auto it = batch_request->cached_telemetry_data.find (channel_a->get_endpoint ());
				if (it != batch_request->cached_telemetry_data.cend ())
				{
					add_callback_async (it->second, it->first);
					return;
				}
			}
			// Next check single requests which finished and are awaiting batched requests
			auto it = finished_single_requests.find (channel_a->get_endpoint ());
			if (it != finished_single_requests.cend ())
			{
				add_callback_async (it->second, it->first);
				return;
			}

			auto pair = single_requests.emplace (channel_a->get_endpoint (), single_request_data{ std::make_shared<nano::telemetry_impl> (network, alarm, worker), std::chrono::steady_clock::now () });
			auto & single_request_data_it = pair.first;
			update_cleanup_data (single_request_data_it->first, single_request_data_it->second, pair.second);

			single_request_data_it->second.impl->get_metrics_async ({ channel_a }, [callback_a, channel_a](telemetry_data_responses const & telemetry_data_responses_a) {
				// There should only be 1 response, so if this hasn't been received then conclude it is an error.
				auto const error = !telemetry_data_responses_a.all_received;
				if (!error)
				{
					assert (telemetry_data_responses_a.telemetry_data_time_pairs.size () == 1);
					auto it = telemetry_data_responses_a.telemetry_data_time_pairs.begin ();
					callback_a ({ it->second, it->first, error });
				}
				else
				{
					callback_a ({ nano::telemetry_data_time_pair{}, channel_a->get_endpoint (), error });
				}
			});
		}
		else
		{
			invoke_callback_with_error ();
		}
	}
	else
	{
		invoke_callback_with_error ();
	}
}

nano::telemetry_data_response nano::telemetry::get_metrics_single_peer (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	std::promise<telemetry_data_response> promise;
	get_metrics_single_peer_async (channel_a, [&promise](telemetry_data_response const & single_metric_data_a) {
		promise.set_value (single_metric_data_a);
	});

	return promise.get_future ().get ();
}

size_t nano::telemetry::telemetry_data_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	auto total = std::accumulate (single_requests.begin (), single_requests.end (), static_cast<size_t> (0), [](size_t total, auto & single_request) {
		return total += single_request.second.impl->telemetry_data_size ();
	});

	if (batch_request)
	{
		total += batch_request->telemetry_data_size ();
	}
	return total;
}

size_t nano::telemetry::finished_single_requests_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return finished_single_requests.size ();
}

nano::telemetry_impl::telemetry_impl (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a) :
network (network_a),
alarm (alarm_a),
worker (worker_a)
{
}

void nano::telemetry_impl::flush_callbacks_async ()
{
	// Post to worker so that it's truly async and not on the calling thread (same problem as std::async otherwise)
	worker.push_task ([this_w = std::weak_ptr<nano::telemetry_impl> (shared_from_this ())]() {
		if (auto this_l = this_w.lock ())
		{
			nano::unique_lock<std::mutex> lk (this_l->mutex);
			// Invoke all callbacks, it's possible that during the mutex unlock other callbacks were added,
			// so check again and invoke those too
			this_l->invoking = true;
			while (!this_l->callbacks.empty ())
			{
				lk.unlock ();
				this_l->invoke_callbacks ();
				lk.lock ();
			}
			this_l->invoking = false;
		}
	});
}

void nano::telemetry_impl::get_metrics_async (std::deque<std::shared_ptr<nano::transport::channel>> const & channels_a, std::function<void(telemetry_data_responses const &)> const & callback_a)
{
	{
		nano::unique_lock<std::mutex> lk (mutex);
		callbacks.push_back (callback_a);
		if (callbacks.size () > 1 || invoking)
		{
			// This means we already have at least one pending result already, so it will handle calls this callback when it completes
			return;
		}

		// Check if we can just return cached results
		if (channels_a.empty () || std::chrono::steady_clock::now () <= (last_time + cache_cutoff))
		{
			flush_callbacks_async ();
			return;
		}

		failed.clear ();
		assert (required_responses.empty ());
		std::transform (channels_a.begin (), channels_a.end (), std::inserter (required_responses, required_responses.end ()), [](auto const & channel) {
			return channel->get_endpoint ();
		});
	}

	fire_request_messages (channels_a);
}

void nano::telemetry_impl::add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a, bool is_empty_a)
{
	nano::unique_lock<std::mutex> lk (mutex);
	if (required_responses.find (endpoint_a) == required_responses.cend ())
	{
		// Not requesting telemetry data from this channel so ignore it
		return;
	}

	if (!is_empty_a)
	{
		current_telemetry_data_responses[endpoint_a] = { telemetry_data_a, std::chrono::steady_clock::now (), std::chrono::system_clock::now () };
	}
	channel_processed (lk, endpoint_a);
}

void nano::telemetry_impl::invoke_callbacks ()
{
	decltype (callbacks) callbacks_l;
	bool all_received;
	decltype (cached_telemetry_data) cached_telemetry_data_l;
	{
		// Copy callbacks so that they can be called outside of holding the lock
		nano::lock_guard<std::mutex> guard (mutex);
		callbacks_l = callbacks;
		cached_telemetry_data_l = cached_telemetry_data;
		current_telemetry_data_responses.clear ();
		callbacks.clear ();
		all_received = failed.empty ();
	}

	if (pre_callback_callback)
	{
		pre_callback_callback (cached_telemetry_data_l, mutex);
	}
	// Need to account for nodes which disable telemetry data in responses
	bool all_received_l = !cached_telemetry_data_l.empty () && all_received;
	for (auto & callback : callbacks_l)
	{
		callback ({ cached_telemetry_data_l, all_received_l });
	}
}

void nano::telemetry_impl::channel_processed (nano::unique_lock<std::mutex> & lk_a, nano::endpoint const & endpoint_a)
{
	assert (lk_a.owns_lock ());
	auto num_removed = required_responses.erase (endpoint_a);
	if (num_removed > 0 && required_responses.empty ())
	{
		assert (lk_a.owns_lock ());
		cached_telemetry_data = current_telemetry_data_responses;

		last_time = std::chrono::steady_clock::now ();
		flush_callbacks_async ();
	}
}

void nano::telemetry_impl::fire_request_messages (std::deque<std::shared_ptr<nano::transport::channel>> const & channels)
{
	uint64_t round_l;
	{
		nano::lock_guard<std::mutex> guard (mutex);
		++round;
		round_l = round;
	}

	// Fire off a telemetry request to all passed in channels
	nano::telemetry_req message;
	for (auto & channel : channels)
	{
		assert (channel->get_network_version () >= network_params.protocol.telemetry_protocol_version_min);

		std::weak_ptr<nano::telemetry_impl> this_w (shared_from_this ());
		// clang-format off
		channel->send (message, [this_w, endpoint = channel->get_endpoint ()](boost::system::error_code const & ec, size_t size_a) {
			if (auto this_l = this_w.lock ())
			{
				if (ec)
				{
					// Error sending the telemetry_req message
					nano::unique_lock<std::mutex> lk (this_l->mutex);
					this_l->failed.push_back (endpoint);
					this_l->channel_processed (lk, endpoint);
				}
			}
		},
		false);
		// clang-format on

		// If no response is seen after a certain period of time, remove it from the list of expected responses. However, only if it is part of the same round.
		alarm.add (std::chrono::steady_clock::now () + alarm_cutoff, [this_w, endpoint = channel->get_endpoint (), round_l]() {
			if (auto this_l = this_w.lock ())
			{
				nano::unique_lock<std::mutex> lk (this_l->mutex);
				if (this_l->round == round_l && this_l->required_responses.find (endpoint) != this_l->required_responses.cend ())
				{
					this_l->failed.push_back (endpoint);
					this_l->channel_processed (lk, endpoint);
				}
			}
		});
	}
}

size_t nano::telemetry_impl::telemetry_data_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return current_telemetry_data_responses.size ();
}

bool nano::telemetry_data_time_pair::operator== (telemetry_data_time_pair const & telemetry_data_time_pair_a) const
{
	return data == telemetry_data_time_pair_a.data && last_updated == telemetry_data_time_pair_a.last_updated;
}

bool nano::telemetry_data_time_pair::operator!= (telemetry_data_time_pair const & telemetry_data_time_pair_a) const
{
	return !(*this == telemetry_data_time_pair_a);
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (telemetry & telemetry, const std::string & name)
{
	size_t single_requests_count;
	{
		nano::lock_guard<std::mutex> guard (telemetry.mutex);
		single_requests_count = telemetry.single_requests.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	if (telemetry.batch_request)
	{
		composite->add_component (collect_container_info (*telemetry.batch_request, "batch_request"));
	}
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "single_requests", single_requests_count, sizeof (decltype (telemetry.single_requests)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "finished_single_requests", telemetry.finished_single_requests_size (), sizeof (decltype (telemetry.finished_single_requests)::value_type) }));
	return composite;
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (telemetry_impl & telemetry_impl, const std::string & name)
{
	size_t callback_count;
	size_t all_telemetry_data_count;
	size_t cached_telemetry_data_count;
	size_t required_responses_count;
	{
		nano::lock_guard<std::mutex> guard (telemetry_impl.mutex);
		callback_count = telemetry_impl.callbacks.size ();
		all_telemetry_data_count = telemetry_impl.current_telemetry_data_responses.size ();
		cached_telemetry_data_count = telemetry_impl.cached_telemetry_data.size ();
		required_responses_count = telemetry_impl.required_responses.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "callbacks", callback_count, sizeof (decltype (telemetry_impl.callbacks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "current_telemetry_data_responses", all_telemetry_data_count, sizeof (decltype (telemetry_impl.current_telemetry_data_responses)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cached_telemetry_data", cached_telemetry_data_count, sizeof (decltype (telemetry_impl.cached_telemetry_data)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "required_responses", required_responses_count, sizeof (decltype (telemetry_impl.required_responses)::value_type) }));
	return composite;
}

nano::telemetry_data nano::consolidate_telemetry_data (std::vector<nano::telemetry_data> const & telemetry_datas)
{
	std::vector<nano::telemetry_data_time_pair> telemetry_data_time_pairs;
	telemetry_data_time_pairs.reserve (telemetry_datas.size ());

	std::transform (telemetry_datas.begin (), telemetry_datas.end (), std::back_inserter (telemetry_data_time_pairs), [](nano::telemetry_data const & telemetry_data_a) {
		// Don't care about the timestamps here
		return nano::telemetry_data_time_pair{ telemetry_data_a, {}, {} };
	});

	return consolidate_telemetry_data_time_pairs (telemetry_data_time_pairs).data;
}

nano::telemetry_data_time_pair nano::consolidate_telemetry_data_time_pairs (std::vector<nano::telemetry_data_time_pair> const & telemetry_data_time_pairs_a)
{
	if (telemetry_data_time_pairs_a.empty ())
	{
		return {};
	}
	else if (telemetry_data_time_pairs_a.size () == 1)
	{
		// Only 1 element in the collection, so just return it.
		return telemetry_data_time_pairs_a.front ();
	}

	std::unordered_map<uint8_t, int> protocol_versions;
	std::unordered_map<std::string, int> vendor_versions;
	std::unordered_map<uint64_t, int> bandwidth_caps;
	std::unordered_map<nano::block_hash, int> genesis_blocks;

	// Use a trimmed average which excludes the upper and lower 10% of the results
	std::multiset<uint64_t> account_counts;
	std::multiset<uint64_t> block_counts;
	std::multiset<uint64_t> cemented_counts;
	std::multiset<uint32_t> peer_counts;
	std::multiset<uint64_t> unchecked_counts;
	std::multiset<uint64_t> uptime_counts;
	std::multiset<uint64_t> bandwidth_counts;
	std::multiset<long long> timestamp_counts;

	for (auto const & telemetry_data_time_pair : telemetry_data_time_pairs_a)
	{
		auto & telemetry_data = telemetry_data_time_pair.data;
		account_counts.insert (telemetry_data.account_count);
		block_counts.insert (telemetry_data.block_count);
		cemented_counts.insert (telemetry_data.cemented_count);

		std::ostringstream ss;
		ss << telemetry_data.major_version;
		if (telemetry_data.minor_version.is_initialized ())
		{
			ss << "." << *telemetry_data.minor_version;
			if (telemetry_data.patch_version.is_initialized ())
			{
				ss << "." << *telemetry_data.patch_version;
				if (telemetry_data.pre_release_version.is_initialized ())
				{
					ss << "." << *telemetry_data.pre_release_version;
					if (telemetry_data.maker.is_initialized ())
					{
						ss << "." << *telemetry_data.maker;
					}
				}
			}
		}

		++vendor_versions[ss.str ()];
		++protocol_versions[telemetry_data.protocol_version];
		peer_counts.insert (telemetry_data.peer_count);
		unchecked_counts.insert (telemetry_data.unchecked_count);
		uptime_counts.insert (telemetry_data.uptime);
		// 0 has a special meaning (unlimited), don't include it in the average as it will be heavily skewed
		if (telemetry_data.bandwidth_cap != 0)
		{
			bandwidth_counts.insert (telemetry_data.bandwidth_cap);
		}

		++bandwidth_caps[telemetry_data.bandwidth_cap];
		++genesis_blocks[telemetry_data.genesis_block];

		timestamp_counts.insert (std::chrono::time_point_cast<std::chrono::milliseconds> (telemetry_data_time_pair.system_last_updated).time_since_epoch ().count ());
	}

	// Remove 10% of the results from the lower and upper bounds to catch any outliers. Need at least 10 responses before any are removed.
	auto num_either_side_to_remove = telemetry_data_time_pairs_a.size () / 10;

	auto strip_outliers_and_sum = [num_either_side_to_remove](auto & counts) {
		counts.erase (counts.begin (), std::next (counts.begin (), num_either_side_to_remove));
		counts.erase (std::next (counts.rbegin (), num_either_side_to_remove).base (), counts.end ());
		return std::accumulate (counts.begin (), counts.end (), nano::uint128_t (0), [](nano::uint128_t total, auto count) {
			return total += count;
		});
	};

	auto account_sum = strip_outliers_and_sum (account_counts);
	auto block_sum = strip_outliers_and_sum (block_counts);
	auto cemented_sum = strip_outliers_and_sum (cemented_counts);
	auto peer_sum = strip_outliers_and_sum (peer_counts);
	auto unchecked_sum = strip_outliers_and_sum (unchecked_counts);
	auto uptime_sum = strip_outliers_and_sum (uptime_counts);
	auto bandwidth_sum = strip_outliers_and_sum (bandwidth_counts);

	nano::telemetry_data consolidated_data;
	auto size = telemetry_data_time_pairs_a.size () - num_either_side_to_remove * 2;
	consolidated_data.account_count = boost::numeric_cast<decltype (consolidated_data.account_count)> (account_sum / size);
	consolidated_data.block_count = boost::numeric_cast<decltype (consolidated_data.block_count)> (block_sum / size);
	consolidated_data.cemented_count = boost::numeric_cast<decltype (consolidated_data.cemented_count)> (cemented_sum / size);
	consolidated_data.peer_count = boost::numeric_cast<decltype (consolidated_data.peer_count)> (peer_sum / size);
	consolidated_data.uptime = boost::numeric_cast<decltype (consolidated_data.uptime)> (uptime_sum / size);
	consolidated_data.unchecked_count = boost::numeric_cast<decltype (consolidated_data.unchecked_count)> (unchecked_sum / size);

	auto set_mode_or_average = [](auto const & collection, auto & var, auto const & sum, size_t size) {
		auto max = std::max_element (collection.begin (), collection.end (), [](auto const & lhs, auto const & rhs) {
			return lhs.second < rhs.second;
		});
		if (max->second > 1)
		{
			var = max->first;
		}
		else
		{
			var = (sum / size).template convert_to<std::remove_reference_t<decltype (var)>> ();
		}
	};

	auto set_mode = [](auto const & collection, auto & var, size_t size) {
		auto max = std::max_element (collection.begin (), collection.end (), [](auto const & lhs, auto const & rhs) {
			return lhs.second < rhs.second;
		});
		if (max->second > 1)
		{
			var = max->first;
		}
		else
		{
			// Just pick the first one
			var = collection.begin ()->first;
		}
	};

	// Use the mode of protocol version and vendor version. Also use it for bandwidth cap if there is 2 or more of the same cap.
	set_mode_or_average (bandwidth_caps, consolidated_data.bandwidth_cap, bandwidth_sum, size);
	set_mode (protocol_versions, consolidated_data.protocol_version, size);
	set_mode (genesis_blocks, consolidated_data.genesis_block, size);

	// Vendor version, needs to be parsed out of the string
	std::string version;
	set_mode (vendor_versions, version, size);

	// May only have major version, but check for optional parameters as well, only output if all are used
	std::vector<std::string> version_fragments;
	boost::split (version_fragments, version, boost::is_any_of ("."));
	assert (!version_fragments.empty () && version_fragments.size () <= 5);
	consolidated_data.major_version = boost::lexical_cast<uint8_t> (version_fragments.front ());
	if (version_fragments.size () == 5)
	{
		consolidated_data.minor_version = boost::lexical_cast<uint8_t> (version_fragments[1]);
		consolidated_data.patch_version = boost::lexical_cast<uint8_t> (version_fragments[2]);
		consolidated_data.pre_release_version = boost::lexical_cast<uint8_t> (version_fragments[3]);
		consolidated_data.maker = boost::lexical_cast<uint8_t> (version_fragments[4]);
	}

	// Consolidate timestamps
	auto timestamp_sum = strip_outliers_and_sum (timestamp_counts);
	auto consolidated_timestamp = boost::numeric_cast<long long> (timestamp_sum / size);

	return telemetry_data_time_pair{ consolidated_data, std::chrono::steady_clock::time_point{}, std::chrono::system_clock::time_point (std::chrono::milliseconds (consolidated_timestamp)) };
}