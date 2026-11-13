#include <nano/lib/blocks.hpp>
#include <nano/lib/container_info.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bootstrap/ondemand_strategy.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/bootstrap/verify.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>

#include <optional>

namespace nano::bootstrap
{
ondemand_strategy::ondemand_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a },
	index{ ctx.config.ondemand }
{
}

void ondemand_strategy::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_ondemand);
		run ();
	});
}

void ondemand_strategy::stop ()
{
	nano::join_or_pass (thread);
}

void ondemand_strategy::reset ()
{
	debug_assert (!ctx.mutex.try_lock ());

	index.clear ();
}

void ondemand_strategy::maintenance ()
{
	debug_assert (!ctx.mutex.try_lock ());

	auto const expired = index.expire (std::chrono::steady_clock::now ());
	ctx.stats.add (nano::stat::type::bootstrap_ondemand, nano::stat::detail::expired, expired);
}

void ondemand_strategy::demand (nano::block_hash const & hash, ondemand_index::reason reason)
{
	debug_assert (!ctx.mutex.try_lock ());

	if (!enabled ())
	{
		return;
	}

	auto const result = index.insert (hash, reason, std::chrono::steady_clock::now ());
	ctx.stats.inc (nano::stat::type::bootstrap_ondemand_insert, to_stat_detail (result));

	ctx.logger.debug (nano::log::type::bootstrap, "Block wanted: {} (reason: {}, result: {})", hash, nano::enum_to_string (reason), nano::enum_to_string (result));
}

bool ondemand_strategy::enabled () const
{
	// Without bootstrap as a whole nothing would request the tracked blocks or expire them
	if (!ctx.config.enable)
	{
		return false;
	}

	return ctx.config.enable_ondemand;
}

/*
 * Requests
 */

void ondemand_strategy::run ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_ondemand);
		run_one ();
		lock.lock ();
	}
}

void ondemand_strategy::run_one ()
{
	// Wait for a block that is due for a request, failing the requests that went unanswered in the meantime
	std::optional<ondemand_index::request> request;
	ctx.wait ([this, &request] () {
		auto const now = std::chrono::steady_clock::now ();
		for (auto const & [hash, result] : index.timeout (now))
		{
			ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::timeout);
			failed (hash, result);
		}
		request = index.next (ondemand_index::reason::dependency, now);
		return request.has_value ();
	});
	if (!request)
	{
		return;
	}

	// A block that reached the ledger since it became wanted needs no request
	if (in_ledger (request->hash))
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };
		index.erase (request->hash);
		ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::old);
		return;
	}

	// Neither does one that the node holds already, parked until a dependency of its own arrives
	if (ctx.unchecked.contains_block (request->hash))
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };
		index.hold (request->hash);
		ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::unchecked);
		return;
	}

	// Any peer will do, as long as it has not failed to deliver this block already
	auto const acquired = ctx.wait_channels (strategy::ondemand, {}, request->exclude, /* max */ 1);

	std::shared_ptr<nano::transport::channel> channel;
	auto const id = nano::bootstrap::generate_id ();
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };

		if (acquired.exhausted)
		{
			// Every peer has been tried, one of them may have received the block since
			index.exhausted (request->hash);
			ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::exhausted);
		}

		for (auto const & lease : acquired.leases)
		{
			if (index.dispatch (request->hash, id, lease.node_id, std::chrono::steady_clock::now ()))
			{
				channel = lease.channel;
			}
			else
			{
				// The block stopped waiting for a request while a peer was being found
				ctx.peers.release (lease.channel);
				ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::stale);
			}
		}
	}
	if (!channel)
	{
		return;
	}

	ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request);

	single_block_query query{ .hash = request->hash };
	ctx.send (channel, query, strategy::ondemand, id);
}

bool ondemand_strategy::in_ledger (nano::block_hash const & hash) const
{
	auto transaction = ctx.ledger.tx_begin_read ();
	return ctx.ledger.any.block_exists_or_pruned (transaction, hash);
}

/*
 * Responses
 */

bool ondemand_strategy::process (nano::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (tag.type () == query_type::single_block);

	release_assert (std::holds_alternative<single_block_query> (tag.query));
	auto const & query = std::get<single_block_query> (tag.query);

	auto const result = verify (response, query);
	switch (result)
	{
		case verify_result::ok:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched);

			if (index.submit (query.hash))
			{
				ctx.logger.debug (nano::log::type::bootstrap, "Fetched wanted block: {}", query.hash);

				// The block processor is not to be called with the bootstrap mutex held
				ctx.workers.post ([this, block = response.blocks.front ()] () {
					submit (block);
				});
			}
			else
			{
				// The block arrived some other way while this copy was on its way
				ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::redundant);
			}
		}
		break;
		case verify_result::nothing_new:
		{
			// The peer does not hold the block
			ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::empty);
			failed (query.hash, index.fail (query.hash, tag.id, std::chrono::steady_clock::now ()));
		}
		break;
		case verify_result::invalid:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::invalid);
			failed (query.hash, index.fail (query.hash, tag.id, std::chrono::steady_clock::now ()));
		}
		break;
	}

	return result != verify_result::invalid;
}

void ondemand_strategy::timeout (async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());

	release_assert (std::holds_alternative<single_block_query> (tag.query));
	auto const & query = std::get<single_block_query> (tag.query);

	// Usually stale by now, the index gives up on a request long before its tag expires
	failed (query.hash, index.fail (query.hash, tag.id, std::chrono::steady_clock::now ()));
}

void ondemand_strategy::failure (async_tag const & tag)
{
	timeout (tag);
}

void ondemand_strategy::submit (std::shared_ptr<nano::block> const & block)
{
	auto const added = ctx.block_processor.add (block, nano::block_source::bootstrap, ctx.submission_channel (strategy::ondemand), /* callback */ nullptr, strategy::ondemand);
	if (added)
	{
		return;
	}

	// The block processor counted why it refused the copy, here it is as good as never delivered
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };
		failed (block->hash (), index.reject (block->hash (), std::chrono::steady_clock::now ()));
	}
	ctx.condition.notify_all ();
}

void ondemand_strategy::failed (nano::block_hash const & hash, ondemand_index::fail_result result)
{
	debug_assert (!ctx.mutex.try_lock ());

	ctx.stats.inc (nano::stat::type::bootstrap_ondemand_fail, to_stat_detail (result));

	if (result == ondemand_index::fail_result::abandoned)
	{
		ctx.logger.debug (nano::log::type::bootstrap, "Giving up on block: {} after {} requests", hash, ctx.config.ondemand.max_attempts);
	}
}

/*
 * Block-processor feedback
 */

void ondemand_strategy::inspect (nano::block_status result, nano::block const & block, strategy submitter)
{
	debug_assert (!ctx.mutex.try_lock ());

	auto const hash = block.hash ();
	switch (result)
	{
		case nano::block_status::progress:
		case nano::block_status::old:
		{
			// The block is in the ledger, whoever delivered it
			if (auto const arrival = index.erase (hash))
			{
				ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::stat::detail::resolved);
				ctx.stats.sample (nano::stat::sample::bootstrap_ondemand_duration, nano::log::milliseconds_delta (*arrival), { 0, std::chrono::milliseconds{ ctx.config.ondemand.max_age }.count () });
			}
		}
		break;
		case nano::block_status::fork:
		case nano::block_status::gap_previous:
		case nano::block_status::gap_source:
		{
			// The node holds the block but cannot apply it for now, whoever delivered it
			if (index.hold (hash))
			{
				ctx.stats.inc (nano::stat::type::bootstrap_ondemand, nano::to_stat_detail (result));
				ctx.logger.debug (nano::log::type::bootstrap, "Holding wanted block: {} ({})", hash, nano::to_string (result));
			}
		}
		break;
		default:
		{
			// A refused copy only counts against the peer that served it when it was fetched here
			if (submitter == strategy::ondemand)
			{
				failed (hash, index.reject (hash, std::chrono::steady_clock::now ()));
			}
		}
		break;
	}
}

/*
 *
 */

nano::container_info ondemand_strategy::container_info () const
{
	return index.container_info ();
}
}
