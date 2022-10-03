#pragma once

#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>

#include <functional>
#include <future>
#include <random>
#include <thread>
#include <vector>

namespace nano
{
class block_processor;
class ledger;

namespace transport
{
	class channel;
}

namespace bootstrap
{
	class bootstrap_ascending
	{
	public:
		bootstrap_ascending (nano::node &, nano::store &, nano::block_processor &, nano::ledger &, nano::stat &);
		~bootstrap_ascending ();

		void start ();
		void stop ();

		/**
		 * Inspects a block that has been processed by the block processor
		 * - Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
		 * - Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
		 */
		void inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block);

		bool blocked (nano::account const & account) const;

		void dump_stats ();

	private: // Dependencies
		nano::node & node;
		nano::store & store;
		nano::block_processor & block_processor;
		nano::ledger & ledger;
		nano::stat & stats;

	private:
		/**
		 * Seed backoffs with accounts from the ledger
		 */
		void seed ();

		void debug_log (const std::string &) const;
		// void dump_miss_histogram ();

	public:
		using socket_channel_t = std::pair<std::shared_ptr<nano::socket>, std::shared_ptr<nano::transport::channel>>;

		class connection_pool
		{
		public:
			explicit connection_pool (bootstrap_ascending & bootstrap);
			/** Given a tag context, find or create a connection to a peer and then call the op callback */
			std::future<std::optional<socket_channel_t>> request ();
			void put (socket_channel_t const & connection);

		private:
			bootstrap_ascending & bootstrap;

			std::deque<socket_channel_t> connections;
			mutable nano::mutex mutex;
		};

		/** This class tracks accounts various account sets which are shared among the multiple bootstrap threads */
		class account_sets
		{
		public:
			explicit account_sets (nano::stat &);

			void prioritize (nano::account const & account, float priority);
			void block (nano::account const & account, nano::block_hash const & dependency);
			void unblock (nano::account const & account, nano::block_hash const & hash);
			void force_unblock (nano::account const & account);
			void dump () const;
			nano::account next ();

		public:
			bool blocked (nano::account const & account) const;

		private: // Dependencies
			nano::stat & stats;

		private:
			nano::account random ();

			// A forwarded account is an account that has recently had a new block inserted or has been a destination reference and therefore is a more likely candidate for furthur block retrieval
			std::unordered_set<nano::account> forwarding;
			// A blocked account is an account that has failed to insert a block because the source block is gapped.
			// An account is unblocked once it has a block successfully inserted.
			std::map<nano::account, nano::block_hash> blocking;
			// Tracks the number of requests for additional blocks without a block being successfully returned
			// Each time a block is inserted to an account, this number is reset.
			std::map<nano::account, float> backoff;

			static size_t constexpr backoff_exclusion = 4;
			std::default_random_engine rng;

			/**
				Convert a vector of attempt counts to a probability vector suitable for std::discrete_distribution
				This implementation applies 1/2^i for each element, effectivly an exponential backoff
			*/
			std::vector<double> probability_transform (std::vector<decltype (backoff)::mapped_type> const & attempts) const;

		public:
			using backoff_info_t = std::tuple<decltype (forwarding), decltype (blocking), decltype (backoff)>; // <forwarding, blocking, backoff>

			backoff_info_t backoff_info () const;
		};

		class pull_tag : public std::enable_shared_from_this<pull_tag>
		{
		public:
			enum class process_result
			{
				success,
				end,
				error,
				malice,
			};

		public:
			pull_tag (bootstrap_ascending &, socket_channel_t, nano::hash_or_account const & start);

			std::future<bool> send ();
			std::future<bool> read ();

			std::function<void (std::shared_ptr<nano::block> & block)> process_block{ [] (auto &) { debug_assert (false, "bootstrap_ascending::bulk_pull_tag callback empty"); } };

		private:
			void read_block (std::function<void (process_result)> callback);
			process_result block_received (boost::system::error_code ec, std::shared_ptr<nano::block> block);

		private:
			bootstrap_ascending & bootstrap;

			std::shared_ptr<nano::socket> socket;
			std::shared_ptr<nano::transport::channel> channel;
			const nano::hash_or_account start;

			std::shared_ptr<nano::bootstrap::block_deserializer> deserializer;

		private:
			std::atomic<int> block_counter{ 0 };
		};

		/** A single thread performing the ascending bootstrap algorithm
			Each thread tracks the number of outstanding requests over the network that have not yet completed.
		*/
		class thread
		{
		public:
			explicit thread (bootstrap_ascending & bootstrap);

			void run ();

			std::function<void (std::shared_ptr<nano::block> & block)> process_block{ [] (auto &) { debug_assert (false, "bootstrap_ascending::thread callback empty"); } };

		private:
			/// Wait for there to be space for an additional request
			bool wait_available_request ();
			bool request_one ();
			std::optional<socket_channel_t> pick_connection ();
			nano::account pick_account ();

			std::atomic<int> requests{ 0 };
			static constexpr int requests_max = 1;

		public: // Convinience reference rather than internally using a pointer
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
			explicit async_tag (std::shared_ptr<nano::bootstrap::bootstrap_ascending::thread> bootstrap);

			// bootstrap_ascending::thread::requests will be decemented when destroyed.
			// If success () has been called, the socket will be reused, otherwise it will be abandoned therefore destroyed.
			~async_tag ();
			void success ();
			void connection_set (socket_channel_t const & connection);
			socket_channel_t & connection ();

			// Tracks the number of blocks received from this request
			std::atomic<int> blocks{ 0 };

		private:
			bootstrap_ascending & bootstrap;
			bootstrap_ascending::thread & bootstrap_thread;

			bool success_m{ false };
			std::optional<socket_channel_t> connection_m;
		};

		account_sets::backoff_info_t backoff_info () const;

	private:
		account_sets accounts;
		connection_pool pool;

		std::atomic<bool> stopped{ false };
		mutable nano::mutex mutex;
		nano::condition_variable condition;
		std::vector<std::thread> threads;

		static std::size_t constexpr parallelism = 4;
		static std::size_t constexpr request_message_count = 128;

		std::atomic<int> responses{ 0 };
		std::atomic<int> requests_total{ 0 };
		std::atomic<float> weights{ 0 };
		std::atomic<int> forwarded{ 0 };
		std::atomic<int> block_total{ 0 };
	};
}

template <typename T>
std::shared_ptr<std::promise<T>> shared_promise ()
{
	std::promise<bool> promise;
	auto shared_promise = std::make_shared<decltype (promise)> (std::move (promise));
	return shared_promise;
}
}
