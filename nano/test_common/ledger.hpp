#pragma once

#include <nano/lib/logger_mt.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/ledger.hpp>

namespace nano
{
class store;
namespace test
{
	namespace context
	{
		class ledger_context
		{
		public:
			ledger_context ();
			nano::ledger & ledger ();
			nano::store & store ();
			nano::stat & stats ();

		private:
			nano::logger_mt logger;
			std::unique_ptr<nano::store> store_m;
			nano::stat stats_m;
			nano::ledger ledger_m;
		};

		ledger_context ledger_empty ();
	}
}
}
