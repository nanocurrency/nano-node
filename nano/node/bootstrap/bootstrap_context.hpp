#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/random.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/bootstrap/account_sets_index.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/node/bootstrap/database_scan_index.hpp>
#include <nano/node/bootstrap/frontier_scan_index.hpp>
#include <nano/node/bootstrap/peer_pool.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/bootstrap/throttle.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
class priority_strategy;
class database_strategy;
class dependency_strategy;
class frontier_strategy;
class topo_strategy;

struct async_tag
{
	strategy source{ strategy::invalid };
	query_descriptor query;

	// Index keys, derived from the query descriptor when the tag is created
	nano::account account{ 0 };
	nano::block_hash hash{ 0 };

	// Identity of the peer this request was sent to, for per-peer fetch tracking
	nano::account node_id{ 0 };

	std::chrono::steady_clock::time_point cutoff{};
	std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };
	id_t id{ generate_id () };

	query_type type () const;
};

class bootstrap_context
{
public:
	bootstrap_context (nano::node_config const &, nano::node &, nano::ledger &, nano::ledger_notifications &, nano::block_processor &, nano::network &, nano::stats &, nano::logger &);
	~bootstrap_context ();

	void start ();
	void stop ();

	void reset ();

	// Process bootstrap messages coming from the network
	void process (nano::messages::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const &);

	// Waits for a condition to be satisfied with incremental backoff
	void wait (std::function<bool ()> const & predicate) const;

	template <typename ResultProvider>
	auto wait_result (ResultProvider && provider) const -> std::invoke_result_t<ResultProvider &>
	{
		using result_type = std::invoke_result_t<ResultProvider &>;

		result_type result{};
		wait ([&] () {
			result = provider ();
			return static_cast<bool> (result);
		});
		return result;
	}

	// Wait until there is enough space in block_processor for new blocks
	void wait_block_processor (nano::bootstrap::strategy) const;

	// Waits for a channel that is not full. Applies the per-strategy rate limiter and only returns a peer that meets the requirements.
	std::shared_ptr<nano::transport::channel> wait_channel (nano::bootstrap::strategy strategy, peer_requirements const & required = {});

	// One reserved peer of a fanout round
	struct channel_lease
	{
		std::shared_ptr<nano::transport::channel> channel;
		nano::account node_id{ 0 };
	};

	// Outcome of a fanout acquire: the leases obtained, and whether the distinct-peer pool ran out
	struct fanout_result
	{
		std::vector<channel_lease> leases;
		bool exhausted{ false };
	};

	// Reserves up to `max` distinct peers meeting the requirements (each excluded from the next pick, on top of `exclude`).
	// Blocks for the first lease only on a fresh round (empty `exclude`); the rest are best-effort.
	// `exhausted` is set when the matching pool runs dry before reaching `max`. Applies the per-strategy rate limiter.
	fanout_result wait_channels (nano::bootstrap::strategy strategy, peer_requirements const & required, std::span<nano::account const> exclude, unsigned max);

	enum class conclusion
	{
		timeout,
		failure
	};

	bool send (std::shared_ptr<nano::transport::channel> const &, query_descriptor query, strategy source);
	bool send (std::shared_ptr<nano::transport::channel> const &, query_descriptor query, strategy source, id_t id);
	bool conclude_tag (id_t, conclusion);

	size_t count_tags (nano::account const & account, strategy source) const;
	size_t count_tags (nano::block_hash const & hash, strategy source) const;

	// Inspects a block that has been processed by the block processor
	void inspect (secure::transaction const &, nano::block_status const & result, nano::block_context const & context);

	// Placeholder channel used as a fair-queue partition key so the block processor equalizes ingest across sources
	std::shared_ptr<nano::transport::channel> const & submission_channel (nano::bootstrap::strategy) const;

	// Handles a block that has been rolled back from the ledger
	void rollback (nano::block const & block);

	nano::container_info container_info () const;

private:
	bool process (nano::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
	bool process (nano::messages::asc_pull_ack::account_info_payload const & response, async_tag const & tag);
	bool process (nano::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag);
	bool process (nano::messages::asc_pull_ack::topo_index_payload const & response, async_tag const & tag);
	bool process (nano::messages::empty_payload const & response, async_tag const & tag);

	// Inserts the tag and transmits the message over the channel
	bool transmit (std::shared_ptr<nano::transport::channel> const &, nano::messages::asc_pull_req && message, async_tag tag);
	void conclude (async_tag const &, conclusion);

	// Filters out blocks already present in the ledger and submits the rest to the block processor
	void submit_blocks (std::deque<std::shared_ptr<nano::block>> blocks, async_tag const & tag);

	void maintenance (nano::unique_lock<nano::mutex> & lock);
	void run_maintenance ();

	// Calculates a lookback size based on the size of the ledger
	std::size_t compute_throttle_size () const;

public: // Dependencies
	nano::bootstrap_config const & config;
	nano::network_constants const & network_constants;
	nano::ledger & ledger;
	nano::ledger_notifications & ledger_notifications;
	nano::block_processor & block_processor;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

public: // Strategies
	std::unique_ptr<nano::bootstrap::priority_strategy> priority_strat_impl;
	nano::bootstrap::priority_strategy & priority_strat;
	std::unique_ptr<nano::bootstrap::database_strategy> database_strat_impl;
	nano::bootstrap::database_strategy & database_strat;
	std::unique_ptr<nano::bootstrap::dependency_strategy> dependency_strat_impl;
	nano::bootstrap::dependency_strategy & dependency_strat;
	std::unique_ptr<nano::bootstrap::frontier_strategy> frontier_strat_impl;
	nano::bootstrap::frontier_strategy & frontier_strat;
	std::unique_ptr<nano::bootstrap::topo_strategy> topo_strat_impl;
	nano::bootstrap::topo_strategy & topo_strat;

public: // Shared state
	nano::bootstrap::account_sets_index accounts;
	nano::bootstrap::database_scan_index database_scan;
	nano::bootstrap::frontier_scan_index frontiers;
	nano::bootstrap::throttle throttle;
	nano::bootstrap::peer_pool peers;

	// clang-format off
	class tag_sequenced {};
	class tag_id {};
	class tag_account {};
	class tag_hash {};

	using ordered_tags = boost::multi_index_container<async_tag,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<async_tag, id_t, &async_tag::id>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<async_tag, nano::account, &async_tag::account>>,
		mi::hashed_non_unique<mi::tag<tag_hash>,
			mi::member<async_tag, nano::block_hash, &async_tag::hash>>
	>>;
	// clang-format on
	ordered_tags tags;

	nano::random_generator_mt rng;

	// Per-strategy rate limiters
	nano::rate_limiter priority_limiter;
	nano::rate_limiter database_limiter;
	nano::rate_limiter dependency_limiter;
	nano::rate_limiter frontier_limiter;
	nano::rate_limiter topo_limiter;

	// Per-source placeholder channels. Tagging block_processor submissions with a distinct
	// channel per source gives each its own fair-queue bucket, so the processor round-robins
	// ingest evenly across sources instead of letting one starve the other.
	std::shared_ptr<nano::transport::channel> generic_channel; // Null, used as generic fair queue origin
	std::shared_ptr<nano::transport::channel> priority_channel;
	std::shared_ptr<nano::transport::channel> database_channel;
	std::shared_ptr<nano::transport::channel> topology_channel;

	bool stopped{ false };
	mutable nano::mutex mutex;
	mutable nano::condition_variable condition;

	std::thread maintenance_thread;

	// Must stay single-threaded: block submissions rely on FIFO execution to keep same-account chains in order
	nano::thread_pool workers;
};
}
