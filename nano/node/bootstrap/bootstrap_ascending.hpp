#pragma once

#include <nano/lib/observer_set.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <random>
#include <thread>

namespace mi = boost::multi_index;

namespace nano
{
class block_processor;
class ledger;
class network;

namespace transport
{
	class channel;
}

class bootstrap_ascending
{
	using id_t = uint64_t;

public:
	bootstrap_ascending (nano::node &, nano::store &, nano::block_processor &, nano::ledger &, nano::network &, nano::stats &);
	~bootstrap_ascending ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_ack` message coming from network
	 */
	void process (nano::asc_pull_ack const & message);

public: // Container info
	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);
	size_t blocked_size () const;
	size_t priority_size () const;

private: // Dependencies
	nano::node & node;
	nano::store & store;
	nano::block_processor & block_processor;
	nano::ledger & ledger;
	nano::network & network;
	nano::stats & stats;

public: // async_tag
	struct async_tag
	{
		enum class query_type
		{
			invalid = 0, // Default initialization
			blocks_by_hash,
			blocks_by_account,
			// TODO: account_info,
		};

		query_type type{ query_type::invalid };
		id_t id{ 0 };
		nano::hash_or_account start{ 0 };
		nano::millis_t time{ 0 };
		nano::account account{ 0 };
	};

public: // Events
	nano::observer_set<async_tag const &, std::shared_ptr<nano::transport::channel> &> on_request;
	nano::observer_set<async_tag const &> on_reply;
	nano::observer_set<async_tag const &> on_timeout;

private:
	/* Inspects a block that has been processed by the block processor */
	void inspect (nano::transaction const &, nano::process_return const & result, nano::block const & block);

	void run ();
	bool run_one ();
	void run_timeouts ();

	/* Limits the number of requests per second we make */
	void wait_available_request ();
	/* Throttles requesting new blocks, not to overwhelm blockprocessor */
	void wait_blockprocessor ();
	/* Waits for channel with free capacity for bootstrap messages */
	std::shared_ptr<nano::transport::channel> wait_available_channel ();
	std::shared_ptr<nano::transport::channel> available_channel ();
	/* Waits until a suitable account outside of cool down period is available */
	nano::account available_account ();
	nano::account wait_available_account ();

	bool request (nano::account &, std::shared_ptr<nano::transport::channel> &);
	void send (std::shared_ptr<nano::transport::channel>, async_tag tag);
	void track (async_tag const & tag);

	void process (nano::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
	void process (nano::asc_pull_ack::account_info_payload const & response, async_tag const & tag);
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

	static id_t generate_id ();

public: // account_sets
	/** This class tracks accounts various account sets which are shared among the multiple bootstrap threads */
	class account_sets
	{
	public:
		explicit account_sets (nano::stats &);

		/**
		 * If an account is not blocked, increase its priority.
		 * If the account does not exist in priority set and is not blocked, inserts a new entry.
		 * Current implementation increases priority by 1.0f each increment
		 */
		void priority_up (nano::account const & account);
		/**
		 * Decreases account priority
		 * Current implementation divides priority by 2.0f and saturates down to 1.0f.
		 */
		void priority_down (nano::account const & account);
		void block (nano::account const & account, nano::block_hash const & dependency);
		void unblock (nano::account const & account, std::optional<nano::block_hash> const & hash = std::nullopt);
		void timestamp (nano::account const & account, bool reset = false);

		nano::account next ();

	public:
		bool blocked (nano::account const & account) const;
		std::size_t priority_size () const;
		std::size_t blocked_size () const;
		/**
		 * Accounts in the ledger but not in priority list are assumed priority 1.0f
		 * Blocked accounts are assumed priority 0.0f
		 */
		float priority (nano::account const & account) const;

	public: // Container info
		std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);

	private:
		void trim_overflow ();
		bool check_timestamp (nano::account const & account) const;

	private: // Dependencies
		nano::stats & stats;

	private:
		struct priority_entry
		{
			nano::account account{ 0 };
			float priority{ 0 };
			nano::millis_t timestamp{ 0 };
			id_t id{ 0 }; // Uniformly distributed, used for random querying

			priority_entry (nano::account account, float priority);
		};

		struct blocking_entry
		{
			nano::account account{ 0 };
			nano::block_hash dependency{ 0 };
			priority_entry original_entry{ 0, 0 };

			float priority () const
			{
				return original_entry.priority;
			}
		};

		// clang-format off
		class tag_account {};
		class tag_priority {};
		class tag_sequenced {};
		class tag_id {};

		// Tracks the ongoing account priorities
		// This only stores account priorities > 1.0f.
		using ordered_priorities = boost::multi_index_container<priority_entry,
		mi::indexed_by<
			mi::sequenced<mi::tag<tag_sequenced>>,
			mi::ordered_unique<mi::tag<tag_account>,
				mi::member<priority_entry, nano::account, &priority_entry::account>>,
			mi::ordered_non_unique<mi::tag<tag_priority>,
				mi::member<priority_entry, float, &priority_entry::priority>>,
			mi::ordered_unique<mi::tag<tag_id>,
				mi::member<priority_entry, bootstrap_ascending::id_t, &priority_entry::id>>
		>>;

		// A blocked account is an account that has failed to insert a new block because the source block is not currently present in the ledger
		// An account is unblocked once it has a block successfully inserted
		using ordered_blocking = boost::multi_index_container<blocking_entry,
		mi::indexed_by<
			mi::sequenced<mi::tag<tag_sequenced>>,
			mi::ordered_unique<mi::tag<tag_account>,
				mi::member<blocking_entry, nano::account, &blocking_entry::account>>,
			mi::ordered_non_unique<mi::tag<tag_priority>,
				mi::const_mem_fun<blocking_entry, float, &blocking_entry::priority>>
		>>;
		// clang-format on

		ordered_priorities priorities;
		ordered_blocking blocking;

		std::default_random_engine rng;

	private: // TODO: Move into config
		static std::size_t constexpr consideration_count = 4;
		static std::size_t constexpr priorities_max = 256 * 1024;
		static std::size_t constexpr blocking_max = 256 * 1024;
		static nano::millis_t constexpr cooldown = 3 * 1000;

	public: // Consts
		static float constexpr priority_initial = 8.0f;
		static float constexpr priority_increase = 2.0f;
		static float constexpr priority_decrease = 0.5f;
		static float constexpr priority_max = 32.0f;
		static float constexpr priority_cutoff = 1.0f;

	public:
		using info_t = std::tuple<decltype (blocking), decltype (priorities)>; // <blocking, priorities>
		info_t info () const;
	};

	account_sets::info_t info () const;

private: // Database iterators
	class database_iterator
	{
	public:
		enum class table_type
		{
			account,
			pending
		};

		explicit database_iterator (nano::store & store, table_type);
		nano::account operator* () const;
		void next (nano::transaction & tx);

	private:
		nano::store & store;
		nano::account current{ 0 };
		const table_type table;
	};

	class buffered_iterator
	{
	public:
		explicit buffered_iterator (nano::store & store);
		nano::account operator* () const;
		nano::account next ();

	private:
		void fill ();

	private:
		nano::store & store;
		std::deque<nano::account> buffer;

		database_iterator accounts_iterator;
		database_iterator pending_iterator;

		static std::size_t constexpr size = 1024;
	};

private:
	account_sets accounts;
	buffered_iterator iterator;

	// clang-format off
	class tag_sequenced {};
	class tag_id {};
	class tag_account {};

	using ordered_tags = boost::multi_index_container<async_tag,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<async_tag, id_t, &async_tag::id>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<async_tag, nano::account , &async_tag::account>>
	>>;
	// clang-format on
	ordered_tags tags;

	nano::bandwidth_limiter limiter;
	// Requests for accounts from database have much lower hitrate and could introduce strain on the network
	// A separate (lower) limiter ensures that we always reserve resources for querying accounts from priority queue
	nano::bandwidth_limiter database_limiter;

	bool stopped{ false };
	mutable nano::mutex mutex;
	mutable nano::condition_variable condition;
	std::thread thread;
	std::thread timeout_thread;
};
}
