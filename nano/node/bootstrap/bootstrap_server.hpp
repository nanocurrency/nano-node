#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace nano
{
class bootstrap_server_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ true };
	size_t max_queue{ 16 };
	size_t threads{ 1 };
	size_t batch_size{ 64 };
	size_t limiter{ 500 };
};

/**
 * Processes bootstrap requests (`asc_pull_req` messages) and replies with bootstrap responses (`asc_pull_ack`)
 */
class bootstrap_server final
{
public:
	bootstrap_server (bootstrap_server_config const &, nano::store::ledger_store &, nano::ledger &, nano::network_constants const &, nano::stats &);
	~bootstrap_server ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_req` message coming from network.
	 * Reply will be sent back over passed in `channel`
	 */
	bool request (nano::messages::asc_pull_req const & message, std::shared_ptr<nano::transport::channel> const & channel);

public: // Events
	nano::observer_set<nano::messages::asc_pull_ack const &, std::shared_ptr<nano::transport::channel> const &> on_response;

private:
	// `asc_pull_req` message is small, store by value
	using request_t = std::pair<nano::messages::asc_pull_req, std::shared_ptr<nano::transport::channel>>; // <request, response channel>

	void run ();
	void run_batch (nano::unique_lock<nano::mutex> & lock);
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req const & message);
	void respond (nano::messages::asc_pull_ack &, std::shared_ptr<nano::transport::channel> const &);

	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::empty_payload const & request);

	/*
	 * Blocks request
	 */
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::blocks_payload const & request) const;
	nano::messages::asc_pull_ack prepare_response (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::block_hash start_block, std::size_t count) const;
	nano::messages::asc_pull_ack prepare_empty_blocks_response (nano::messages::asc_pull_req::id_t id) const;
	std::deque<std::shared_ptr<nano::block>> prepare_blocks (secure::transaction const &, nano::block_hash start_block, std::size_t count) const;

	/*
	 * Account info request
	 */
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::account_info_payload const & request) const;

	/*
	 * Frontiers request
	 */
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::frontiers_payload const & request) const;

	/*
	 * Checks if the request should be dropped early on
	 */
	bool verify (nano::messages::asc_pull_req const & message) const;
	bool verify_request_type (nano::messages::asc_pull_type) const;

private: // Dependencies
	bootstrap_server_config const & config;
	nano::store::ledger_store & store;
	nano::ledger & ledger;
	nano::network_constants const & network_constants;
	nano::stats & stats;

private:
	nano::fair_queue<request_t, nano::no_value> queue;
	nano::rate_limiter limiter;

	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::vector<std::thread> threads;

public: // Config
	/** Maximum number of blocks to send in a single response, cannot be higher than capacity of a single `asc_pull_ack` message */
	constexpr static std::size_t max_blocks = nano::messages::asc_pull_ack::blocks_payload::max_blocks;
	constexpr static std::size_t max_frontiers = nano::messages::asc_pull_ack::frontiers_payload::max_frontiers;
};

nano::stat::detail to_stat_detail (nano::messages::asc_pull_type);
}
