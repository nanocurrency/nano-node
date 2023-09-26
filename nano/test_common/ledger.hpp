#pragma once

#include <nano/lib/logger_mt.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/ledger.hpp>

namespace nano
{
namespace store
{
	class component;
}
namespace test
{
	namespace context
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

		private:
			nano::logger_mt logger;
			std::unique_ptr<nano::store::component> store_m;
			nano::stats stats_m;
			nano::ledger ledger_m;
			std::deque<std::shared_ptr<nano::block>> blocks_m;
		};

		/** Only a genesis block */
		ledger_context ledger_empty ();
		/** Send/receive pair of state blocks on the genesis account*/
		ledger_context ledger_send_receive ();
		/** Send/receive pair of legacy blocks on the genesis account*/
		ledger_context ledger_send_receive_legacy ();
	}
}
}
