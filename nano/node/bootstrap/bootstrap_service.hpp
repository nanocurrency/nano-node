#pragma once

#include <celerix/lib/interval.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/observer_set.hpp>
#include <celerix/lib/random.hpp>
#include <celerix/lib/rate_limiting.hpp>
#include <celerix/lib/thread_pool.hpp>
#include <celerix/node/bootstrap/account_sets.hpp>
#include <celerix/node/bootstrap/bootstrap_config.hpp>
#include <celerix/node/bootstrap/common.hpp>
#include <celerix/node/bootstrap/database_scan.hpp>
#include <celerix/node/bootstrap/frontier_scan.hpp>
#include <celerix/node/bootstrap/peer_scoring.hpp>
#include <celerix/node/bootstrap/throttle.hpp>
#include <celerix/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <thread>

namespace mi = boost::multi_index;

namespace celerix
{
class bootstrap_service
{
public:
	bootstrap_service (celerix::node_config const &, celerix::block_processor &, celerix::ledger &, celerix::network &, celerix::stats &, celerix::logger &);
	~bootstrap_service ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_ack` message coming from network
	 */
	void process (celerix::asc_pull_ack const & message, std::shared_ptr<celerix::transport::channel> const &);

	std::size_t blocked_size () const;
	std::size_t priority_size () const;
	std::size_t score_size () const;

	bool prioritized (celerix::account const &) const;
	bool blocked (celerix::account const &) const;

	celerix::container_info container_info () const;

	celerix::bootstrap::account_sets::info_t info () const;

private: // Dependencies
	bootstrap_config const & config;
	celerix::network_constants const & network_constants;
	celerix::block_processor & block_processor;
	celerix::ledger & ledger;
	celerix::network & network;
	celerix::stats & stats;
	celerix::logger & logger;

public: // Tag
	enum class query_type
	{
		invalid = 0, // Default initialization
		blocks_by_hash,
		blocks_by_account,
		account_info_by_hash,
		frontiers,
	};

	enum class query_source
	{
		invalid,
		priority,
		database,
		dependencies,
		frontiers,
	};

	struct async_tag
	{
		using id_t = celerix::bootstrap::id_t;

		query_type type{ query_type::invalid };
		query_source source{ query_source::invalid };
		celerix::hash_or_account start{ 0 };
		celerix::account account{ 0 };
		celerix::block_hash hash{ 0 };
		size_t count{ 0 };
		std::chrono::steady_clock::time_point cutoff{};
		std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };
		id_t id{ celerix::bootstrap::generate_id () };
	};

private:
	/* Inspects a block that has been processed by the block processor */
	void inspect (secure::transaction const &, celerix::block_status const & result, celerix::block const & block, celerix::block_source);

	void run_priorities ();
	void run_one_priority ();
	void run_database ();
	void run_one_database (bool should_throttle);
	void run_dependencies ();
	void run_one_dependency ();
	void run_frontiers ();
	void run_one_frontier ();
	void run_timeouts ();
	void cleanup_and_sync ();

	/* Waits for a condition to be satisfied with incremental backoff */
	void wait (std::function<bool ()> const & predicate) const;

	/* Ensure there is enough space in block_processor for queuing new blocks */
	void wait_block_processor () const;
	/* Waits for a channel that is not full */
	std::shared_ptr<celerix::transport::channel> wait_channel ();
	/* Waits until a suitable account outside of cooldown period is available */
	using priority_result = celerix::bootstrap::account_sets::priority_result;
	priority_result next_priority ();
	priority_result wait_priority ();
	/* Gets the next account from the database */
	celerix::account next_database (bool should_throttle);
	celerix::account wait_database (bool should_throttle);
	/* Waits for next available blocking block */
	celerix::block_hash next_blocking ();
	celerix::block_hash wait_blocking ();
	/* Waits for next available frontier scan range */
	celerix::account wait_frontier ();

	bool request (celerix::account, size_t count, std::shared_ptr<celerix::transport::channel> const &, query_source);
	bool request_info (celerix::block_hash, std::shared_ptr<celerix::transport::channel> const &, query_source);
	bool request_frontiers (celerix::account, std::shared_ptr<celerix::transport::channel> const &, query_source);
	bool send (std::shared_ptr<celerix::transport::channel> const &, async_tag tag);

	bool process (celerix::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
	bool process (celerix::asc_pull_ack::account_info_payload const & response, async_tag const & tag);
	bool process (celerix::asc_pull_ack::frontiers_payload const & response, async_tag const & tag);
	bool process (celerix::empty_payload const & response, async_tag const & tag);

	void process_frontiers (std::deque<std::pair<celerix::account, celerix::block_hash>> const & frontiers);

	enum class verify_result
	{
		ok,
		nothing_new,
		invalid,
	};

	/**
	 * Verifies whether the received response is valid. Returns:
	 * - invalid: when received blocks do not correspond to requested hash/account or they do not make a valid chain
	 * - nothing_new: when received response indicates that the account chain does not have more blocks
	 * - ok: otherwise, if all checks pass
	 */
	verify_result verify (celerix::asc_pull_ack::blocks_payload const & response, async_tag const & tag) const;
	verify_result verify (celerix::asc_pull_ack::frontiers_payload const & response, async_tag const & tag) const;

	size_t count_tags (celerix::account const & account, query_source source) const;
	size_t count_tags (celerix::block_hash const & hash, query_source source) const;

	// Calculates a lookback size based on the size of the ledger where larger ledgers have a larger sample count
	std::size_t compute_throttle_size () const;

private:
	celerix::bootstrap::account_sets accounts;
	celerix::bootstrap::database_scan database_scan;
	celerix::bootstrap::throttle throttle;
	celerix::bootstrap::peer_scoring scoring;
	celerix::bootstrap::frontier_scan frontiers;

	// clang-format off
	class tag_sequenced {};
	class tag_id {};
	class tag_account {};
	class tag_hash {};

	using ordered_tags = boost::multi_index_container<async_tag,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<async_tag, celerix::bootstrap::id_t, &async_tag::id>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<async_tag, celerix::account , &async_tag::account>>,
		mi::hashed_non_unique<mi::tag<tag_hash>,
			mi::member<async_tag, celerix::block_hash, &async_tag::hash>>
	>>;
	// clang-format on
	ordered_tags tags;

	// Rate limiter for all types of requests
	celerix::rate_limiter limiter;
	// Requests for accounts from database have much lower hitrate and could introduce strain on the network
	// A separate (lower) limiter ensures that we always reserve resources for querying accounts from priority queue
	celerix::rate_limiter database_limiter;
	// Rate limiter for frontier requests
	celerix::rate_limiter frontiers_limiter;

	celerix::interval sync_dependencies_interval;

	bool stopped{ false };
	mutable celerix::mutex mutex;
	mutable celerix::condition_variable condition;
	std::thread priorities_thread;
	std::thread database_thread;
	std::thread dependencies_thread;
	std::thread frontiers_thread;
	std::thread cleanup_thread;

	celerix::thread_pool workers;
	celerix::random_generator_mt rng;
};

celerix::stat::detail to_stat_detail (bootstrap_service::query_type);
}
