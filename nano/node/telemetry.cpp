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

nano::telemetry::telemetry (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a) :
network (network_a),
alarm (alarm_a),
worker (worker_a),
batch_telemetry (std::make_shared<nano::telemetry_impl> (network, alarm, worker))
{
}

void nano::telemetry::stop ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	batch_telemetry = nullptr;
	single_requests.clear ();
	stopped = true;
}

void nano::telemetry::add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	if (!stopped)
	{
		batch_telemetry->add (telemetry_data_a, endpoint_a);

		for (auto & request : single_requests)
		{
			request.second->add (telemetry_data_a, endpoint_a);
		}
	}
}

void nano::telemetry::get_random_metrics_async (std::function<void(batched_metric_data const &)> const & callback_a)
{
	// These peers will only be used if there isn't an already ongoing batch telemetry request round
	auto random_peers = network.random_set (network.size_sqrt (), network_params.protocol.telemetry_protocol_version_min);
	nano::lock_guard<std::mutex> guard (mutex);
	if (!stopped)
	{
		batch_telemetry->get_metrics_async (random_peers, [callback_a](nano::batched_metric_data const & batched_metric_data) {
			callback_a (batched_metric_data);
		});
	}
}

nano::batched_metric_data nano::telemetry::get_random_metrics ()
{
	std::promise<batched_metric_data> promise;
	get_random_metrics_async ([&promise](batched_metric_data const & batched_metric_data_a) {
		promise.set_value (batched_metric_data_a);
	});

	return promise.get_future ().get ();
}

void nano::telemetry::get_single_metric_async (std::shared_ptr<nano::transport::channel> const & channel_a, std::function<void(single_metric_data const &)> const & callback_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	if (!stopped)
	{
		if (!channel_a)
		{
			const auto error = true;
			callback_a (nano::single_metric_data{ nano::telemetry_data (), false, error });
		}
		else
		{
			auto it = single_requests.emplace (channel_a->get_endpoint (), std::make_shared<nano::telemetry_impl> (network, alarm, worker));
			it.first->second->get_metrics_async ({ channel_a }, [callback_a](batched_metric_data const & batched_metric_data_a) {
				assert (batched_metric_data_a.data.size () == 1);
				callback_a ({ batched_metric_data_a.data.front (), batched_metric_data_a.is_cached, batched_metric_data_a.error });
			});
		}
	}
}

nano::single_metric_data nano::telemetry::get_single_metric (std::shared_ptr<nano::transport::channel> const & channel_a)
{
	std::promise<single_metric_data> promise;
	get_single_metric_async (channel_a, [&promise](single_metric_data const & single_metric_data_a) {
		promise.set_value (single_metric_data_a);
	});

	return promise.get_future ().get ();
}

size_t nano::telemetry::telemetry_data_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	auto total = std::accumulate (single_requests.begin (), single_requests.end (), 0, [](size_t total, auto & single_request) {
		return total += single_request.second->telemetry_data_size ();
	});

	if (batch_telemetry)
	{
		total += batch_telemetry->telemetry_data_size ();
	}
	return total;
}

nano::telemetry_impl::telemetry_impl (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a) :
network (network_a),
alarm (alarm_a),
worker (worker_a)
{
}

void nano::telemetry_impl::get_metrics_async (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels_a, std::function<void(batched_metric_data const &)> const & callback_a)
{
	std::deque<std::shared_ptr<nano::transport::channel>> d;
	{
		nano::unique_lock<std::mutex> lk (mutex);
		callbacks.push_back (callback_a);
		if (callbacks.size () > 1)
		{
			// This means we already have at least one pending result already, so it will handle calls this callback when it completes
			return;
		}
		else if (channels_a.empty ())
		{
			const auto error = true;
			fire_callbacks (lk, error);
			return;
		}

		// Check if we can just return cached results
		if (std::chrono::steady_clock::now () < (last_time + cache_cutoff))
		{
			// Post to worker so that it's truly async and not on the calling thread (same problem as std::async otherwise)
			worker.push_task ([this_w = std::weak_ptr<nano::telemetry_impl> (shared_from_this ())]() {
				if (auto this_l = this_w.lock ())
				{
					// Just invoke all callbacks, it's possible that during the mutex unlock other callbacks were added, so check again
					nano::unique_lock<std::mutex> lk (this_l->mutex);
					while (!this_l->callbacks.empty ())
					{
						lk.unlock ();
						const auto error = false;
						const auto is_cached = true;
						this_l->invoke_callbacks (is_cached, error);
						lk.lock ();
					}
				}
			});
			return;
		}

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

	all_telemetry_data.push_back (telemetry_data_a);
	channel_processed (lk, endpoint_a, false);
}

void nano::telemetry_impl::invoke_callbacks (bool cached_a, bool error_a)
{
	decltype (callbacks) callbacks_l;

	{
		// Copy callbacks so that they can be called outside of holding the lock
		nano::lock_guard<std::mutex> guard (mutex);
		callbacks_l = callbacks;
		all_telemetry_data.clear ();
		callbacks.clear ();
	}
	for (auto & callback : callbacks_l)
	{
		callback ({ cached_telemetry_data, cached_a, error_a });
	}
}

void nano::telemetry_impl::channel_processed (nano::unique_lock<std::mutex> & lk_a, nano::endpoint const & endpoint_a, bool error_a)
{
	assert (lk_a.owns_lock ());
	auto num_removed = required_responses.erase (endpoint_a);
	if (num_removed > 0 && required_responses.empty ())
	{
		fire_callbacks (lk_a, error_a);
	}
}

void nano::telemetry_impl::fire_callbacks (nano::unique_lock<std::mutex> & lk, bool error_a)
{
	assert (lk.owns_lock ());
	cached_telemetry_data = all_telemetry_data;

	last_time = std::chrono::steady_clock::now ();
	lk.unlock ();
	invoke_callbacks (false, error_a);
	lk.lock ();
}

void nano::telemetry_impl::fire_request_messages (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels)
{
	uint64_t round_l = 0;
	{
		nano::lock_guard<std::mutex> guard (mutex);
		round_l = round;
		++round;
	}

	// Fire off a telemetry request to all passed in channels
	nano::telemetry_req message;
	for (auto & channel : channels)
	{
		assert (channel->get_network_version () >= network_params.protocol.telemetry_protocol_version_min);

		std::weak_ptr<nano::telemetry_impl> this_w (shared_from_this ());
		channel->send (message, [this_w, endpoint = channel->get_endpoint (), round_l](boost::system::error_code const & ec, size_t size_a) {
			if (auto this_l = this_w.lock ())
			{
				if (ec)
				{
					// Error sending the telemetry_req message
					nano::unique_lock<std::mutex> lk (this_l->mutex);
					this_l->channel_processed (lk, endpoint, true);
				}
				else
				{
					// If no response is seen after a certain period of time, remove it from the list of expected responses. However, only if it is part of the same round.
					this_l->alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (2000), [this_w, endpoint, round_l]() {
						if (auto this_l = this_w.lock ())
						{
							nano::unique_lock<std::mutex> lk (this_l->mutex);
							if (this_l->round == round_l)
							{
								this_l->channel_processed (lk, endpoint, false);
							}
						}
					});
				}
			}
		});
	}
}

size_t nano::telemetry_impl::telemetry_data_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return all_telemetry_data.size ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (telemetry & telemetry, const std::string & name)
{
	size_t single_requests_count;
	{
		nano::lock_guard<std::mutex> guard (telemetry.mutex);
		single_requests_count = telemetry.single_requests.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	if (telemetry.batch_telemetry)
	{
		composite->add_component (collect_container_info (*telemetry.batch_telemetry, "batch_telemetry"));
	}
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "single_requests", single_requests_count, sizeof (decltype (telemetry.single_requests)::value_type) }));
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
		all_telemetry_data_count = telemetry_impl.all_telemetry_data.size ();
		cached_telemetry_data_count = telemetry_impl.cached_telemetry_data.size ();
		required_responses_count = telemetry_impl.required_responses.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "callbacks", callback_count, sizeof (decltype (telemetry_impl.callbacks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "all_telemetry_data", all_telemetry_data_count, sizeof (decltype (telemetry_impl.all_telemetry_data)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cached_telemetry_data", cached_telemetry_data_count, sizeof (decltype (telemetry_impl.cached_telemetry_data)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "required_responses", required_responses_count, sizeof (decltype (telemetry_impl.required_responses)::value_type) }));
	return composite;
}
