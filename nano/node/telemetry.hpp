#pragma once

#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace mi = boost::multi_index;

namespace nano
{
class network;
class node_observers;
class stat;
class ledger;
class thread_pool;
class unchecked_map;
namespace transport
{
	class channel;
}

/**
 * TODO: Update description
 *
 * This class requests node telemetry metrics from peers and invokes any callbacks which have been aggregated.
 * All calls to get_metrics return cached data, it does not do any requests, these are periodically done in
 * ongoing_req_all_peers. This can be disabled with the disable_ongoing_telemetry_requests node flag.
 * Calls to get_metrics_single_peer_async will wait until a response is made if it is not within the cache
 * cut off.
 */
class telemetry
{
public:
	struct config
	{
		bool enable_ongoing_requests{ true };
		/// milliseconds
		uint32_t request_interval{ 0 };
		/// milliseconds
		uint32_t cache_cutoff{ 0 };
	};

public:
	telemetry (config const &, nano::network &, nano::node_observers &, nano::network_params &, nano::stat &);
	~telemetry ();

	void start ();
	void stop ();

	/**
	 * Process telemetry message from network
	 */
	void process (nano::telemetry_ack const &, std::shared_ptr<nano::transport::channel> const &);

	/**
	 * Trigger manual telemetry request to all peers
	 */
	void trigger ();

	std::size_t size () const;

	/**
	 * Returns telemetry for selected endpoint
	 */
	std::optional<nano::telemetry_data> get_telemetry (nano::endpoint const &) const;

	/**
	 * Returns all available telemetry
	 */
	std::unordered_map<nano::endpoint, nano::telemetry_data> get_all_telemetries () const;

public: // Container info
	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);

private: // Dependencies
	nano::network & network;
	nano::node_observers & observers;
	nano::network_params & network_params;
	nano::stat & stats;

	const config config_m;

private:
	struct entry
	{
		nano::endpoint endpoint;
		nano::telemetry_data data;
		std::chrono::steady_clock::time_point last_updated;
		std::shared_ptr<nano::transport::channel> channel;
	};

private:
	void run ();
	void run_requests ();
	void cleanup ();

	void request (std::shared_ptr<nano::transport::channel> &);

	bool verify (nano::telemetry_ack const &, std::shared_ptr<nano::transport::channel> const &) const;
	bool check_timeout (entry const &) const;

private:
	// clang-format off
	class tag_sequenced {};
	class tag_endpoint {};
	class tag_last_updated {};

	using ordered_telemetries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<entry, nano::endpoint, &entry::endpoint>>,
		mi::ordered_non_unique<mi::tag<tag_last_updated>,
			mi::member<entry, std::chrono::steady_clock::time_point, &entry::last_updated>>
	>>;
	// clang-format on

	ordered_telemetries telemetries;

	std::atomic<bool> triggered;

	std::atomic<bool> stopped{ false };
	mutable nano::mutex mutex{ mutex_identifier (mutexes::telemetry) };
	mutable nano::condition_variable condition;
	std::thread thread;

private:
	static std::size_t constexpr max_size = 1024;
};

nano::telemetry_data consolidate_telemetry_data (std::vector<telemetry_data> const & telemetry_data);
nano::telemetry_data local_telemetry_data (nano::ledger const & ledger_a, nano::network &, nano::unchecked_map const &, uint64_t, nano::network_params const &, std::chrono::steady_clock::time_point, uint64_t, nano::keypair const &);
}
