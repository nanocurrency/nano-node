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

/*
 * This class requests node telemetry metrics from square root number of nodes and invokes any callbacks
 * which have been aggregated. Further calls to get_metrics_async may return cached telemetry metrics
 * if they are within cache_cutoff time from the latest request.
 */
class telemetry : public std::enable_shared_from_this<telemetry>
{
public:
	telemetry (nano::network & network_a, nano::alarm & alarm_a, nano::worker & worker_a);

	/*
	 * It's possible that callbacks can be called immediately if there are cached values present.
	 */
	void get_metrics_async (std::function<void(std::vector<nano::telemetry_data> const &, bool)> const & callback);

	/*
	 * Add telemetry metrics received from this endpoint.
	 * Should this be unsolicited, it will not be added.
	 */
	void add (nano::telemetry_data const & telemetry_data_a, nano::endpoint const & endpoint_a);

	/*
	 * Return the number of node metrics collected
	 */
	size_t telemetry_data_size ();

private:
	// Anything older than this requires requesting metrics from other nodes
	nano::network_params network_params;
	std::chrono::milliseconds const cache_cutoff{ network_params.network.is_test_network () ? 1000 : 5000 };

	// All data in this chunk is protected by this mutex
	std::mutex mutex;
	std::vector<std::function<void(std::vector<nano::telemetry_data> const &, bool)>> callbacks;
	std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now () - cache_cutoff;
	std::vector<nano::telemetry_data> all_telemetry_data;
	std::vector<nano::telemetry_data> cached_telemetry_data;
	std::unordered_set<nano::endpoint> required_responses;
	uint64_t round{ 0 };

	nano::network & network;
	nano::alarm & alarm;
	nano::worker & worker;

	void invoke_callbacks (bool cached_a);
	void channel_processed (nano::unique_lock<std::mutex> & lk_a, nano::endpoint const & endpoint_a);
	void fire_callbacks (nano::unique_lock<std::mutex> & lk);
	void fire_messages (std::unordered_set<std::shared_ptr<nano::transport::channel>> const & channels);

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (telemetry &, const std::string &);
};

std::unique_ptr<nano::seq_con_info_component> collect_seq_con_info (telemetry & telemetry, const std::string & name);
}