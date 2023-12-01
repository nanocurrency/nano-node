#pragma once

#include <nano/lib/observer_set.hpp>
#include <nano/lib/processing_queue.hpp>
#include <nano/node/messages.hpp>

#include <memory>
#include <utility>

namespace nano::store
{
class transaction;
}

namespace nano
{
class ledger;
namespace transport
{
	class channel;
}

/**
 * Processes bootstrap requests (`asc_pull_req` messages) and replies with bootstrap responses (`asc_pull_ack`)
 *
 * In order to ensure maximum throughput, there are two internal processing queues:
 * - One for doing ledger lookups and preparing responses (`request_queue`)
 * - One for sending back those responses over the network (`response_queue`)
 */
class bootstrap_server final
{
public:
	// `asc_pull_req` message is small, store by value
	using request_t = std::pair<nano::asc_pull_req, std::shared_ptr<nano::transport::channel>>; // <request, response channel>

public:
	bootstrap_server (nano::store::component &, nano::ledger &, nano::network_constants const &, nano::stats &);
	~bootstrap_server ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_req` message coming from network.
	 * Reply will be sent back over passed in `channel`
	 */
	bool request (nano::asc_pull_req const & message, std::shared_ptr<nano::transport::channel> channel);

public: // Events
	nano::observer_set<nano::asc_pull_ack &, std::shared_ptr<nano::transport::channel> &> on_response;

private:
	void process_batch (std::deque<request_t> & batch);
	nano::asc_pull_ack process (store::transaction const &, nano::asc_pull_req const & message);
	void respond (nano::asc_pull_ack &, std::shared_ptr<nano::transport::channel> &);

	nano::asc_pull_ack process (store::transaction const &, nano::asc_pull_req::id_t id, nano::empty_payload const & request);

	/*
	 * Blocks request
	 */
	nano::asc_pull_ack process (store::transaction const &, nano::asc_pull_req::id_t id, nano::asc_pull_req::blocks_payload const & request);
	nano::asc_pull_ack prepare_response (store::transaction const &, nano::asc_pull_req::id_t id, nano::block_hash start_block, std::size_t count);
	nano::asc_pull_ack prepare_empty_blocks_response (nano::asc_pull_req::id_t id);
	std::vector<std::shared_ptr<nano::block>> prepare_blocks (store::transaction const &, nano::block_hash start_block, std::size_t count) const;

	/*
	 * Account info request
	 */
	nano::asc_pull_ack process (store::transaction const &, nano::asc_pull_req::id_t id, nano::asc_pull_req::account_info_payload const & request);

	/*
	 * Frontiers request
	 */
	nano::asc_pull_ack process (store::transaction const &, nano::asc_pull_req::id_t id, nano::asc_pull_req::frontiers_payload const & request);

	/*
	 * Checks if the request should be dropped early on
	 */
	bool verify (nano::asc_pull_req const & message) const;
	bool verify_request_type (nano::asc_pull_type) const;

private: // Dependencies
	nano::store::component & store;
	nano::ledger & ledger;
	nano::network_constants const & network_constants;
	nano::stats & stats;

private:
	nano::processing_queue<request_t> request_queue;

public: // Config
	/** Maximum number of blocks to send in a single response, cannot be higher than capacity of a single `asc_pull_ack` message */
	constexpr static std::size_t max_blocks = nano::asc_pull_ack::blocks_payload::max_blocks;
	constexpr static std::size_t max_frontiers = nano::asc_pull_ack::frontiers_payload::max_frontiers;
};
}
