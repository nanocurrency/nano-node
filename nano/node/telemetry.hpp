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

class telemetry_data_time_pair
{
public:
	nano::telemetry_data data;
	std::chrono::steady_clock::time_point last_updated;
	std::chrono::system_clock::time_point system_last_updated;
	bool operator== (telemetry_data_time_pair const &) const;
	bool operator!= (telemetry_data_time_pair const &) const;
};

/*
 * Holds a response from a telemetry request
 */
class telemetry_data_response
{
public:
	nano::telemetry_data_time_pair telemetry_data_time_pair;
	nano::endpoint endpoint;
	bool error{ true };
};

/*
 * Holds many responses from telemetry requests
 */
class telemetry_data_responses
{
public:
	std::unordered_map<nano::endpoint, telemetry_data_time_pair> telemetry_data_time_pairs;
	bool all_received{ false };
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
	void get_metrics_async (std::deque<std::shared_ptr<nano::transport::channel>> const & channels_a, std::function<void(telemetry_data_responses const &)> const & callback_a);
	void add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a, bool is_empty_a);
	size_t telemetry_data_size ();

	nano::network_params network_params;
	// Anything older than this requires requesting metrics from other nodes.
	std::chrono::seconds const cache_cutoff{ nano::telemetry_cache_cutoffs::network_to_time (network_params.network) };
	static std::chrono::seconds constexpr alarm_cutoff{ 3 };

	// All data in this chunk is protected by this mutex
	std::mutex mutex;
	std::vector<std::function<void(telemetry_data_responses const &)>> callbacks;
	std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now () - cache_cutoff;
	/* The responses received during this request round */
	std::unordered_map<nano::endpoint, telemetry_data_time_pair> current_telemetry_data_responses;
	/* The metrics for the last request round */
	std::unordered_map<nano::endpoint, telemetry_data_time_pair> cached_telemetry_data;
	std::unordered_set<nano::endpoint> required_responses;
	uint64_t round{ 0 };
	/* Currently executing callbacks */
	bool invoking{ false };
	std::vector<nano::endpoint> failed;

	nano::network & network;
	nano::alarm & alarm;
	nano::worker & worker;

	std::function<void(std::unordered_map<nano::endpoint, telemetry_data_time_pair> & data_a, std::mutex &)> pre_callback_callback;

	void invoke_callbacks ();
	void channel_processed (nano::unique_lock<std::mutex> & lk_a, nano::endpoint const & endpoint_a);
	void flush_callbacks_async ();
	void fire_request_messages (std::deque<std::shared_ptr<nano::transport::channel>> const & channels);

	friend std::unique_ptr<container_info_component> collect_container_info (telemetry_impl &, const std::string &);
	friend nano::telemetry;
	friend class node_telemetry_single_request_Test;
	friend class node_telemetry_basic_Test;
	friend class node_telemetry_ongoing_requests_Test;
};

std::unique_ptr<nano::container_info_component> collect_container_info (telemetry_impl & telemetry_impl, const std::string & name);

/*
 * This class has 2 main operations:
 * Request metrics from specific single peers (single_requests)
 *  - If this peer is in the batched request, it will use the value from that, otherwise send a telemetry_req message (non-droppable)
 * Request metrics from all peers (batched_request)
 *  - This is polled every minute.
 *  - If a single request is currently underway, do not request because other peers will just reject if within a hotzone time
 *    - This will be proactively added when callbacks are called inside pre_callback_callback 
 */
class telemetry
{
public:
	telemetry (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a);

	/*
	 * Add telemetry metrics received from this endpoint.
	 * Should this be unsolicited, it will not be added.
	 * Some peers may have disabled responding with telemetry data, need to account for this
	 */
	void add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a, bool is_empty_a);

	/*
	 * Collects metrics from all known peers and invokes the callback when complete.
	 */
	void get_metrics_peers_async (std::function<void(telemetry_data_responses const &)> const & callback_a);

	/*
	 * A blocking version of get_metrics_peers_async ().
	 */
	telemetry_data_responses get_metrics_peers ();

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
	 * Return the number of finished_single_requests elements
	 */
	size_t finished_single_requests_size ();

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
		std::shared_ptr<telemetry_impl> impl;
		std::chrono::steady_clock::time_point last_updated{ std::chrono::steady_clock::now () };
	};

	std::mutex mutex;
	/* Requests telemetry data from a random selection of peers */
	std::shared_ptr<telemetry_impl> batch_request;
	/* Any requests to specific individual peers is maintained here */
	std::unordered_map<nano::endpoint, single_request_data> single_requests;
	/* This holds data from single_requests after the cache is removed */
	std::unordered_map<nano::endpoint, telemetry_data_time_pair> finished_single_requests;
	bool stopped{ false };

	void update_cleanup_data (nano::endpoint const & endpoint_a, nano::telemetry::single_request_data & single_request_data_a, bool is_new_a);
	void ongoing_single_request_cleanup (nano::endpoint const & endpoint_a, nano::telemetry::single_request_data const & single_request_data_a);
	void ongoing_req_all_peers ();

	friend class node_telemetry_multiple_single_request_clearing_Test;
	friend std::unique_ptr<container_info_component> collect_container_info (telemetry &, const std::string &);
};

std::unique_ptr<nano::container_info_component> collect_container_info (telemetry & telemetry, const std::string & name);

nano::telemetry_data consolidate_telemetry_data (std::vector<telemetry_data> const & telemetry_data);
nano::telemetry_data_time_pair consolidate_telemetry_data_time_pairs (std::vector<telemetry_data_time_pair> const & telemetry_data_time_pairs);
}