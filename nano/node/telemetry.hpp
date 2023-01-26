#pragma once

#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/nodeconfig.hpp>
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
class node;
class network;
class node_observers;
class stats;
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
		bool enable_ongoing_broadcasts{ true };

		config (nano::node_config const & config, nano::node_flags const & flags) :
			enable_ongoing_requests{ !flags.disable_ongoing_telemetry_requests },
			enable_ongoing_broadcasts{ !flags.disable_providing_telemetry_metrics }
		{
		}
	};

public:
	telemetry (config const &, nano::node &, nano::network &, nano::node_observers &, nano::network_params &, nano::stats &);
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
	nano::node & node;
	nano::network & network;
	nano::node_observers & observers;
	nano::network_params & network_params;
	nano::stats & stats;

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
	bool request_predicate () const;
	bool broadcast_predicate () const;

	void run ();
	void run_requests ();
	void run_broadcasts ();
	void cleanup ();

	void request (std::shared_ptr<nano::transport::channel> &);
	void broadcast (std::shared_ptr<nano::transport::channel> &, nano::telemetry_data const &);

	bool verify (nano::telemetry_ack const &, std::shared_ptr<nano::transport::channel> const &) const;
	bool check_timeout (entry const &) const;

private:
	// clang-format off
	class tag_sequenced {};
	class tag_endpoint {};

	using ordered_telemetries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<entry, nano::endpoint, &entry::endpoint>>
	>>;
	// clang-format on

	ordered_telemetries telemetries;

	bool triggered{ false };
	std::chrono::steady_clock::time_point last_request{};
	std::chrono::steady_clock::time_point last_broadcast{};

	bool stopped{ false };
	mutable nano::mutex mutex{ mutex_identifier (mutexes::telemetry) };
	nano::condition_variable condition;
	std::thread thread;

private:
	static std::size_t constexpr max_size = 1024;
};

nano::telemetry_data consolidate_telemetry_data (std::vector<telemetry_data> const & telemetry_data);
}
