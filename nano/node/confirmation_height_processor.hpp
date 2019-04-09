#pragma once

#include <condition_variable>
#include <mutex>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>
#include <queue>
#include <stack>
#include <thread>
#include <unordered_map>

namespace nano
{
class block_store;
class ledger;
class active_transactions;
class transaction;
class logger_mt;

class pending_confirmation_height
{
public:
	size_t size ();

private:
	std::mutex mutex;
	std::queue<nano::block_hash> queue;

	friend class confirmation_height_processor;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (pending_confirmation_height &, const std::string &);

class confirmation_height_processor final
{
public:
	confirmation_height_processor (pending_confirmation_height &, nano::block_store &, nano::ledger &, nano::active_transactions &, nano::logger_mt &);
	~confirmation_height_processor ();
	void add (nano::block_hash const &);
	void stop ();

private:
	class conf_height_details final
	{
	public:
		conf_height_details (nano::account const &, nano::block_hash const &, uint64_t);

		nano::account account;
		nano::block_hash hash;
		uint64_t height;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (conf_height_details const &, const nano::block_hash &);

		conf_height_details receive_details;
		nano::block_hash source_hash;
	};

	std::condition_variable condition;
	nano::pending_confirmation_height & pending_confirmations;
	std::atomic<bool> stopped{ false };
	nano::block_store & store;
	nano::ledger & ledger;
	nano::active_transactions & active;
	nano::logger_mt & logger;
	std::mutex receive_source_pairs_mutex;
	std::stack<receive_source_pair> receive_source_pairs; // Only single writer allowed, multiple readers
	std::thread thread;
	constexpr static std::chrono::milliseconds batch_write_delta{ 100 };

	void run ();
	void add_confirmation_height (nano::block_hash const &);
	void collect_unconfirmed_receive_and_sources_for_account (uint64_t, uint64_t, nano::block_hash &, const nano::block_hash &, std::stack<receive_source_pair> &, nano::account const &, nano::transaction &);
	bool write_pending (std::queue<conf_height_details> &);
	size_t receive_source_pairs_size ();

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor &, const std::string &);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor &, const std::string &);
}
