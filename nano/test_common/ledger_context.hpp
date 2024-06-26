#pragma once

#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/work.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/fwd.hpp>

namespace nano::test
{
class ledger_context
{
public:
	/** 'blocks' initialises the ledger with each block in-order
		Blocks must all return process_result::progress when processed */
	ledger_context (std::deque<std::shared_ptr<nano::block>> && blocks = std::deque<std::shared_ptr<nano::block>>{});
	nano::ledger & ledger ();
	nano::store::component & store ();
	nano::stats & stats ();
	std::deque<std::shared_ptr<nano::block>> const & blocks () const;
	nano::work_pool & pool ();

private:
	nano::logger logger;
	std::unique_ptr<nano::store::component> store_m;
	nano::stats stats_m;
	nano::ledger ledger_m;
	std::deque<std::shared_ptr<nano::block>> blocks_m;
	nano::work_pool pool_m;
};

/** Only a genesis block */
ledger_context ledger_empty ();
/** Send/receive pair of state blocks on the genesis account */
ledger_context ledger_send_receive ();
/** Send/receive pair of legacy blocks on the genesis account */
ledger_context ledger_send_receive_legacy ();
/** Full binary tree of state blocks */
ledger_context ledger_diamond (unsigned height);
/** Single chain of state blocks with send and receives to itself */
ledger_context ledger_single_chain (unsigned height);
}
