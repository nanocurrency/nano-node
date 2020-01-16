#pragma once

#include <nano/node/common.hpp>
#include <nano/secure/common.hpp>

#include <functional>
#include <memory>
#include <unordered_set>

namespace nano
{
class network;
class alarm;
class worker;
namespace transport
{
	class channel;
}

class single_metric_data
{
public:
	nano::telemetry_data data;
	bool is_cached;
	bool error;
};

class batched_metric_data
{
public:
	std::vector<nano::telemetry_data> data;
	bool is_cached;
	bool error;
};

/*
 * This class requests node telemetry metrics and invokes any callbacks
 * which have been aggregated. Further calls to get_metrics_async may return cached telemetry metrics
 * if they are within cache_cutoff time from the latest request.
 */
class telemetry_impl : public std::enable_shared_from_this<telemetry_impl>
{
public:
	telemetry_impl (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a);

	void get_metrics_async (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels_a, std::function<void(batched_metric_data const &)> const & callback_a);
	void add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a);
	size_t telemetry_data_size ();

private:
	nano::network_params network_params;
	// Anything older than this requires requesting metrics from other nodes
	std::chrono::milliseconds const cache_cutoff{ network_params.network.is_test_network () ? 1000 : 5000 };

	// All data in this chunk is protected by this mutex
	std::mutex mutex;
	std::vector<std::function<void(batched_metric_data const &)>> callbacks;
	std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now () - cache_cutoff;
	std::vector<nano::telemetry_data> all_telemetry_data;
	std::vector<nano::telemetry_data> cached_telemetry_data;
	std::unordered_set<nano::endpoint> required_responses;
	uint64_t round{ 0 };

	nano::network & network;
	nano::alarm & alarm;
	nano::worker & worker;

	void invoke_callbacks (bool cached_a, bool error_a);
	void channel_processed (nano::unique_lock<std::mutex> & lk_a, nano::endpoint const & endpoint_a, bool error_a);
	void fire_callbacks (nano::unique_lock<std::mutex> & lk, bool error_a);
	void fire_request_messages (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels);

	friend std::unique_ptr<container_info_component> collect_container_info (telemetry_impl &, const std::string &);
};

std::unique_ptr<nano::container_info_component> collect_container_info (telemetry_impl & telemetry_impl, const std::string & name);

class telemetry
{
public:
	telemetry (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a);

	/*
	 * Add telemetry metrics received from this endpoint.
	 * Should this be unsolicited, it will not be added.
	 */
	void add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a);

	/*
	 * Collects metrics from square root number of peers and invokes the callback when complete.
	 */
	void get_random_metrics_async (std::function<void(batched_metric_data const &)> const & callback_a);

	/*
	 * A blocking version of get_random_metrics_async ().
	 */
	batched_metric_data get_random_metrics ();

	/*
	 * This makes a telemetry request to the specific channel
	 */
	void get_single_metric_async (std::shared_ptr<nano::transport::channel> const &, std::function<void(single_metric_data const &)> const & callback_a);

	/*
	 * A blocking version of get_single_metric_async
	 */
	single_metric_data get_single_metric (std::shared_ptr<nano::transport::channel> const &);

	/*
	 * Return the number of node metrics collected
	 */
	size_t telemetry_data_size ();

	/*
	 * Stop the telemetry processor
	 */
	void stop ();

private:
	nano::network & network;
	nano::alarm & alarm;
	nano::worker & worker;

	nano::network_params network_params;

	std::mutex mutex;
	std::shared_ptr<telemetry_impl> batch_telemetry;
	std::unordered_map<nano::endpoint, std::shared_ptr<telemetry_impl>> single_requests;
	bool stopped{ false };

	friend std::unique_ptr<container_info_component> collect_container_info (telemetry &, const std::string &);
};

std::unique_ptr<nano::container_info_component> collect_container_info (telemetry & telemetry, const std::string & name);
}