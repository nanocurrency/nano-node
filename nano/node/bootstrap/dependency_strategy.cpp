#include <nano/lib/logging.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/dependency_strategy.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/nodeconfig.hpp>

#include <optional>

using namespace std::chrono_literals;

namespace nano::bootstrap
{
dependency_strategy::dependency_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a }
{
}

void dependency_strategy::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_dependency_walker);
		run ();
	});

	debug_assert (!sync_thread.joinable ());
	sync_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_dependency_sync);
		run_sync ();
	});
}

void dependency_strategy::stop ()
{
	nano::join_or_pass (thread);
	nano::join_or_pass (sync_thread);
}

void dependency_strategy::run ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_dependencies);
		run_one ();
		lock.lock ();
	}
}

void dependency_strategy::run_one ()
{
	// No need to wait for block_processor, as we are not processing blocks
	auto channel = ctx.wait_channel (strategy::dependency);
	if (!channel)
	{
		return;
	}
	auto blocking = wait_blocking ();
	if (blocking.is_zero ())
	{
		return;
	}
	request_info (blocking, channel);
}

nano::block_hash dependency_strategy::next_blocking ()
{
	debug_assert (!ctx.mutex.try_lock ());

	auto blocking = ctx.accounts.next_blocking ([this] (nano::block_hash const & hash) {
		return ctx.count_tags (hash, strategy::dependency) == 0;
	});
	if (blocking.is_zero ())
	{
		return { 0 };
	}
	ctx.stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_blocking);
	return blocking;
}

nano::block_hash dependency_strategy::wait_blocking ()
{
	auto result = ctx.wait_result ([this] () -> std::optional<nano::block_hash> {
		auto blocking = next_blocking ();
		if (blocking.is_zero ())
		{
			return std::nullopt;
		}
		return blocking;
	});
	return result.value_or (nano::block_hash{ 0 });
}

bool dependency_strategy::request_info (nano::block_hash hash, std::shared_ptr<nano::transport::channel> const & channel)
{
	account_info_query query{};
	query.target = hash;

	return ctx.send (channel, query, strategy::dependency);
}

bool dependency_strategy::process (nano::messages::asc_pull_ack::account_info_payload const & response, async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (tag.type () == query_type::account_info_by_hash);
	debug_assert (!tag.hash.is_zero ());

	if (response.account.is_zero ())
	{
		ctx.stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::account_info_empty);
		return true; // OK, but nothing to do
	}

	ctx.stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::account_info);

	// Prioritize account containing the dependency
	ctx.accounts.dependency_update (tag.hash, response.account);
	ctx.accounts.priority_set (response.account, account_sets_index::priority_cutoff); // Use the lowest possible priority here

	return true; // OK, no way to verify the response
}

void dependency_strategy::run_sync ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		// Reinsert known dependencies into the priority set
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::sync_dependencies);
		auto synced = ctx.accounts.sync_dependencies ();
		ctx.logger.debug (nano::log::type::bootstrap, "Synced {} dependencies", synced);
		ctx.condition.wait_for (lock, nano::is_dev_run () ? 500ms : 60s, [this] () { return ctx.stopped; });
	}
}
}
