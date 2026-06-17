#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/node/bootstrap/bootstrap_context.hpp>
#include <nano/node/bootstrap/topo_blocks.hpp>
#include <nano/node/bootstrap/topo_gaps.hpp>
#include <nano/node/bootstrap/topo_scan.hpp>
#include <nano/secure/fwd.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <thread>

namespace nano::bootstrap
{
/*
 * Topology bootstrap strategy.
 *
 * Owns three driver threads and a background worker pool, and drives two engines:
 *  - topo_scan walks the peers' topo index across heads and retires pages to the worker pool for
 *    a ledger pre-check (already-present blocks are dropped, the rest become fetch candidates).
 *  - topo_blocks fetches the candidates via random-block requests and releases them to the block
 *    processor in topological order.
 */
class topo_strategy
{
public:
	explicit topo_strategy (bootstrap_context &);

	void start ();
	void stop ();

	void reset ();

	void maintenance ();

	nano::container_info container_info () const;

	// Response/conclusion hooks
	bool process (nano::messages::asc_pull_ack::topo_index_payload const & response, async_tag const & tag);
	bool process (nano::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
	void timeout (async_tag const & tag);
	void failure (async_tag const & tag);

	// Block-processor feedback
	void inspect (nano::block_status result, nano::block const & block, nano::bootstrap::strategy tag);
	void rollback (nano::account const & account);

private:
	// Driver loops (one thread each) and their single-iteration bodies
	void run_scan ();
	void run_fetch ();
	void run_submit ();
	void scan_one ();
	void fetch_one ();
	void submit_one ();

	// Re-anchor the spearhead to our local topology tip
	void orient ();

	// Route a retired page to its head class's pre-check pool (spearhead vs repair)
	void post_precheck (topo_scan::page page);

	// Worker-pool task: drop already-present blocks, hand the rest to the fetch engine
	void precheck (head_index head, std::deque<nano::topo_key> entries);

private:
	bootstrap_context & ctx;

	topo_scan scan;
	topo_blocks blocks;
	topo_gaps gaps;

	std::size_t consecutive_redundant_spearhead_pages{ 0 };

	std::thread scan_thread;
	std::thread fetch_thread;
	std::thread submit_thread;

	// Pre-check ledger reads run here, off the message thread
	nano::thread_pool spearhead_workers;
	nano::thread_pool repair_workers;

	// Rate-limits the "not enough peers" warning while discovery is starved
	nano::interval starvation_warning_interval;
};
}
