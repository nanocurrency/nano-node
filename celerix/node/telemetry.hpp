#pragma once

#include <celerix/lib/utility.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/messages.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace mi = boost::multi_index;

namespace celerix
{
class telemetry_config final
{
public:
	bool enable_ongoing_requests{ false }; // TODO: No longer used, remove
	bool enable_ongoing_broadcasts{ true };

public:
	explicit telemetry_config (celerix::node_flags const & flags) :
		enable_ongoing_broadcasts{ !flags.disable_providing_telemetry_metrics }
	{
	}
};

/**
 * This class periodically broadcasts and requests telemetry from peers.
 * Those intervals are configurable via `telemetry_request_interval` & `telemetry_broadcast_interval` network constants
 * Telemetry datas are only removed after becoming stale (configurable via `telemetry_cache_cutoff` network constant), so peer data will still be available for a short period after that peer is disconnected
 *
 * Broadcasts can be disabled via `disable_providing_telemetry_metrics` node flag
 *
 */
class telemetry
{
public:
	telemetry (celerix::node_flags const &, celerix::node &, celerix::network &, celerix::node_observers &, celerix::network_params &, celerix::stats &);
	~telemetry ();

	void start ();
	void stop ();

	/**
	 * Process telemetry message from network
	 */
	void process (celerix::telemetry_ack const &, std::shared_ptr<celerix::transport::channel> const &);

	/**
	 * Trigger manual telemetry request to all peers
	 */
	void trigger ();

	std::size_t size () const;

	/**
	 * Returns telemetry for selected endpoint
	 */
	std::optional<celerix::telemetry_data> get_telemetry (celerix::endpoint const &) const;

	/**
	 * Returns all available telemetry
	 */
	std::unordered_map<celerix::endpoint, celerix::telemetry_data> get_all_telemetries () const;

	celerix::container_info container_info () const;

private: // Dependencies
	telemetry_config const config;
	celerix::node & node;
	celerix::network & network;
	celerix::node_observers & observers;
	celerix::network_params & network_params;
	celerix::stats & stats;

private:
	struct entry
	{
		std::shared_ptr<celerix::transport::channel> channel;
		celerix::telemetry_data data;
		std::chrono::steady_clock::time_point last_updated;

		celerix::endpoint endpoint () const
		{
			return channel->get_remote_endpoint ();
		}
	};

private:
	bool request_predicate () const;
	bool broadcast_predicate () const;

	void run ();
	void run_requests ();
	void run_broadcasts ();
	void cleanup ();

	void request (std::shared_ptr<celerix::transport::channel> const &);
	void broadcast (std::shared_ptr<celerix::transport::channel> const &, celerix::telemetry_data const &);

	bool verify (celerix::telemetry_ack const &, std::shared_ptr<celerix::transport::channel> const &) const;
	bool check_timeout (entry const &) const;

private:
	// clang-format off
	class tag_sequenced {};
	class tag_channel {};
	class tag_endpoint {};

	using ordered_telemetries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_unique<mi::tag<tag_channel>,
			mi::member<entry,  std::shared_ptr<celerix::transport::channel>, &entry::channel>>,
		mi::hashed_non_unique<mi::tag<tag_endpoint>,
			mi::const_mem_fun<entry, celerix::endpoint, &entry::endpoint>>
	>>;
	// clang-format on

	ordered_telemetries telemetries;

	bool triggered{ false };
	std::chrono::steady_clock::time_point last_request{};
	std::chrono::steady_clock::time_point last_broadcast{};

	bool stopped{ false };
	mutable celerix::mutex mutex{ mutex_identifier (mutexes::telemetry) };
	celerix::condition_variable condition;
	std::thread thread;

private:
	static std::size_t constexpr max_size = 1024;
};
}
