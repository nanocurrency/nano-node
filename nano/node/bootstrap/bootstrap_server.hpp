#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/fwd.hpp>

#include <atomic>
#include <deque>
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
	bool enable{ true }; // When disabled, no threads are started and every request is dropped
	size_t channel_limit{ 16 }; // Maximum queued requests per channel
	size_t threads{ 1 }; // Threads processing queued requests
	size_t batch_size{ 64 }; // Maximum requests processed under one read transaction
	size_t limiter{ 500 }; // Requests processed per second, 0 for unlimited
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

	/** Process `asc_pull_req` message coming from network. Reply will be sent back over passed in `channel` */
	bool request (nano::messages::asc_pull_req const & message, std::shared_ptr<nano::transport::channel> const & channel);

	nano::container_info container_info () const;

public: // Events
	/** Notified with every response just before it is sent */
	nano::observer_set<nano::messages::asc_pull_ack const &, std::shared_ptr<nano::transport::channel> const &> on_response;

private:
	using request_type = std::pair<nano::messages::asc_pull_req, std::shared_ptr<nano::transport::channel>>; // <request, response channel>
	using queue_type = nano::fair_queue<request_type, nano::no_value, std::shared_ptr<nano::transport::channel>>;

	/** Main thread loop */
	void run ();
	/** Pops the requests ready to be served, up to `batch_size` */
	std::deque<queue_type::value_type> next_batch ();
	/** Serves the batch under one read transaction */
	void run_batch (std::deque<queue_type::value_type> const &);
	/** Dispatches the request to the `process` overload of its payload type */
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req const & message);
	/** Updates response stats, notifies `on_response` and sends the response over `channel` */
	void respond (nano::messages::asc_pull_ack &, std::shared_ptr<nano::transport::channel> const &);

	/** Unreachable after `verify`, answers with an `invalid` type response */
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::empty_payload const & request);

	// Blocks request
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::blocks_payload const & request) const;
	/** Response with up to `count` blocks of the chain starting at `start_block` */
	nano::messages::asc_pull_ack prepare_response (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::block_hash start_block, std::size_t count) const;
	/** Response with no blocks, sent when the requested block or account is unknown */
	nano::messages::asc_pull_ack prepare_empty_blocks_response (nano::messages::asc_pull_req::id_t id) const;
	/** Up to `count` blocks from `start_block` following successors, stopping at the chain head */
	std::deque<std::shared_ptr<nano::block>> prepare_blocks (secure::transaction const &, nano::block_hash start_block, std::size_t count) const;

	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::account_info_payload const & request) const;
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::frontiers_payload const & request) const;
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::blocks_random_payload const & request) const;
	nano::messages::asc_pull_ack process (secure::transaction const &, nano::messages::asc_pull_req::id_t id, nano::messages::asc_pull_req::topo_index_payload const & request) const;

	/** Checks if the request should be dropped early on */
	bool verify (nano::messages::asc_pull_req const & message) const;
	/** False for `invalid` and unknown request types */
	bool verify_request_type (nano::messages::asc_pull_type) const;

private: // Dependencies
	bootstrap_server_config const & config;
	nano::store::ledger_store & store;
	nano::ledger & ledger;
	nano::network_constants const & network_constants;
	nano::stats & stats;

private:
	// Pending requests, at most `channel_limit` per channel, served round robin across channels
	queue_type queue;

	// Paces batch processing to `limiter` requests per second
	nano::rate_limiter limiter;

	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::vector<std::thread> threads;

public: // Config
	/** Maximum number of blocks to send in a single response, cannot be higher than capacity of a single `asc_pull_ack` message */
	constexpr static std::size_t max_blocks = nano::messages::asc_pull_ack::blocks_payload::max_blocks;
	/** Maximum frontiers per response, the capacity of a single `asc_pull_ack` message */
	constexpr static std::size_t max_frontiers = nano::messages::asc_pull_ack::frontiers_payload::max_frontiers;
	/** Maximum topology entries per response, the capacity of a single `asc_pull_ack` message */
	constexpr static std::size_t max_topo_entries = nano::messages::asc_pull_ack::topo_index_payload::max_entries;
	/** Maximum hashes in a blocks random request, the capacity of a single `asc_pull_req` message */
	constexpr static std::size_t max_random_hashes = nano::messages::asc_pull_req::blocks_random_payload::max_hashes;
	/** Maximum blocks in a blocks random response; requested blocks past it are left out */
	constexpr static std::size_t max_random_blocks = 100;
};

nano::stat::detail to_stat_detail (nano::messages::asc_pull_type);
}
