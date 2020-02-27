#pragma once

#include <nano/node/common.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <functional>
#include <memory>

namespace mi = boost::multi_index;

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
 * Holds a response from a telemetry request
 */
class telemetry_data_response
{
public:
	nano::telemetry_data telemetry_data;
	nano::endpoint endpoint;
	bool error{ true };
};

class telemetry_info final
{
public:
	telemetry_info () = default;
	telemetry_info (nano::endpoint const & endpoint, nano::telemetry_data const & data, std::chrono::steady_clock::time_point last_request, bool undergoing_request);
	bool awaiting_first_response () const;

	nano::endpoint endpoint;
	nano::telemetry_data data;
	std::chrono::steady_clock::time_point last_request;
	bool undergoing_request{ false };
	uint64_t round{ 0 };
};

/*
 * This class requests node telemetry metrics from peers and invokes any callbacks which have been aggregated.
 * All calls to get_metrics return cached data, it does not do any requests, these are periodically done in
 * ongoing_req_all_peers. This can be disabled with the disable_ongoing_telemetry_requests node flag.
 * Calls to get_metrics_single_peer_async will wait until a response is made if it is not within the cache
 * cut off.
 */
class telemetry : public std::enable_shared_from_this<telemetry>
{
public:
	telemetry (nano::network &, nano::alarm &, nano::worker &, bool);
	void start ();
	void stop ();

	/*
	 * Set the telemetry data associated with this peer
	 */
	void set (nano::telemetry_data const &, nano::endpoint const &, bool);

	/*
	 * This returns what ever is in the cache
	 */
	std::unordered_map<nano::endpoint, nano::telemetry_data> get_metrics ();

	/*
	 * This makes a telemetry request to the specific channel
	 */
	void get_metrics_single_peer_async (std::shared_ptr<nano::transport::channel> const &, std::function<void(telemetry_data_response const &)> const &);

	/*
	 * A blocking version of get_metrics_single_peer_async
	 */
	telemetry_data_response get_metrics_single_peer (std::shared_ptr<nano::transport::channel> const &);

	/*
	 * Return the number of node metrics collected
	 */
	size_t telemetry_data_size ();

private:
	class tag_endpoint
	{
	};
	class tag_last_updated
	{
	};

	nano::network & network;
	nano::alarm & alarm;
	nano::worker & worker;

	std::atomic<bool> stopped{ false };
	nano::network_params network_params;
	bool disable_ongoing_requests;

	std::mutex mutex;
	// clang-format off
	// This holds the last telemetry data received from peers or can be a placeholder awaiting the first response (check with awaiting_first_response ())
	boost::multi_index_container<nano::telemetry_info,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<nano::telemetry_info, nano::endpoint, &nano::telemetry_info::endpoint>>,
		mi::ordered_non_unique<mi::tag<tag_last_updated>,
			mi::member<nano::telemetry_info, std::chrono::steady_clock::time_point, &nano::telemetry_info::last_request>>>> recent_or_initial_request_telemetry_data;
	// clang-format on

	// Anything older than this requires requesting metrics from other nodes.
	std::chrono::seconds const cache_cutoff{ nano::telemetry_cache_cutoffs::network_to_time (network_params.network) };
	std::chrono::seconds const response_time_cutoff{ is_sanitizer_build || nano::running_within_valgrind () ? 6 : 3 };

	std::unordered_map<nano::endpoint, std::vector<std::function<void(telemetry_data_response const &)>>> callbacks;

	void ongoing_req_all_peers (std::chrono::milliseconds);

	void fire_request_message (std::shared_ptr<nano::transport::channel> const & channel);
	void channel_processed (nano::endpoint const &, bool);
	void flush_callbacks_async (nano::endpoint const &, bool);
	void invoke_callbacks (nano::endpoint const &, bool);

	bool within_cache_cutoff (nano::telemetry_info const &) const;
	friend std::unique_ptr<nano::container_info_component> collect_container_info (telemetry & telemetry, const std::string & name);
};

std::unique_ptr<nano::container_info_component> collect_container_info (telemetry & telemetry, const std::string & name);

nano::telemetry_data consolidate_telemetry_data (std::vector<telemetry_data> const & telemetry_data);
nano::telemetry_data local_telemetry_data (nano::ledger_cache const &, nano::network &, uint64_t, nano::network_params const &, std::chrono::steady_clock::time_point);
}