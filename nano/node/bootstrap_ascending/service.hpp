#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap_ascending/account_sets.hpp>
#include <nano/node/bootstrap_ascending/common.hpp>
#include <nano/node/bootstrap_ascending/iterators.hpp>
#include <nano/node/bootstrap_ascending/peer_scoring.hpp>
#include <nano/node/bootstrap_ascending/throttle.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <thread>

namespace mi = boost::multi_index;

namespace nano::secure
{
class transaction;
}

namespace nano
{
class block_processor;
class ledger;
class network;
class node_config;

namespace transport
{
	class channel;
}

namespace bootstrap_ascending
{
	class service
	{
	public:
		service (nano::node_config &, nano::block_processor &, nano::ledger &, nano::network &, nano::stats &);
		~service ();

		void start ();
		void stop ();

		/**
		 * Process `asc_pull_ack` message coming from network
		 */
		void process (nano::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const &);

	public: // Container info
		std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);
		std::size_t blocked_size () const;
		std::size_t priority_size () const;
		std::size_t score_size () const;
		nano::bootstrap_ascending::account_sets::info_t info () const;

	private: // Dependencies
		nano::node_config & config;
		nano::network_constants & network_consts;
		nano::block_processor & block_processor;
		nano::ledger & ledger;
		nano::network & network;
		nano::stats & stats;

	public: // Tag
		enum class query_type
		{
			invalid = 0, // Default initialization
			blocks_by_hash,
			blocks_by_account,
			account_info_by_hash,
		};

		enum class query_source
		{
			invalid,
			priority,
			database,
			blocking,
		};

		struct async_tag
		{
			query_type type{ query_type::invalid };
			query_source source{ query_source::invalid };
			nano::hash_or_account start{ 0 };
			nano::account account{ 0 };
			nano::block_hash hash{ 0 };

			id_t id{ generate_id () };
			std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };
		};

	public: // Events
		nano::observer_set<async_tag const &, std::shared_ptr<nano::transport::channel> const &> on_request;
		nano::observer_set<async_tag const &> on_reply;
		nano::observer_set<async_tag const &> on_timeout;

	private:
		/* Inspects a block that has been processed by the block processor */
		void inspect (secure::transaction const &, nano::block_status const & result, nano::block const & block);

		void run_priorities ();
		void run_one_priority ();
		void run_database ();
		void run_one_database (bool should_throttle);
		void run_dependencies ();
		void run_one_blocking ();
		void run_timeouts ();
		void cleanup_and_sync ();

		void wait_backoff (std::function<bool ()> const & predicate, std::vector<std::chrono::milliseconds> const & intervals);

		/* Avoid too many in-flight requests */
		void wait_tags ();
		/* Ensure there is enough space in blockprocessor for queuing new blocks */
		void wait_blockprocessor ();
		/* Waits for a channel that is not full */
		std::shared_ptr<nano::transport::channel> wait_channel ();
		/* Waits until a suitable account outside of cool down period is available */
		nano::account next_priority ();
		nano::account wait_priority ();
		/* Gets the next account from the database */
		nano::account next_database (bool should_throttle);
		nano::account wait_database (bool should_throttle);
		/* Waits for next available blocking block */
		nano::block_hash next_blocking ();
		nano::block_hash wait_blocking ();

		bool request (nano::account, std::shared_ptr<nano::transport::channel> const &, query_source);
		bool request_info (nano::block_hash, std::shared_ptr<nano::transport::channel> const &, query_source);
		void send (std::shared_ptr<nano::transport::channel> const &, async_tag tag);

		void process (nano::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
		void process (nano::asc_pull_ack::account_info_payload const & response, async_tag const & tag);
		void process (nano::asc_pull_ack::frontiers_payload const & response, async_tag const & tag);
		void process (nano::empty_payload const & response, async_tag const & tag);

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
		verify_result verify (nano::asc_pull_ack::blocks_payload const & response, async_tag const & tag) const;

		size_t count_tags (nano::account const & account, query_source source) const;
		size_t count_tags (nano::block_hash const & hash, query_source source) const;

		// Calculates a lookback size based on the size of the ledger where larger ledgers have a larger sample count
		std::size_t compute_throttle_size () const;

	private:
		nano::bootstrap_ascending::account_sets accounts;
		nano::bootstrap_ascending::buffered_iterator iterator;
		nano::bootstrap_ascending::throttle throttle;
		nano::bootstrap_ascending::peer_scoring scoring;

		// clang-format off
		class tag_sequenced {};
		class tag_id {};
		class tag_account {};
		class tag_hash {};

		using ordered_tags = boost::multi_index_container<async_tag,
		mi::indexed_by<
			mi::sequenced<mi::tag<tag_sequenced>>,
			mi::hashed_unique<mi::tag<tag_id>,
				mi::member<async_tag, nano::bootstrap_ascending::id_t, &async_tag::id>>,
			mi::hashed_non_unique<mi::tag<tag_account>,
				mi::member<async_tag, nano::account , &async_tag::account>>,
			mi::hashed_non_unique<mi::tag<tag_hash>,
				mi::member<async_tag, nano::block_hash, &async_tag::hash>>
		>>;
		// clang-format on
		ordered_tags tags;

		// Requests for accounts from database have much lower hitrate and could introduce strain on the network
		// A separate (lower) limiter ensures that we always reserve resources for querying accounts from priority queue
		nano::bandwidth_limiter database_limiter;

		nano::interval sync_dependencies_interval;

		bool stopped{ false };
		mutable nano::mutex mutex;
		mutable nano::condition_variable condition;
		std::thread priorities_thread;
		std::thread database_thread;
		std::thread dependencies_thread;
		std::thread timeout_thread;

		static size_t constexpr max_tags{ 1024 };
	};

	nano::stat::detail to_stat_detail (service::query_type);
}
}
