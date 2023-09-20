#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/relaxed_atomic.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/store/component.hpp>

#include <chrono>
#include <unordered_map>

namespace nano
{
class ledger;
class read_transaction;
class logging;
class logger_mt;
class write_database_queue;
class write_guard;

class confirmation_height_unbounded final
{
public:
	confirmation_height_unbounded (nano::ledger &, nano::write_database_queue &, std::chrono::milliseconds batch_separate_pending_min_time, nano::logging const &, nano::logger_mt &, std::atomic<bool> & stopped, uint64_t & batch_write_size, std::function<void (std::vector<std::shared_ptr<nano::block>> const &)> const & cemented_callback, std::function<void (nano::block_hash const &)> const & already_cemented_callback, std::function<uint64_t ()> const & awaiting_processing_size_query);
	bool pending_empty () const;
	void clear_process_vars ();
	void process (std::shared_ptr<nano::block> original_block);
	void cement_blocks (nano::write_guard &);
	bool has_iterated_over_block (nano::block_hash const &) const;

private:
	class confirmed_iterated_pair
	{
	public:
		confirmed_iterated_pair (uint64_t confirmed_height_a, uint64_t iterated_height_a);
		uint64_t confirmed_height;
		uint64_t iterated_height;
	};

	class conf_height_details final
	{
	public:
		conf_height_details (nano::account const &, nano::block_hash const &, uint64_t, uint64_t, std::vector<nano::block_hash> const &);

		nano::account account;
		nano::block_hash hash;
		uint64_t height;
		uint64_t num_blocks_confirmed;
		std::vector<nano::block_hash> block_callback_data;
		std::vector<nano::block_hash> source_block_callback_data;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (std::shared_ptr<conf_height_details> const &, nano::block_hash const &);

		std::shared_ptr<conf_height_details> receive_details;
		nano::block_hash source_hash;
	};

	// All of the atomic variables here just track the size for use in collect_container_info.
	// This is so that no mutexes are needed during the algorithm itself, which would otherwise be needed
	// for the sake of a rarely used RPC call for debugging purposes. As such the sizes are not being acted
	// upon in any way (does not synchronize with any other data).
	// This allows the load and stores to use relaxed atomic memory ordering.
	std::unordered_map<account, confirmed_iterated_pair> confirmed_iterated_pairs;
	nano::relaxed_atomic_integral<uint64_t> confirmed_iterated_pairs_size{ 0 };
	std::shared_ptr<nano::block> get_block_and_sideband (nano::block_hash const &, store::transaction const &);
	std::deque<conf_height_details> pending_writes;
	nano::relaxed_atomic_integral<uint64_t> pending_writes_size{ 0 };
	std::unordered_map<nano::block_hash, std::weak_ptr<conf_height_details>> implicit_receive_cemented_mapping;
	nano::relaxed_atomic_integral<uint64_t> implicit_receive_cemented_mapping_size{ 0 };

	mutable nano::mutex block_cache_mutex;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> block_cache;
	uint64_t block_cache_size () const;

	nano::timer<std::chrono::milliseconds> timer;

	class preparation_data final
	{
	public:
		uint64_t block_height;
		uint64_t confirmation_height;
		uint64_t iterated_height;
		decltype (confirmed_iterated_pairs.begin ()) account_it;
		nano::account const & account;
		std::shared_ptr<conf_height_details> receive_details;
		bool already_traversed;
		nano::block_hash const & current;
		std::vector<nano::block_hash> const & block_callback_data;
		std::vector<nano::block_hash> const & orig_block_callback_data;
	};

	void collect_unconfirmed_receive_and_sources_for_account (uint64_t, uint64_t, std::shared_ptr<nano::block> const &, nano::block_hash const &, nano::account const &, store::read_transaction const &, std::vector<receive_source_pair> &, std::vector<nano::block_hash> &, std::vector<nano::block_hash> &, std::shared_ptr<nano::block> original_block);
	void prepare_iterated_blocks_for_cementing (preparation_data &);

	nano::ledger & ledger;
	nano::write_database_queue & write_database_queue;
	std::chrono::milliseconds batch_separate_pending_min_time;
	nano::logger_mt & logger;
	std::atomic<bool> & stopped;
	uint64_t & batch_write_size;
	nano::logging const & logging;

	std::function<void (std::vector<std::shared_ptr<nano::block>> const &)> notify_observers_callback;
	std::function<void (nano::block_hash const &)> notify_block_already_cemented_observers_callback;
	std::function<uint64_t ()> awaiting_processing_size_callback;

	friend class confirmation_height_dynamic_algorithm_no_transition_while_pending_Test;
	friend std::unique_ptr<nano::container_info_component> collect_container_info (confirmation_height_unbounded &, std::string const & name_a);
};

std::unique_ptr<nano::container_info_component> collect_container_info (confirmation_height_unbounded &, std::string const & name_a);
}
