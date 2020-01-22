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
class telemetry;
namespace transport
{
	class channel;
}

/*
 * Holds a response from a telemetry request
 */
class telemetry_data_response
{
public:
	nano::telemetry_data data;
	bool is_cached;
	bool error;
};

/*
 * Holds many responses from telemetry requests
 */
class telemetry_data_responses
{
public:
	std::vector<nano::telemetry_data> data;
	bool is_cached;
	bool all_received;
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

private:
	// Class only available to the telemetry class
	void get_metrics_async (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels_a, std::function<void(telemetry_data_responses const &)> const & callback_a);
	void add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a);
	size_t telemetry_data_size ();

	nano::network_params network_params;
	// Anything older than this requires requesting metrics from other nodes
	std::chrono::milliseconds const cache_cutoff{ network_params.network.is_test_network () ? 500 : 3000 };

	// All data in this chunk is protected by this mutex
	std::mutex mutex;
	std::vector<std::function<void(telemetry_data_responses const &)>> callbacks;
	std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now () - cache_cutoff;
	/* The responses received during this request round */
	std::vector<nano::telemetry_data> current_telemetry_data_responses;
	/* The metrics for the last request round */
	std::vector<nano::telemetry_data> cached_telemetry_data;
	std::unordered_set<nano::endpoint> required_responses;
	uint64_t round{ 0 };

	std::atomic<bool> all_received{ true };

	nano::network & network;
	nano::alarm & alarm;
	nano::worker & worker;

	void invoke_callbacks (bool cached_a);
	void channel_processed (nano::unique_lock<std::mutex> & lk_a, nano::endpoint const & endpoint_a);
	void fire_callbacks (nano::unique_lock<std::mutex> & lk);
	void fire_request_messages (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels);

	friend std::unique_ptr<container_info_component> collect_container_info (telemetry_impl &, const std::string &);
	friend nano::telemetry;
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
	void get_metrics_random_peers_async (std::function<void(telemetry_data_responses const &)> const & callback_a);

	/*
	 * A blocking version of get_metrics_random_peers_async ().
	 */
	telemetry_data_responses get_metrics_random_peers ();

	/*
	 * This makes a telemetry request to the specific channel
	 */
	void get_metrics_single_peer_async (std::shared_ptr<nano::transport::channel> const &, std::function<void(telemetry_data_response const &)> const & callback_a);

	/*
	 * A blocking version of get_metrics_single_peer_async
	 */
	telemetry_data_response get_metrics_single_peer (std::shared_ptr<nano::transport::channel> const &);

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

	class single_request_data
	{
	public:
		std::shared_ptr<telemetry_impl> telemetry_impl;
		std::chrono::steady_clock::time_point last_updated{ std::chrono::steady_clock::now () };
	};

	std::mutex mutex;
	/* Requests telemetry data from a random selection of peers */
	std::shared_ptr<telemetry_impl> batch_request;
	/* Any requests to specific individual peers is maintained here */
	std::unordered_map<nano::endpoint, single_request_data> single_requests;
	bool stopped{ false };

	void update_cleanup_data (nano::endpoint const & endpoint_a, nano::telemetry::single_request_data & single_request_data_a, bool is_new_a);
	void ongoing_single_request_cleanup (nano::endpoint const & endpoint_a, nano::telemetry::single_request_data const & single_request_data_a);

	friend class node_telemetry_multiple_single_request_clearing_Test;
	friend std::unique_ptr<container_info_component> collect_container_info (telemetry &, const std::string &);
};

std::unique_ptr<nano::container_info_component> collect_container_info (telemetry & telemetry, const std::string & name);
}