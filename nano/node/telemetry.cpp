#include <nano/lib/alarm.hpp>
#include <nano/lib/worker.hpp>
#include <nano/node/network.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/buffer.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <future>
#include <numeric>

std::chrono::seconds constexpr nano::telemetry_impl::alarm_cutoff;

namespace
{
// This class is just a wrapper to allow a recursive lambda while properly handling memory resources
class ongoing_func_wrapper
{
public:
	std::function<void()> ongoing_func;
};
}

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

void nano::telemetry::add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	if (!stopped)
	{
		batch_request->add (telemetry_data_a, endpoint_a);

		for (auto & request : single_requests)
		{
			request.second.impl->add (telemetry_data_a, endpoint_a);
		}
	}
}

void nano::telemetry::ongoing_req_all_peers ()
{
	auto wrapper = std::make_shared<ongoing_func_wrapper> ();
	// Keep calling ongoing_func while the peer is still being called
	wrapper->ongoing_func = [this, telemetry_impl_w = std::weak_ptr<nano::telemetry_impl> (batch_request), wrapper]() {
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
				this->alarm.add (std::chrono::steady_clock::now () + batch_telemetry_impl->cache_cutoff + batch_telemetry_impl->alarm_cutoff, wrapper->ongoing_func);
			}
		}
	};

	alarm.add (std::chrono::steady_clock::now () + batch_request->cache_cutoff + batch_request->alarm_cutoff, wrapper->ongoing_func);
}

void nano::telemetry::get_metrics_peers_async (std::function<void(telemetry_data_responses const &)> const & callback_a)
{
	auto peers = network.list (std::numeric_limits<size_t>::max (), false, network_params.protocol.telemetry_protocol_version_min);
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
	auto wrapper = std::make_shared<ongoing_func_wrapper> ();
	// Keep calling ongoing_func while the peer is still being called
	const auto & last_updated = single_request_data_a.last_updated;
	wrapper->ongoing_func = [this, telemetry_impl_w = std::weak_ptr<nano::telemetry_impl> (single_request_data_a.impl), &last_updated, &endpoint_a, wrapper]() {
		if (auto telemetry_impl = telemetry_impl_w.lock ())
		{
			nano::lock_guard<std::mutex> guard (this->mutex);
			if (std::chrono::steady_clock::now () - telemetry_impl->cache_cutoff > last_updated && telemetry_impl->callbacks.empty ())
			{
				//  This will be picked up by the batch request next round
				this->finished_single_requests[endpoint_a] = telemetry_impl->cached_telemetry_data.begin ()->second;
				this->single_requests.erase (endpoint_a);
			}
			else
			{
				// Request is still active, so call again
				this->alarm.add (std::chrono::steady_clock::now () + telemetry_impl->cache_cutoff, wrapper->ongoing_func);
			}
		}
	};

	alarm.add (std::chrono::steady_clock::now () + single_request_data_a.impl->cache_cutoff, wrapper->ongoing_func);
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
			update_cleanup_data (pair.first->first, pair.first->second, pair.second);

			pair.first->second.impl->get_metrics_async ({ channel_a }, [callback_a, channel_a](telemetry_data_responses const & telemetry_data_responses_a) {
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

void nano::telemetry_impl::add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a)
{
	nano::unique_lock<std::mutex> lk (mutex);
	if (required_responses.find (endpoint_a) == required_responses.cend ())
	{
		// Not requesting telemetry data from this channel so ignore it
		return;
	}

	current_telemetry_data_responses[endpoint_a] = { telemetry_data_a, std::chrono::steady_clock::now (), std::chrono::system_clock::now () };
	channel_processed (lk, endpoint_a);
}

void nano::telemetry_impl::invoke_callbacks ()
{
	decltype (callbacks) callbacks_l;
	bool all_received;

	{
		// Copy callbacks so that they can be called outside of holding the lock
		nano::lock_guard<std::mutex> guard (mutex);
		callbacks_l = callbacks;
		current_telemetry_data_responses.clear ();
		callbacks.clear ();
		all_received = failed.empty ();
	}

	if (pre_callback_callback)
	{
		pre_callback_callback (cached_telemetry_data, mutex);
	}

	for (auto & callback : callbacks_l)
	{
		callback ({ cached_telemetry_data, all_received });
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
