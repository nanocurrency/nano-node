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

class confirmation_height_processor final
{
public:
	confirmation_height_processor (nano::block_store &, nano::ledger &, nano::active_transactions &);
	~confirmation_height_processor ();
	void add (nano::block_hash const &);
	void stop ();

private:
	std::mutex mutex;
	std::condition_variable condition;
	std::queue<nano::block_hash> pending_confirmations;
	bool stopped{ false };
	nano::block_store & store;
	nano::ledger & ledger;
	nano::active_transactions & active;
	std::thread thread;
	constexpr static std::chrono::milliseconds batch_write_delta{ 100 };

	class block_hash_height_pair final
	{
	public:
		block_hash_height_pair (const nano::block_hash &, uint64_t height);

		nano::block_hash hash;
		uint64_t height;
	};

	class open_receive_details final
	{
	public:
		open_receive_details (const nano::account &,
		const block_hash_height_pair &);

		nano::account account;
		confirmation_height_processor::block_hash_height_pair block_hash_height_pair;
	};

	class open_receive_source_pair final
	{
	public:
		open_receive_source_pair (const nano::account &,
		block_hash_height_pair const &,
		const nano::block_hash &);

		confirmation_height_processor::open_receive_details open_receive_details;
		nano::block_hash source_hash;
	};

	void run ();
	void add_confirmation_height (nano::block_hash const &);
	void collect_unconfirmed_receive_and_sources_for_account (uint64_t, uint64_t, nano::block_hash &, const nano::block_hash &, std::stack<open_receive_source_pair> &, nano::account const &, nano::transaction &);
	void write_pending (std::unordered_map<nano::account, block_hash_height_pair> &);
	void update_confirmation_height (nano::account const &, block_hash_height_pair const &, std::unordered_map<nano::account, block_hash_height_pair> &);
};
}
