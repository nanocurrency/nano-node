#pragma once

#include <nano/node/bootstrap/bootstrap_attempt.hpp>

#include <random>
#include <thread>

namespace nano
{
namespace transport
{
	class channel;
}
namespace bootstrap
{
	class bootstrap_ascending
	{
	public:
		explicit bootstrap_ascending (nano::node & node_a);
		virtual ~bootstrap_ascending ();

		void init ();
		void start ();
		void stop ();
		void run ();

		// Make an account known to ascending bootstrap and set its priority
		void priority_up (nano::account const & account_a);
		void priority_down (nano::account const & account_a);

		void get_information (boost::property_tree::ptree &);
		std::unique_ptr<nano::container_info_component> collect_container_info (const std::string & name);

	private: // Dependencies
		nano::stat & stats;

	private:
		nano::node & node;
		bool stopped{ false };
		nano::condition_variable condition;
		mutable nano::mutex mutex;
		std::thread main_thread;
		void debug_log (const std::string &) const;
		// void dump_miss_histogram ();

	public:
		class async_tag;

		class connection_pool
		{
		public:
			using socket_channel = std::pair<std::shared_ptr<nano::socket>, std::shared_ptr<nano::transport::channel>>;

		public:
			connection_pool (nano::bootstrap::bootstrap_ascending & bootstrap);
			/** Given a tag context, find or create a connection to a peer and then call the op callback */
			bool operator() (std::shared_ptr<async_tag> tag, std::function<void ()> op);
			void add (socket_channel const & connection);
			std::unique_ptr<nano::container_info_component> collect_container_info (const std::string & name);

		private:
			std::deque<socket_channel> connections;
			const nano::bootstrap::bootstrap_ascending & bootstrap;
		};

		using socket_channel = connection_pool::socket_channel;

		/** This class tracks accounts various account sets which are shared among the multiple bootstrap threads */
		class account_sets
		{
		public:
			explicit account_sets (nano::stat &, nano::store & store);

			/**
			 * If an account is not blocked, increase its priority.
			 * Priority is increased whether it is in the normal priority set, or it is currently blocked and in the blocked set.
			 * Current implementation increases priority by 1.0f each increment
			 */
			void priority_up (nano::account const & account);
			/**
			 * Decreases account priority
			 * Current implementation divides priority by 2.0f and saturates down to 1.0f.
			 */
			void priority_down (nano::account const & account);
			/**
			 * Marks an account as blocked and it receives 0.0f priority
			 */
			void block (nano::account const & account, nano::block_hash const & dependency);
			/**
			 * Unblock an account but only if it was blocked waiting for a specific hash.
			 * This is useful when a source block dependency is satisfied
			 */
			void unblock (nano::account const & account, nano::block_hash const & hash);
			/**
			 * Unblocks an account regardless of the block that has been added
			 * This is useful for if an account was blocked and a block has now been inserted
			 * Regardless of what we were waiting for before, the account is no longer blocked.
			*/
			void force_unblock (nano::account const & account);
			void dump () const;
			std::string to_string () const;
			std::unique_ptr<nano::container_info_component> collect_container_info (const std::string & name);

		public:
			bool blocked (nano::account const & account) const;
			float priority (nano::account const & account) const;
			/**
			 * Selects a random account from either:
			 * 1) The priority set in memory
			 * 2) The accounts in the ledger
			 * 3) Pending entries in the ledger
			 * Creates consideration set of "consideration_count" items and returns on randomly weighted by priority
			 * Half are considered from the "priorities" container, half are considered from the ledger.
			 */
			nano::account random ();

		private: // Dependencies
			nano::stat & stats;
			nano::store & store;

		private:
			static size_t constexpr consideration_count = 2;

			// A blocked account is an account that has failed to insert a block because the source block is gapped.
			// An account is unblocked once it has a block successfully inserted.
			// Maps "blocked account" -> ["blocked hash", "Priority count"]
			std::map<nano::account, std::pair<nano::block_hash, float>> blocking;
			class priority_t
			{
			public:
				nano::account account;
				float priority;
			};
			class tag_hash
			{
			};
			class tag_priority
			{
			};
			// Tracks the ongoing account priorities
			// This only stores account priorities > 1.0f.
			// Accounts in the ledger but not in this list are assumed priority 1.0f.
			// Blocked accounts are assumed priority 0.0f
			boost::multi_index_container<priority_t,
			boost::multi_index::indexed_by<
			boost::multi_index::ordered_unique<boost::multi_index::tag<tag_hash>,
			boost::multi_index::member<priority_t, nano::account, &priority_t::account>>,
			boost::multi_index::ordered_non_unique<boost::multi_index::tag<tag_priority>,
			boost::multi_index::member<priority_t, float, &priority_t::priority>>>>
			priorities;
			static size_t const priorities_max = 65536;

			std::default_random_engine rng;

		public:
			using backoff_info_t = std::tuple<decltype (blocking), decltype (priorities)>; // <blocking, priorities>

			backoff_info_t backoff_info () const;
		};

		/** A single thread performing the ascending bootstrap algorithm
			Each thread tracks the number of outstanding requests over the network that have not yet completed.
		*/
		class thread : public std::enable_shared_from_this<thread>
		{
		public:
			explicit thread (bootstrap_ascending & bootstrap);

			/// Wait for there to be space for an additional request
			bool wait_available_request ();
			bool request_one ();
			void run ();
			std::shared_ptr<thread> shared ();
			nano::account pick_account ();
			// Send a request for a specific account or hash `start' to `tag' which contains a bootstrap socket.
			void send (std::shared_ptr<async_tag> tag, nano::hash_or_account const & start);
			// Reads a block from a specific `tag' / bootstrap socket.
			void read_block (std::shared_ptr<async_tag> tag);

			// given an account, pick the start point of the pull request
			nano::hash_or_account pick_start (const nano::account & account_a);

			std::atomic<int> requests{ 0 };
			static constexpr int requests_max = 1;

			bootstrap_ascending & bootstrap;
		};

		/** This class tracks the lifetime of a network request within a bootstrap attempt thread
			Each async_tag will increment the number of bootstrap requests tracked by a bootstrap_ascending::thread object
			A shared_ptr is used  for its copy semantics, as is required by callbacks through the boost asio system
			The tag also tracks success of a specific request. Success is defined by the correct receipt of a stream of blocks, followed by a not_a_block terminator
		*/
		class async_tag : public std::enable_shared_from_this<async_tag>
		{
		public:
			explicit async_tag (std::shared_ptr<nano::bootstrap::bootstrap_ascending::thread> bootstrap, nano::account const & account_a);

			// bootstrap_ascending::thread::requests will be decemented when destroyed.
			// If success () has been called, the socket will be reused, otherwise it will be abandoned therefore destroyed.
			~async_tag ();
			void success ();
			void connection_set (socket_channel const & connection);
			socket_channel & connection ();

			// Tracks the number of blocks received from this request
			std::atomic<int> blocks{ 0 };
			nano::account const account;

		private:
			bool success_m{ false };
			std::optional<socket_channel> connection_m;

			// storing weak pointers to dependencies to avoid dependency cycles
			// which cause problems during shutdown
			std::weak_ptr<bootstrap_ascending::thread> thread_weak;
			std::weak_ptr<nano::node> node_weak;
		};

		bool blocked (nano::account const & account);
		void inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block);
		void dump_stats ();

		account_sets::backoff_info_t backoff_info () const;

		// pull optimistically, pull unconfirmed blocks without limit
		static bool optimistic_pulling;

	private:
		account_sets accounts;
		connection_pool pool;

		static std::size_t constexpr parallelism = 16;
		static std::size_t constexpr request_message_count = 128;

		std::atomic<int> responses{ 0 };
		std::atomic<int> requests_total{ 0 };
		std::atomic<float> weights{ 0 };
		std::atomic<int> forwarded{ 0 };
		std::atomic<int> block_total{ 0 };
	};
}
}
