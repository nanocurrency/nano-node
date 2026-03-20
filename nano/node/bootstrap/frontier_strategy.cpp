#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/bootstrap/frontier_strategy.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/pending.hpp>

using namespace std::chrono_literals;

namespace nano::bootstrap
{
frontier_strategy::frontier_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a }
{
}

void frontier_strategy::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_frontier_scan);
		run ();
	});
}

void frontier_strategy::stop ()
{
	nano::join_or_pass (thread);
}

void frontier_strategy::run ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_frontiers);
		run_one ();
		lock.lock ();
	}
}

void frontier_strategy::run_one ()
{
	// No need to wait for block_processor, as we are not processing blocks
	ctx.wait ([this] () {
		return !ctx.accounts.priority_half_full ();
	});
	ctx.wait ([this] () {
		return ctx.frontiers_limiter.should_pass (1);
	});
	ctx.wait ([this] () {
		return ctx.workers.queued_tasks () < ctx.config.frontier_scan.max_pending;
	});
	auto channel = ctx.wait_channel ();
	if (!channel)
	{
		return;
	}
	auto frontier = wait_frontier ();
	if (frontier.is_zero ())
	{
		return;
	}
	request_frontiers (frontier, channel);
}

nano::account frontier_strategy::wait_frontier ()
{
	nano::account result{ 0 };
	ctx.wait ([this, &result] () {
		debug_assert (!ctx.mutex.try_lock ());
		result = ctx.frontiers.next ();
		if (!result.is_zero ())
		{
			ctx.stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_frontier);
			return true;
		}
		return false;
	});
	return result;
}

bool frontier_strategy::request_frontiers (nano::account start, std::shared_ptr<nano::transport::channel> const & channel)
{
	async_tag tag{};
	tag.type = query_type::frontiers;
	tag.source = query_source::frontiers;

	frontier_tag_payload payload{};
	payload.start = start;
	tag.payload = payload;

	// Build the message
	nano::messages::asc_pull_req message{ ctx.network_constants };
	message.id = tag.id;
	message.type = nano::messages::asc_pull_type::frontiers;

	nano::messages::asc_pull_req::frontiers_payload msg_pld;
	msg_pld.start = start;
	msg_pld.count = nano::messages::asc_pull_ack::frontiers_payload::max_frontiers;
	message.payload = msg_pld;
	message.update_header ();

	ctx.logger.debug (nano::log::type::bootstrap, "Requesting frontiers starting from: {} from: {}", start, channel);

	return ctx.send (channel, std::move (message), tag);
}

bool frontier_strategy::process (nano::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (tag.type == query_type::frontiers);

	release_assert (std::holds_alternative<frontier_tag_payload> (tag.payload));
	auto const & payload = std::get<frontier_tag_payload> (tag.payload);
	debug_assert (!payload.start.is_zero ());

	if (response.frontiers.empty ())
	{
		ctx.stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::frontiers_empty);
		return true; // OK, but nothing to do
	}

	ctx.stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::frontiers);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_verify_frontiers, nano::stat::detail::ok);
			ctx.stats.add (nano::stat::type::bootstrap, nano::stat::detail::frontiers, nano::stat::dir::in, response.frontiers.size ());

			ctx.frontiers.process (payload.start, response.frontiers);

			// Allow some overfill to avoid unnecessarily dropping responses
			if (ctx.workers.queued_tasks () < ctx.config.frontier_scan.max_pending * 4)
			{
				ctx.workers.post ([this, frontiers_l = response.frontiers] {
					process_frontiers (frontiers_l);
				});
			}
			else
			{
				ctx.stats.add (nano::stat::type::bootstrap, nano::stat::detail::frontiers_dropped, response.frontiers.size ());
			}
		}
		break;
		case verify_result::nothing_new:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_verify_frontiers, nano::stat::detail::nothing_new);
		}
		break;
		case verify_result::invalid:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_verify_frontiers, nano::stat::detail::invalid);
		}
		break;
	}

	return result != verify_result::invalid;
}

verify_result frontier_strategy::verify (nano::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag) const
{
	release_assert (std::holds_alternative<frontier_tag_payload> (tag.payload));
	auto const & payload = std::get<frontier_tag_payload> (tag.payload);
	auto const & frontiers = response.frontiers;

	if (frontiers.empty ())
	{
		return verify_result::nothing_new;
	}

	// Ensure frontiers accounts are in ascending order
	nano::account previous{ 0 };
	for (auto const & [account, _] : frontiers)
	{
		if (account.number () <= previous.number ())
		{
			return verify_result::invalid;
		}
		previous = account;
	}

	// Ensure the frontiers are larger or equal to the requested frontier
	if (frontiers.front ().first.number () < payload.start.number ())
	{
		return verify_result::invalid;
	}

	return verify_result::ok;
}

void frontier_strategy::process_frontiers (std::deque<std::pair<nano::account, nano::block_hash>> const & frontiers)
{
	release_assert (!frontiers.empty ());

	// Accounts must be passed in ascending order
	debug_assert (std::adjacent_find (frontiers.begin (), frontiers.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs.first.number () >= rhs.first.number ();
	})
	== frontiers.end ());

	ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::processing_frontiers);

	size_t outdated = 0;
	size_t pending = 0;

	// Accounts with outdated frontiers to sync
	std::deque<nano::account> result;
	{
		auto transaction = ctx.ledger.tx_begin_read ();

		auto const start = frontiers.front ().first;
		auto account_crawler = ctx.ledger.store.account.crawl (transaction, start);
		auto pending_crawler = ctx.ledger.store.pending.crawl (transaction, start);

		auto block_exists = [&] (nano::block_hash const & hash) {
			return ctx.ledger.any.block_exists_or_pruned (transaction, hash);
		};

		auto should_prioritize = [&] (nano::account const & account, nano::block_hash const & frontier) {
			account_crawler.skip_to (account);
			pending_crawler.skip_to (account);

			// Check if account exists in our ledger
			if (account_crawler && account_crawler->first == account)
			{
				// Check for frontier mismatch
				if (account_crawler->second.head != frontier)
				{
					// Check if frontier block exists in our ledger
					if (!block_exists (frontier))
					{
						outdated++;
						return true; // Frontier is outdated
					}
				}
				return false; // Account exists and frontier is up-to-date
			}

			// Check if account has pending blocks in our ledger
			if (pending_crawler && pending_crawler->first.account == account)
			{
				pending++;
				return true; // Account doesn't exist but has pending blocks in the ledger
			}

			return false; // Account doesn't exist in the ledger and has no pending blocks, can't be prioritized right now
		};

		for (auto const & [account, frontier] : frontiers)
		{
			if (should_prioritize (account, frontier))
			{
				result.push_back (account);
			}
		}
	}

	ctx.stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::processed, frontiers.size ());
	ctx.stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::prioritized, result.size ());
	ctx.stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::outdated, outdated);
	ctx.stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::pending, pending);

	ctx.logger.debug (nano::log::type::bootstrap, "Processed {} frontiers of which outdated: {}, pending: {}", frontiers.size (), outdated, pending);

	nano::unique_lock<nano::mutex> lock{ ctx.mutex };

	for (auto const & account : result)
	{
		// Use the lowest possible priority here
		ctx.accounts.priority_set (account, account_sets_index::priority_cutoff);
	}

	lock.unlock ();

	if (!result.empty ())
	{
		ctx.condition.notify_all ();
	}
}
}
