#pragma once

#include <celerix/lib/logging.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/work.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/store/fwd.hpp>

namespace celerix::test
{
class ledger_context
{
public:
	/** 'blocks' initialises the ledger with each block in-order
		Blocks must all return process_result::progress when processed */
	ledger_context (std::deque<std::shared_ptr<celerix::block>> && blocks = std::deque<std::shared_ptr<celerix::block>>{});
	celerix::ledger & ledger ();
	celerix::store::component & store ();
	std::deque<std::shared_ptr<celerix::block>> const & blocks () const;
	celerix::work_pool & pool ();
	celerix::stats & stats ();
	celerix::logger & logger ();

private:
	celerix::logger logger_m;
	std::unique_ptr<celerix::store::component> store_m;
	celerix::stats stats_m;
	celerix::ledger ledger_m;
	std::deque<std::shared_ptr<celerix::block>> blocks_m;
	celerix::work_pool pool_m;
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
