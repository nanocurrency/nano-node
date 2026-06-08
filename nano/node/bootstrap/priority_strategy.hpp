#pragma once

#include <nano/node/bootstrap/bootstrap_context.hpp>

#include <deque>
#include <thread>

namespace nano::bootstrap
{
class priority_strategy
{
public:
	explicit priority_strategy (bootstrap_context & ctx);

	void start ();
	void stop ();
	void run_one ();

private:
	void run ();

	using priority_result = account_sets_index::priority_result;

	// Blocks until at least one priority account is available, then returns a batch to request
	std::deque<priority_result> wait_priority_batch ();

	// Fills the local buffer with pull queries for the given batch, reading the ledger once for the whole batch
	void refill (std::deque<priority_result> const & batch);

	// Builds a pull query for a single priority account
	blocks_query prepare_query (nano::store::transaction const &, priority_result const &) const;

	struct buffered_request
	{
		blocks_query query;
		unsigned fails;
	};

	// Prefetched pull queries, drained one per request
	std::deque<buffered_request> buffer;

	bootstrap_context & ctx;
	std::thread thread;

	static std::size_t constexpr batch_size = 32;
};
}
