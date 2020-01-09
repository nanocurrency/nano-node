#include <nano/lib/alarm.hpp>
#include <nano/lib/worker.hpp>
#include <nano/node/network.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/buffer.hpp>

#include <cassert>
#include <cstdint>

nano::telemetry::telemetry (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a) :
network (network_a),
alarm (alarm_a),
worker (worker_a)
{
}

void nano::telemetry::get_metrics_async (std::function<void(std::vector<nano::telemetry_data> const &, bool)> const & callback)
{
	auto random_peers = network.random_set (network.size_sqrt (), network_params.protocol.telemetry_protocol_version_min);

	std::deque<std::shared_ptr<nano::transport::channel>> d;
	{
		nano::unique_lock<std::mutex> lk (mutex);
		callbacks.push_back (callback);
		if (callbacks.size () > 1)
		{
			// This means we already have at least one pending result already, so it will handle calls this callback when it completes
			return;
		}
		else if (random_peers.empty ())
		{
			fire_callbacks (lk);
			return;
		}

		// Check if we can just return cached results
		if (std::chrono::steady_clock::now () < (last_time + cache_cutoff))
		{
			// Post to worker so that it's truly async and not on the calling thread (same problem as std::async otherwise)
			worker.push_task ([this_w = std::weak_ptr<nano::telemetry> (shared_from_this ())]() {
				if (auto this_l = this_w.lock ())
				{
					// Just invoke all callbacks, it's possible that during the mutex unlock other callbacks were added, so check again
					nano::unique_lock<std::mutex> lk (this_l->mutex);
					while (!this_l->callbacks.empty ())
					{
						lk.unlock ();
						this_l->invoke_callbacks (true);
						lk.lock ();
					}
				}
			});
			return;
		}

		assert (required_responses.empty ());
		std::transform (random_peers.begin (), random_peers.end (), std::inserter (required_responses, required_responses.end ()), [](auto const & channel) {
			return channel->get_endpoint ();
		});
	}

	fire_messages (random_peers);
}

void nano::telemetry::add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a)
{
	nano::unique_lock<std::mutex> lk (mutex);
	if (required_responses.find (endpoint_a) == required_responses.cend ())
	{
		// Not requesting telemetry data from this channel so shouldn't be receiving it
		return;
	}

	// Received telemetry data from a peer
	all_telemetry_data.push_back (telemetry_data_a);

	channel_processed (lk, endpoint_a);
}

void nano::telemetry::invoke_callbacks (bool cached_a)
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
		callback (cached_telemetry_data, cached_a);
	}
}

void nano::telemetry::channel_processed (nano::unique_lock<std::mutex> & lk_a, nano::endpoint const & endpoint_a)
{
	assert (lk_a.owns_lock ());
	auto num_removed = required_responses.erase (endpoint_a);
	if (required_responses.empty ())
	{
		fire_callbacks (lk_a);
	}
}

void nano::telemetry::fire_callbacks (nano::unique_lock<std::mutex> & lk)
{
	assert (lk.owns_lock ());
	cached_telemetry_data = all_telemetry_data;

	// Can just use the cached values rather than request new ones
	last_time = std::chrono::steady_clock::now ();
	lk.unlock ();
	invoke_callbacks (false);
	lk.lock ();
}

void nano::telemetry::fire_messages (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels)
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

		std::weak_ptr<nano::telemetry> this_w (shared_from_this ());
		channel->send (message, [this_w, endpoint = channel->get_endpoint (), round_l](boost::system::error_code const & ec, size_t size_a) {
			if (auto this_l = this_w.lock ())
			{
				if (ec)
				{
					// Error sending the telemetry_req message
					nano::unique_lock<std::mutex> lk (this_l->mutex);
					this_l->channel_processed (lk, endpoint);
				}
				else
				{
					// If no response is seen after a certain period of time, remove it from the list of expected responses. However, only if it is part of the same round.
					this_l->alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (2000), [this_w, endpoint, round_l]() {
						// if in request params, remove it
						if (auto this_l = this_w.lock ())
						{
							nano::unique_lock<std::mutex> lk (this_l->mutex);
							if (this_l->round == round_l && this_l->required_responses.find (endpoint) != this_l->required_responses.cend ())
							{
								this_l->channel_processed (lk, endpoint);
							}
						}
					});
				}
			}
		});
	}
}

size_t nano::telemetry::telemetry_data_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return all_telemetry_data.size ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (telemetry & telemetry, const std::string & name)
{
	size_t callback_count;
	size_t all_telemetry_data_count;
	size_t cached_telemetry_data_count;
	size_t required_responses_count;
	{
		nano::lock_guard<std::mutex> guard (telemetry.mutex);
		callback_count = telemetry.callbacks.size ();
		all_telemetry_data_count = telemetry.all_telemetry_data.size ();
		cached_telemetry_data_count = telemetry.cached_telemetry_data.size ();
		required_responses_count = telemetry.required_responses.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "callbacks", callback_count, sizeof (decltype (telemetry.callbacks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "all_telemetry_data", all_telemetry_data_count, sizeof (decltype (telemetry.all_telemetry_data)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cached_telemetry_data", cached_telemetry_data_count, sizeof (decltype (telemetry.cached_telemetry_data)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "required_responses", required_responses_count, sizeof (decltype (telemetry.required_responses)::value_type) }));
	return composite;
}
