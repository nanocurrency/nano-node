#include <nano/lib/blocks.hpp>
#include <nano/lib/container_info.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/saturate.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/bootstrap/topo_strategy.hpp>
#include <nano/node/bootstrap/verify.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/ledger/topology.hpp>
#include <nano/store/ledger_store.hpp>

#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{
// Per-pool cap on pre-check pages in flight; the scan loop back-pressures each head class on this so pages are never dropped
constexpr std::size_t max_precheck_tasks = 64;
// Maximum blocks released to the block processor per submit iteration
constexpr std::size_t max_submit_batch = 256;
}

namespace nano::bootstrap
{
topo_strategy::topo_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a },
	scan{ ctx.config.topo_scan, ctx.stats },
	blocks{ ctx.config.topo_scan, ctx.stats, ctx.logger },
	gaps{ ctx.config.topo_scan, ctx.stats },
	spearhead_workers{ 1, nano::thread_role::name::bootstrap_topo_processing },
	repair_workers{ 1, nano::thread_role::name::bootstrap_topo_processing }
{
	// Retire completed pages straight into the pre-check pipeline
	scan.sink = [this] (topo_scan::page page) {
		ctx.stats.inc (nano::stat::type::bootstrap_topo, nano::stat::detail::retire);
		post_precheck (std::move (page));
	};
}

void topo_strategy::start ()
{
	debug_assert (!scan_thread.joinable ());

	spearhead_workers.start ();
	repair_workers.start ();

	scan_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_topo_scan);
		run_scan ();
	});
	fetch_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_topo_fetch);
		run_fetch ();
	});
	submit_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_topo_submit);
		run_submit ();
	});
}

void topo_strategy::stop ()
{
	join_or_pass (scan_thread);
	join_or_pass (fetch_thread);
	join_or_pass (submit_thread);
	spearhead_workers.stop ();
	repair_workers.stop ();
}

void topo_strategy::orient ()
{
	std::optional<nano::topo_key> latest;
	{
		auto transaction = ctx.ledger.tx_begin_read ();
		latest = ctx.ledger.store.topology.latest (transaction);
	}

	// Genesis sits at topo_height 1, so only anchor the spearhead to our tip when the ledger holds more than genesis.
	// On a fresh (genesis-only) ledger there is nothing to skip, but epoch open blocks can also sit at
	// topo_height 1 and sort before genesis by hash, so we leave the spearhead at true zero to discover them.
	if (latest && latest->topo_height > 1)
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };
		scan.orient (latest.value_or (nano::topo_key{}));
	}
}

void topo_strategy::reset ()
{
	scan.reset ();
	blocks.reset ();
	gaps.reset ();
	consecutive_redundant_spearhead_pages = 0;
}

void topo_strategy::maintenance ()
{
	debug_assert (!ctx.mutex.try_lock ());

	gaps.evict (); // Drop stale gaps so a permanently-missing dependency can't wedge the spearhead
	scan.reconcile_heads (); // Grow the repair head count as the spearhead pushes the frontier higher

	// Periodically warn while discovery is stalled because the peer pool can't supply a scan's redundancy floor
	if (scan.starved () && starvation_warning_interval.elapse (5min))
	{
		ctx.logger.warn (nano::log::type::bootstrap, "Topology bootstrap: not enough topo-capable peers to reach the scan redundancy floor; discovery is stalled");
	}
}

/*
 * Scan
 */

void topo_strategy::run_scan ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_topo_scan);
		scan_one ();
		lock.lock ();
	}
}

void topo_strategy::scan_one ()
{
	// Back-pressure: pause discovery while the fetch buffer is full
	ctx.wait ([this] () {
		return blocks.total_count () < ctx.config.topo_scan.max_buffered;
	});

	// Back-pressure each head class on its own pre-check pool (spearhead also on the gap backlog) so a saturated
	// pool gates only its class; next () never drops a page, which would strand its blocks out of the buffer.
	std::optional<topo_scan::request> req;
	ctx.wait ([this, &req] () {
		topo_scan::head_gates gates{
			.include_spearhead = gaps.count () < ctx.config.topo_scan.max_gaps && spearhead_workers.queued_tasks () < max_precheck_tasks,
			.include_repair = repair_workers.queued_tasks () < max_precheck_tasks,
		};
		req = scan.next (gates);
		return req.has_value ();
	});
	if (!req)
	{
		return;
	}

	// Acquire up to `fanout` distinct peers (topo index requests need the capability); the cross-round
	// `exclude` keeps a top-up from re-sampling peers already used for this cursor
	auto acquired = ctx.wait_channels (strategy::topology, { nano::node_capabilities::topo_index }, req->exclude, req->fanout);

	std::vector<std::pair<std::shared_ptr<nano::transport::channel>, id_t>> sends;
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };

		for (auto const & lease : acquired.leases)
		{
			auto const id = nano::bootstrap::generate_id ();

			bool dispatched = scan.dispatch (req->head, req->start, id, lease.node_id);
			if (dispatched)
			{
				sends.emplace_back (lease.channel, id);
			}
			else
			{
				ctx.stats.inc (nano::stat::type::bootstrap_topo, nano::stat::detail::stale);
			}
		}

		// An exhausted round may complete the spearhead immediately; the sink retires whatever it produces
		if (acquired.exhausted)
		{
			scan.exhausted (req->head, req->start);
			ctx.stats.inc (nano::stat::type::bootstrap_topo, nano::stat::detail::exhausted);
		}
	}

	topo_index_query query{};
	query.start = req->start;
	query.count = req->count;

	for (auto const & [channel, id] : sends)
	{
		ctx.send (channel, query, strategy::topology, id);
	}
}

/*
 * Fetch
 */

void topo_strategy::run_fetch ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_topo_fetch);
		fetch_one ();
		lock.lock ();
	}
}

void topo_strategy::fetch_one ()
{
	std::optional<topo_blocks::request> req;
	ctx.wait ([this, &req] () {
		req = blocks.next (ctx.config.topo_scan.fetch_batch);
		return req.has_value ();
	});
	if (!req)
	{
		return;
	}

	// Random block pulls are supported from protocol version 0x16 onward
	auto acquired = ctx.wait_channels (strategy::topology, { .minimum_protocol_version = ctx.network_constants.topo_bootstrap_protocol_version_min }, req->exclude, 1);

	std::vector<std::pair<std::shared_ptr<nano::transport::channel>, id_t>> sends;
	{
		nano::lock_guard<nano::mutex> lock{ ctx.mutex };

		for (auto const & lease : acquired.leases)
		{
			auto const id = nano::bootstrap::generate_id ();

			if (blocks.dispatch (*req, id, lease.node_id))
			{
				sends.emplace_back (lease.channel, id);
			}
			else
			{
				ctx.stats.inc (nano::stat::type::bootstrap_topo, nano::stat::detail::stale);
			}
		}

		if (acquired.exhausted)
		{
			blocks.exhausted (*req);
			ctx.stats.inc (nano::stat::type::bootstrap_topo, nano::stat::detail::exhausted);
		}
	}

	blocks_random_query query{};
	query.hashes = std::move (req->hashes);

	for (auto const & [channel, id] : sends)
	{
		ctx.send (channel, query, strategy::topology, id);
	}
}

/*
 * Submit
 */

void topo_strategy::run_submit ()
{
	nano::unique_lock<nano::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_topo_submit);
		submit_one ();
		lock.lock ();
	}
}

void topo_strategy::submit_one ()
{
	std::deque<std::shared_ptr<nano::block>> batch;
	ctx.wait ([this, &batch] () {
		batch = blocks.next_submit (max_submit_batch);
		return !batch.empty ();
	});
	if (batch.empty ())
	{
		return;
	}

	// Respect the topology strategy's own block processor fair-queue bucket before releasing
	ctx.wait_block_processor (strategy::topology);

	ctx.stats.add (nano::stat::type::bootstrap_topo, nano::stat::detail::submitted, batch.size ());

	auto const result = ctx.block_processor.add_many (batch, nano::block_source::bootstrap, ctx.submission_channel (strategy::topology), {}, strategy::topology);
	if (result.dropped > 0)
	{
		ctx.logger.warn (nano::log::type::bootstrap, "Topology bootstrap block processor rejected {} fetched blocks (accepted {} of {})", result.dropped, result.added, batch.size ());
	}
}

/*
 * Response handling
 */

bool topo_strategy::process (nano::messages::asc_pull_ack::topo_index_payload const & response, async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (tag.type () == query_type::topo_index);

	release_assert (std::holds_alternative<topo_index_query> (tag.query));
	auto const & query = std::get<topo_index_query> (tag.query);

	auto const result = verify (response, query);

	if (result == verify_result::invalid)
	{
		ctx.stats.inc (nano::stat::type::bootstrap_verify_topo, nano::stat::detail::invalid);
		scan.cancel (tag.id); // the sink retires any page this completes
		return false;
	}

	ctx.stats.inc (nano::stat::type::bootstrap_verify_topo, result == verify_result::ok ? nano::stat::detail::ok : nano::stat::detail::nothing_new);
	ctx.stats.add (nano::stat::type::bootstrap_topo, nano::stat::detail::topo_indexes, response.entries.size ());

	scan.process (tag.id, response.entries); // the sink retires any page this completes

	return true;
}

bool topo_strategy::process (nano::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (tag.type () == query_type::blocks_random);

	release_assert (std::holds_alternative<blocks_random_query> (tag.query));
	auto const & query = std::get<blocks_random_query> (tag.query);

	auto result = verify (response, query);
	switch (result)
	{
		case verify_result::ok:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_verify_topo, nano::stat::detail::ok);
			ctx.stats.add (nano::stat::type::bootstrap_topo, nano::stat::detail::fetched, response.blocks.size ());
			blocks.process (tag.id, response.blocks);
		}
		break;
		case verify_result::nothing_new:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_verify_topo, nano::stat::detail::nothing_new);
			// No blocks returned; the requested entries stay pending and will be retried after cooldown
			blocks.process (tag.id, {});
		}
		break;
		case verify_result::invalid:
		{
			ctx.stats.inc (nano::stat::type::bootstrap_verify_topo, nano::stat::detail::invalid);
			blocks.process (tag.id, {});
		}
		break;
	}

	return result != verify_result::invalid;
}

void topo_strategy::timeout (async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());
	switch (tag.type ())
	{
		case query_type::topo_index:
		{
			scan.cancel (tag.id); // the sink retires any page this completes
		}
		break;
		case query_type::blocks_random:
		{
			blocks.cancel (tag.id);
		}
		break;
		default:
			break;
	}
}

void topo_strategy::failure (async_tag const & tag)
{
	timeout (tag);
}

/*
 * Pre-check (runs on the worker pool, off the message thread)
 */

void topo_strategy::post_precheck (topo_scan::page page)
{
	if (page.entries.empty ())
	{
		return;
	}

	// Record the number of distinct replies the spearhead advanced on, for observability
	if (page.head == 0)
	{
		ctx.stats.sample (nano::stat::sample::bootstrap_topo_redundancy, page.redundancy, { 0, ctx.config.topo_scan.consideration_count });
	}

	// Never drop: the scan loop back-pressures on this same queue, so it can't run away.
	// Dropping a page here would strand its blocks out of the buffer and release later topo blocks ahead of them (a false gap).
	auto & pool = (page.head == 0) ? spearhead_workers : repair_workers;
	pool.post ([this, head = page.head, entries = std::move (page.entries)] () mutable {
		precheck (head, std::move (entries));
	});
}

void topo_strategy::precheck (head_index head, std::deque<nano::topo_key> entries)
{
	debug_assert (!entries.empty ());

	std::deque<nano::topo_key> missing;
	{
		auto transaction = ctx.ledger.tx_begin_read ();

		if (ctx.ledger.flags.topo_index)
		{
			// With the topo index the table holds a key per block (and pruning is disabled), so a present key implies the block is present.
			// Entries arrive in topo order, matching the table, so crawl it in lockstep rather than a random block lookup per entry.
			auto crawler = ctx.ledger.store.topology.crawl (transaction, entries.front ());
			for (auto const & key : entries)
			{
				if (!crawler.skip_to (key) || crawler.full_key () != key)
				{
					missing.push_back (key);
				}
			}
		}
		else
		{
			// No topo index: fall back to probing each block by hash
			for (auto const & key : entries)
			{
				if (!ctx.ledger.any.block_exists_or_pruned (transaction, key.hash))
				{
					missing.push_back (key);
				}
			}
		}
	}

	ctx.stats.add (nano::stat::type::bootstrap_topo, nano::stat::detail::prechecked, entries.size ());

	// The spearhead (head 0) turns up new tip blocks; repair heads turn up gaps in the already-swept range
	ctx.stats.add (nano::stat::type::bootstrap_topo, (head == 0) ? nano::stat::detail::redundant : nano::stat::detail::rescanned, entries.size () - missing.size ());
	ctx.stats.add (nano::stat::type::bootstrap_topo, (head == 0) ? nano::stat::detail::missing : nano::stat::detail::gap, missing.size ());

	if (!missing.empty ())
	{
		{
			nano::lock_guard<nano::mutex> lock{ ctx.mutex };
			if (head == 0)
			{
				// A missing spearhead page breaks the redundant-page streak
				consecutive_redundant_spearhead_pages = 0;
			}
			blocks.add (missing);
		}
		ctx.condition.notify_all (); // Wake the fetch loop
	}
	else if (head == 0) // Only the spearhead is eligible for fast-forwarding
	{
		bool skipped = false;
		{
			nano::lock_guard<nano::mutex> lock{ ctx.mutex };

			// Consecutive fully redundant spearhead pages indicate a resumed scan can fast-forward
			++consecutive_redundant_spearhead_pages;

			if (ctx.config.topo_scan.redundant_skip_threshold > 0 && ctx.config.topo_scan.redundant_skip_stride > 0 && consecutive_redundant_spearhead_pages >= ctx.config.topo_scan.redundant_skip_threshold)
			{
				auto const target_height = nano::add_sat (entries.back ().topo_height, ctx.config.topo_scan.redundant_skip_stride);
				scan.orient (nano::topo_key{ target_height, nano::block_hash{} });

				consecutive_redundant_spearhead_pages = 0;

				ctx.stats.inc (nano::stat::type::bootstrap_topo, nano::stat::detail::skip);

				skipped = true;
			}
		}
		if (skipped)
		{
			ctx.condition.notify_all (); // Wake the scan loop
		}
	}
}

/*
 * Block-processor feedback
 */

void topo_strategy::inspect (nano::block_status result, nano::block const & block, strategy tag)
{
	debug_assert (!ctx.mutex.try_lock ());

	// Any source: the block is in the ledger now, so the gap we tracked for it (if any) is resolved
	if (result == nano::block_status::progress)
	{
		gaps.resolve (block.hash ());
		return;
	}

	// Only our own submissions count toward the spearhead back-pressure
	if (tag != strategy::topology)
	{
		return;
	}

	auto const account = block.account_field ().value_or (0);

	// Log the gap type for observability
	switch (result)
	{
		case nano::block_status::gap_previous:
		{
			ctx.logger.debug (nano::log::type::bootstrap, "Topo gap (previous): block {} account {} missing previous {}", block.hash (), account, block.previous ());
		}
		break;
		case nano::block_status::gap_source:
		{
			ctx.logger.debug (nano::log::type::bootstrap, "Topo gap (source): block {} account {} missing source {}", block.hash (), account, block.source_field ().value_or (block.link_field ().value_or (0).as_block_hash ()));
		}
		break;
		default:
			return; // Not a gap we track
	}

	gaps.track (block.hash (), account);
}

void topo_strategy::rollback (nano::account const & account)
{
	debug_assert (!ctx.mutex.try_lock ());
	gaps.rollback (account);
}

/*
 *
 */

nano::container_info topo_strategy::container_info () const
{
	nano::container_info info;
	info.add ("scan", scan.container_info ());
	info.add ("blocks", blocks.container_info ());
	info.add ("gaps", gaps.container_info ());
	info.put ("spearhead_redundant_pages", consecutive_redundant_spearhead_pages);
	info.add ("spearhead_workers", spearhead_workers.container_info ());
	info.add ("repair_workers", repair_workers.container_info ());
	return info;
}
}
