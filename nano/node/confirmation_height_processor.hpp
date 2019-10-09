#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace nano
{
class ledger;
class active_transactions;
class read_transaction;
class logger_mt;
class write_database_queue;

class pending_confirmation_height
{
public:
	size_t size ();
	bool is_processing_block (nano::block_hash const &);
	nano::block_hash current ();

private:
	std::mutex mutex;
	std::unordered_set<nano::block_hash> pending;
	/** This is the last block popped off the confirmation height pending collection */
	nano::block_hash current_hash{ 0 };
	friend class confirmation_height_processor;
	friend class confirmation_height_pending_observer_callbacks_Test;
	friend class confirmation_height_dependent_election_Test;
	friend class confirmation_height_dependent_election_after_already_cemented_Test;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (pending_confirmation_height &, const std::string &);

class confirmation_height_processor final
{
public:
	confirmation_height_processor (pending_confirmation_height &, nano::ledger &, nano::active_transactions &, nano::write_database_queue &, std::chrono::milliseconds, nano::logger_mt &);
	~confirmation_height_processor ();
	void add (nano::block_hash const &);
	void stop ();
	void pause ();
	void unpause ();

	/** The maximum amount of accounts to iterate over while writing */
	static uint64_t constexpr batch_write_size = 2048;

	/** The maximum number of blocks to be read in while iterating over a long account chain */
	static uint64_t constexpr batch_read_size = 4096;

private:
	class callback_data final
	{
	public:
		callback_data (std::shared_ptr<nano::block> const &, nano::block_sideband const &, nano::election_status_type);
		std::shared_ptr<nano::block> block;
		nano::block_sideband sideband;
		nano::election_status_type election_status_type;
	};

	class conf_height_details final
	{
	public:
		conf_height_details (nano::account const &, nano::block_hash const &, uint64_t, uint64_t, std::vector<callback_data> const &);

		nano::account account;
		nano::block_hash hash;
		uint64_t height;
		uint64_t num_blocks_confirmed;
		std::vector<callback_data> block_callbacks_required;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (conf_height_details const &, const nano::block_hash &);

		conf_height_details receive_details;
		nano::block_hash source_hash;
	};

	class confirmed_iterated_pair
	{
	public:
		confirmed_iterated_pair (uint64_t confirmed_height_a, uint64_t iterated_height_a);
		uint64_t confirmed_height;
		uint64_t iterated_height;
	};

	nano::condition_variable condition;
	nano::pending_confirmation_height & pending_confirmations;
	std::atomic<bool> stopped{ false };
	std::atomic<bool> paused{ false };
	nano::ledger & ledger;
	nano::active_transactions & active;
	nano::logger_mt & logger;
	std::atomic<uint64_t> receive_source_pairs_size{ 0 };
	std::vector<receive_source_pair> receive_source_pairs;

	std::deque<conf_height_details> pending_writes;
	// Store the highest confirmation heights for accounts in pending_writes to reduce unnecessary iterating,
	// and iterated height to prevent iterating over the same blocks more than once from self-sends or "circular" sends between the same accounts.
	std::unordered_map<account, confirmed_iterated_pair> confirmed_iterated_pairs;
	nano::timer<std::chrono::milliseconds> timer;
	nano::write_database_queue & write_database_queue;
	std::chrono::milliseconds batch_separate_pending_min_time;
	std::thread thread;

	void run ();
	void add_confirmation_height (nano::block_hash const &);
	void collect_unconfirmed_receive_and_sources_for_account (uint64_t, uint64_t, nano::block_hash const &, nano::account const &, nano::read_transaction const &, std::vector<callback_data> &);
	bool write_pending (std::deque<conf_height_details> &);

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor &, const std::string &);
	friend class confirmation_height_pending_observer_callbacks_Test;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor &, const std::string &);
}
