#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/bootstrap/database_strategy.hpp>
#include <nano/node/nodeconfig.hpp>

namespace nano::bootstrap
{
database_strategy::database_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a }
{
}

void database_strategy::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_database_scan);
		run ();
	});
}

void database_strategy::stop ()
{
	join_or_pass (thread);
}

void database_strategy::run ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		// Avoid high churn rate of database requests
		bool should_throttle = !ctx.database_scan.warmed_up () && ctx.throttle.throttled ();
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_database);
		run_one (should_throttle);
		lock.lock ();
	}
}

void database_strategy::run_one (bool should_throttle)
{
	ctx.wait_block_processor ();

	auto channel = ctx.wait_channel ();
	if (!channel)
	{
		return;
	}

	auto query = wait_database (should_throttle);
	if (!query)
	{
		return;
	}

	// The database scan always issues safe requests; record the pull start point
	ctx.stats.inc (nano::stat::type::bootstrap_database, query->type == query_type::blocks_by_hash ? nano::stat::detail::from_confirmed : nano::stat::detail::from_open);

	ctx.send (channel, *query, query_source::database);
}

std::optional<blocks_query> database_strategy::next_database (bool should_throttle)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (ctx.config.database_warmup_ratio > 0);

	// Throttling increases the weight of database requests
	if (!ctx.database_limiter.try_consume (should_throttle ? ctx.config.database_warmup_ratio : 1))
	{
		return std::nullopt;
	}
	auto query = ctx.database_scan.next ([this] (nano::account const & account) {
		return ctx.count_tags (account, query_source::database) == 0;
	});
	if (!query)
	{
		return std::nullopt;
	}
	ctx.stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_database);
	return query;
}

std::optional<blocks_query> database_strategy::wait_database (bool should_throttle)
{
	std::optional<blocks_query> result;
	ctx.wait ([this, &result, should_throttle] () {
		debug_assert (!ctx.mutex.try_lock ());
		result = next_database (should_throttle);
		return result.has_value ();
	});
	return result;
}
}
