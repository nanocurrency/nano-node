#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/observer_set.hpp>
#include <celerix/node/fair_queue.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/messages.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace celerix
{
class bootstrap_server_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig &);
	celerix::error serialize (celerix::tomlconfig &) const;

public:
	size_t max_queue{ 16 };
	size_t threads{ 1 };
	size_t batch_size{ 64 };
};

/**
 * Processes bootstrap requests (`asc_pull_req` messages) and replies with bootstrap responses (`asc_pull_ack`)
 */
class bootstrap_server final
{
public:
	bootstrap_server (bootstrap_server_config const &, celerix::store::component &, celerix::ledger &, celerix::network_constants const &, celerix::stats &);
	~bootstrap_server ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_req` message coming from network.
	 * Reply will be sent back over passed in `channel`
	 */
	bool request (celerix::asc_pull_req const & message, std::shared_ptr<celerix::transport::channel> const & channel);

public: // Events
	celerix::observer_set<celerix::asc_pull_ack const &, std::shared_ptr<celerix::transport::channel> const &> on_response;

private:
	// `asc_pull_req` message is small, store by value
	using request_t = std::pair<celerix::asc_pull_req, std::shared_ptr<celerix::transport::channel>>; // <request, response channel>

	void run ();
	void run_batch (celerix::unique_lock<celerix::mutex> & lock);
	celerix::asc_pull_ack process (secure::transaction const &, celerix::asc_pull_req const & message);
	void respond (celerix::asc_pull_ack &, std::shared_ptr<celerix::transport::channel> const &);

	celerix::asc_pull_ack process (secure::transaction const &, celerix::asc_pull_req::id_t id, celerix::empty_payload const & request);

	/*
	 * Blocks request
	 */
	celerix::asc_pull_ack process (secure::transaction const &, celerix::asc_pull_req::id_t id, celerix::asc_pull_req::blocks_payload const & request) const;
	celerix::asc_pull_ack prepare_response (secure::transaction const &, celerix::asc_pull_req::id_t id, celerix::block_hash start_block, std::size_t count) const;
	celerix::asc_pull_ack prepare_empty_blocks_response (celerix::asc_pull_req::id_t id) const;
	std::deque<std::shared_ptr<celerix::block>> prepare_blocks (secure::transaction const &, celerix::block_hash start_block, std::size_t count) const;

	/*
	 * Account info request
	 */
	celerix::asc_pull_ack process (secure::transaction const &, celerix::asc_pull_req::id_t id, celerix::asc_pull_req::account_info_payload const & request) const;

	/*
	 * Frontiers request
	 */
	celerix::asc_pull_ack process (secure::transaction const &, celerix::asc_pull_req::id_t id, celerix::asc_pull_req::frontiers_payload const & request) const;

	/*
	 * Checks if the request should be dropped early on
	 */
	bool verify (celerix::asc_pull_req const & message) const;
	bool verify_request_type (celerix::asc_pull_type) const;

private: // Dependencies
	bootstrap_server_config const & config;
	celerix::store::component & store;
	celerix::ledger & ledger;
	celerix::network_constants const & network_constants;
	celerix::stats & stats;

private:
	celerix::fair_queue<request_t, celerix::no_value> queue;

	std::atomic<bool> stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::vector<std::thread> threads;

public: // Config
	/** Maximum number of blocks to send in a single response, cannot be higher than capacity of a single `asc_pull_ack` message */
	constexpr static std::size_t max_blocks = celerix::asc_pull_ack::blocks_payload::max_blocks;
	constexpr static std::size_t max_frontiers = celerix::asc_pull_ack::frontiers_payload::max_frontiers;
};

celerix::stat::detail to_stat_detail (celerix::asc_pull_type);
}
