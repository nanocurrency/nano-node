#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/blockstore.hpp>

#include <boost/circular_buffer.hpp>

namespace nano
{
class ledger;
class read_transaction;
class logger_mt;
class write_database_queue;

class confirmation_height_bounded final
{
public:
	confirmation_height_bounded (nano::ledger &, nano::write_database_queue &, std::chrono::milliseconds, nano::logger_mt &, std::atomic<bool> &, nano::block_hash const &, std::function<void(std::vector<std::shared_ptr<nano::block>> const &)> const &, std::function<uint64_t ()> const &);
	bool pending_empty () const;
	void prepare_new ();
	void process ();
	bool cement_blocks ();

private:
	class top_and_next_hash final
	{
	public:
		nano::block_hash top;
		boost::optional<nano::block_hash> next;
		uint64_t next_height;
	};

	class confirmed_info
	{
	public:
		confirmed_info (uint64_t confirmed_height_a, nano::block_hash const & iterated_frontier);
		uint64_t confirmed_height;
		nano::block_hash iterated_frontier;
	};

	/* Holds confirmation height/cemented frontier in memory for accounts while iterating */
	std::unordered_map<account, confirmed_info> accounts_confirmed_info;
	std::atomic<uint64_t> accounts_confirmed_info_size{ 0 };

	class receive_chain_details final
	{
	public:
		receive_chain_details (nano::account const &, uint64_t, nano::block_hash const &, nano::block_hash const &, boost::optional<nano::block_hash>, uint64_t, nano::block_hash const &);
		nano::account account;
		uint64_t height;
		nano::block_hash hash;
		nano::block_hash top_level;
		boost::optional<nano::block_hash> next;
		uint64_t bottom_height;
		nano::block_hash bottom_most;
	};

	class preparation_data final
	{
	public:
		nano::transaction const & transaction;
		nano::block_hash const & top_most_non_receive_block_hash;
		bool already_cemented;
		boost::circular_buffer_space_optimized<nano::block_hash> & checkpoints;
		decltype (accounts_confirmed_info.begin ()) account_it;
		nano::confirmation_height_info const & confirmation_height_info;
		nano::account const & account;
		uint64_t bottom_height;
		nano::block_hash const & bottom_most;
		boost::optional<receive_chain_details> & receive_details;
		boost::optional<top_and_next_hash> & next_in_receive_chain;
	};

	class write_details final
	{
	public:
		write_details (nano::account const &, uint64_t, nano::block_hash const &, uint64_t, nano::block_hash const &);
		nano::account account;
		// This is the first block hash (bottom most) which is not cemented
		uint64_t bottom_height;
		nano::block_hash bottom_hash;
		// Desired cemented frontier
		uint64_t top_height;
		nano::block_hash top_hash;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (receive_chain_details const &, const nano::block_hash &);

		receive_chain_details receive_details;
		nano::block_hash source_hash;
	};

	/** The maximum number of blocks to be read in while iterating over a long account chain */
	static uint64_t constexpr batch_read_size = 4096;

	static uint32_t constexpr max_items{ 65536 };

	std::deque<write_details> pending_writes;
	std::atomic<uint64_t> pending_writes_size{ 0 };
	static uint32_t constexpr pending_writes_max_size{ max_items };
	nano::timer<std::chrono::milliseconds> timer;

	top_and_next_hash get_next_block (boost::optional<top_and_next_hash> const &, boost::circular_buffer_space_optimized<nano::block_hash> const &, boost::circular_buffer_space_optimized<receive_source_pair> const & receive_source_pairs, boost::optional<receive_chain_details> &);
	nano::block_hash get_least_unconfirmed_hash_from_top_level (nano::transaction const &, nano::block_hash const &, nano::account const &, nano::confirmation_height_info const &, uint64_t &);
	void prepare_iterated_blocks_for_cementing (preparation_data &);
	bool iterate (nano::read_transaction const &, uint64_t, nano::block_hash const &, boost::circular_buffer_space_optimized<nano::block_hash> &, nano::block_hash &, nano::block_hash const &, boost::circular_buffer_space_optimized<receive_source_pair> &, nano::account const &);

	nano::ledger & ledger;
	nano::write_database_queue & write_database_queue;
	std::chrono::milliseconds batch_separate_pending_min_time;
	nano::logger_mt & logger;
	std::atomic<bool> & stopped;
	nano::block_hash const & original_hash;
	std::function<void(std::vector<std::shared_ptr<nano::block>> const &)> notify_observers_callback;
	std::function<uint64_t ()> awaiting_processing_size_callback;

	friend std::unique_ptr<nano::container_info_component> collect_container_info (confirmation_height_bounded &, const std::string & name_a);
};

std::unique_ptr<nano::container_info_component> collect_container_info (confirmation_height_bounded &, const std::string & name_a);
}