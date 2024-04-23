#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/bucket.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

nano::scheduler::priority::priority (nano::node & node_a, nano::stats & stats_a) :
	node{ node_a },
	stats{ stats_a }
{
	setup_buckets ();
}

nano::scheduler::priority::~priority ()
{
}

std::shared_ptr<nano::block> nano::scheduler::priority::activate (secure::transaction const & transaction, nano::account const & account)
{
	debug_assert (!account.is_zero ());
	auto head = node.ledger.confirmed.account_head (transaction, account);
	if (node.ledger.any.account_head (transaction, account) == head)
	{
		return nullptr;
	}
	auto block = node.ledger.any.block_get (transaction, node.ledger.any.block_successor (transaction, { head.is_zero () ? static_cast<nano::uint256_union> (account) : head, head }).value ());
	if (!node.ledger.dependents_confirmed (transaction, *block))
	{
		return nullptr;
	}
	return activate (transaction, block);
}

std::shared_ptr<nano::block> nano::scheduler::priority::activate (secure::transaction const & transaction, std::shared_ptr<nano::block> const & block)
{
	auto account = block->account ();
	auto head = node.ledger.confirmed.account_head (transaction, account);
	auto const balance_priority = std::max (block->balance (), node.ledger.any.block_balance (transaction, block->previous ()).value_or (0));
	auto timestamp_calculation = [&] () {
		std::chrono::milliseconds diff{ 0 };
		if (!head.is_zero ())
		{
			auto timestamp = node.ledger.confirmed.block_get (transaction, head)->sideband ().timestamp;
			diff = std::chrono::seconds{ nano::seconds_since_epoch () - timestamp };
		}
		// Use clock with higher precision than seconds
		auto time = std::chrono::steady_clock::now () - diff; // New accounts get current timestamp i.e. lowest priority
		return time;
	};
	auto const time_priority = timestamp_calculation ();

	node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);
	node.logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_activated,
	nano::log::arg{ "account", account.to_account () }, // TODO: Convert to lazy eval
	nano::log::arg{ "block", block },
	nano::log::arg{ "time", time_priority.time_since_epoch ().count () },
	nano::log::arg{ "priority", balance_priority });

	stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority);
	auto result = node.active.insert (block);
	if (result.inserted)
	{
		stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority_success);
	}
	if (result.election != nullptr)
	{
		result.election->transition_active ();
	}

	nano::unique_lock<nano::mutex> lock{ mutex };
	if (tracking.find (block->hash ()) != tracking.end ())
	{
		return nullptr;
	}
	auto iter = buckets.upper_bound (balance_priority);
	--iter; // Iterator points to bucket after the target priority
	debug_assert (iter != buckets.end ());
	auto & bucket = *iter->second;
	auto removed = bucket.insert (time_priority, block);
	if (removed == nullptr)
	{
		node.logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_insert,
		nano::log::arg{ "block", block->hash ().to_string () },
		nano::log::arg{ "time", time_priority.time_since_epoch ().count () });
		// Bucket was not at full capacity
		auto inserted = tracking.emplace (block->hash (), &bucket);
		lock.unlock ();
		debug_assert (inserted.second);
		return nullptr;
	}
	else if (removed != block)
	{
		node.logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_overflow,
		nano::log::arg{ "block", block->hash ().to_string () },
		nano::log::arg{ "time", time_priority.time_since_epoch ().count () });
		// Bucket was full and another block was lowest priority
		auto inserted = tracking.emplace (block->hash (), &bucket);
		lock.unlock ();
		node.active.erase (*removed);
		debug_assert (inserted.second);
		return removed;
	}
	else
	{
		node.logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_reject,
		nano::log::arg{ "block", block->hash ().to_string () },
		nano::log::arg{ "time", time_priority.time_since_epoch ().count () });
		lock.unlock ();
		// Bucket was full and block inserted was lowest priority
		node.active.erase (*block);
		return block;
	}
}

void nano::scheduler::priority::election_stopped (std::shared_ptr<nano::election> election)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	for (auto const & [hash, block] : election->blocks ())
	{
		if (auto existing = tracking.find (hash); existing != tracking.end ())
		{
			auto erased = existing->second->erase (hash);
			debug_assert (erased == 1);
			tracking.erase (existing);
		}
	}
}

void nano::scheduler::priority::setup_buckets ()
{
	auto build_region = [this] (uint128_t const & begin, uint128_t const & end, size_t count) {
		auto width = (end - begin) / count;
		for (auto i = 0; i < count; ++i)
		{
			buckets.emplace (begin + i * width, std::make_unique<nano::scheduler::bucket> (node.config.priority_scheduler.depth));
		}
	};
	build_region (0, uint128_t{ 1 } << 88, 1);
	build_region (uint128_t{ 1 } << 88, uint128_t{ 1 } << 92, 2);
	build_region (uint128_t{ 1 } << 92, uint128_t{ 1 } << 96, 4);
	build_region (uint128_t{ 1 } << 96, uint128_t{ 1 } << 100, 8);
	build_region (uint128_t{ 1 } << 100, uint128_t{ 1 } << 104, 16);
	build_region (uint128_t{ 1 } << 104, uint128_t{ 1 } << 108, 16);
	build_region (uint128_t{ 1 } << 108, uint128_t{ 1 } << 112, 8);
	build_region (uint128_t{ 1 } << 112, uint128_t{ 1 } << 116, 4);
	build_region (uint128_t{ 1 } << 116, uint128_t{ 1 } << 120, 2);
	build_region (uint128_t{ 1 } << 120, uint128_t{ 1 } << 127, 1);
}

std::unique_ptr<nano::container_info_component> nano::scheduler::priority::collect_container_info (std::string const & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	return composite;
}
