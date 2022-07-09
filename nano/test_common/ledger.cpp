#include <nano/node/node.hpp>
#include <nano/test_common/ledger.hpp>

nano::test::context::ledger_context::ledger_context () :
	store_m{ nano::make_store (logger, nano::unique_path (), nano::dev::constants) },
	ledger_m{ *store_m, stats_m, nano::dev::constants }
{
	debug_assert (!store_m->init_error ());
	store_m->initialize (store_m->tx_begin_write (), ledger_m.cache, ledger_m.constants);
}

nano::ledger & nano::test::context::ledger_context::ledger ()
{
	return ledger_m;
}

nano::store & nano::test::context::ledger_context::store ()
{
	return *store_m;
}

nano::stat & nano::test::context::ledger_context::stats ()
{
	return stats_m;
}

auto nano::test::context::ledger_empty () -> ledger_context
{
	return ledger_context{};
}
