#include <nano/lib/stats.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/bootstrap/priority_strategy.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/confirmation_height.hpp>

#include <algorithm>

namespace nano::bootstrap
{
priority_strategy::priority_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a }
{
}

void priority_strategy::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap);
		run ();
	});
}

void priority_strategy::stop ()
{
	join_or_pass (thread);
}

void priority_strategy::run ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_priority);
		run_one ();
		lock.lock ();
	}
}

void priority_strategy::run_one ()
{
	ctx.wait_block_processor ();

	// Refill the buffer with a fresh batch of pull queries once it runs dry
	if (buffer.empty ())
	{
		ctx.stats.inc (nano::stat::type::bootstrap_priority, nano::stat::detail::refill);

		auto batch = wait_priority_batch ();
		if (batch.empty ())
		{
			return; // Stopped
		}
		refill (batch);
	}

	auto channel = ctx.wait_channel ();
	if (!channel)
	{
		return;
	}

	auto entry = buffer.front ();
	buffer.pop_front ();

	ctx.stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_priority);

	bool sent = ctx.send (channel, entry.query, query_source::priority);

	// Only cooldown accounts that are likely to have more blocks
	// This is to avoid requesting blocks from the same frontier multiple times, before the block processor had a chance to process them
	// Not throttling accounts that are probably up-to-date allows us to evict them from the priority set faster
	if (sent && entry.fails == 0)
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };
		ctx.accounts.timestamp_set (entry.query.account);
	}
}

auto priority_strategy::wait_priority_batch () -> std::deque<priority_result>
{
	// Wait until at least one priority account is available, then grab a batch
	std::deque<priority_result> batch;
	ctx.wait ([this, &batch] () {
		batch = ctx.accounts.next_priority_batch ([this] (nano::account const & account) {
			return ctx.count_tags (account, query_source::priority) < 4;
		},
		batch_size);
		return !batch.empty ();
	});
	return batch;
}

void priority_strategy::refill (std::deque<priority_result> const & batch)
{
	// Build all pull queries for the batch using a single read transaction
	auto transaction = ctx.ledger.store.tx_begin_read ();
	for (auto const & entry : batch)
	{
		buffer.push_back ({ prepare_query (transaction, entry), entry.fails });
	}
}

blocks_query priority_strategy::prepare_query (nano::store::transaction const & transaction, priority_result const & entry) const
{
	// Decide how many blocks to request based on the account priority
	size_t const min_pull_count = 2;
	auto count = std::clamp (static_cast<size_t> (entry.priority), min_pull_count, nano::bootstrap_server::max_blocks);
	count = std::min (count, ctx.config.max_pull_count);

	blocks_query query{};
	query.account = entry.account;
	query.count = count;

	// Probabilistically choose between requesting blocks from account frontier or confirmed frontier
	// Optimistic requests start from the (possibly unconfirmed) account frontier and are vulnerable to bootstrap poisoning
	// Safe requests start from the confirmed frontier and given enough time will eventually resolve forks
	bool const optimistic = ctx.rng.random (100) < ctx.config.optimistic_request_percentage;

	// Check if the account picked has blocks, if it does, start the pull from the highest block
	if (auto info = ctx.ledger.store.account.get (transaction, entry.account))
	{
		if (optimistic) // Optimistic request case
		{
			ctx.stats.inc (nano::stat::type::bootstrap_priority, nano::stat::detail::from_frontier);

			query.type = query_type::blocks_by_hash;
			query.start = info->head;
		}
		else // Pessimistic (safe) request case
		{
			if (auto conf_info = ctx.ledger.store.confirmation_height.get (transaction, entry.account))
			{
				ctx.stats.inc (nano::stat::type::bootstrap_priority, nano::stat::detail::from_confirmed);

				query.type = query_type::blocks_by_hash;
				query.start = conf_info->frontier;
			}
			else
			{
				ctx.stats.inc (nano::stat::type::bootstrap_priority, nano::stat::detail::from_open);

				query.type = query_type::blocks_by_account;
				query.start = entry.account;
			}
		}
	}
	else
	{
		ctx.stats.inc (nano::stat::type::bootstrap_priority, nano::stat::detail::from_open);

		query.type = query_type::blocks_by_account;
		query.start = entry.account;
	}

	return query;
}
}
