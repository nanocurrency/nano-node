#pragma once

#include <nano/messages/fwd.hpp>
#include <nano/node/bootstrap/bootstrap_context.hpp>
#include <nano/node/bootstrap/ondemand_index.hpp>
#include <nano/secure/fwd.hpp>

#include <memory>
#include <thread>

namespace nano::bootstrap
{
/*
 * On-demand bootstrap strategy.
 *
 * Fetches single blocks that are known to exist but are missing from the ledger, each by its hash and from one peer at a time.
 * Owns one driver thread and drives the ondemand_index engine, which decides when a block is due for a request.
 */
class ondemand_strategy
{
public:
	explicit ondemand_strategy (bootstrap_context &);

	void start ();
	void stop ();

	void reset ();

	void maintenance ();

	nano::container_info container_info () const;

	// Makes a block wanted for the given reason, does nothing while the strategy is disabled
	void demand (nano::block_hash const &, ondemand_index::reason);

	// Response/conclusion hooks
	bool process (nano::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
	void timeout (async_tag const & tag);
	void failure (async_tag const & tag);

	// Block-processor feedback
	void inspect (nano::block_status result, nano::block const & block, nano::bootstrap::strategy submitter);

private:
	void run ();
	void run_one ();

	// Whether the strategy runs at all
	bool enabled () const;

	// Whether the block is in the ledger, pruned ones included
	bool in_ledger (nano::block_hash const &) const;

	// Hands a fetched block to the block processor
	void submit (std::shared_ptr<nano::block> const &);

	// Counts what became of a block whose request or fetched copy failed
	void failed (nano::block_hash const &, ondemand_index::fail_result);

private:
	bootstrap_context & ctx;

	ondemand_index index;

	std::thread thread;
};
}
