#include <nano/node/node.hpp>
#include <nano/test_common/ledger.hpp>

nano::test::context::ledger_context::ledger_context (std::deque<std::shared_ptr<nano::block>> && blocks) :
	store_m{ nano::make_store (logger, nano::unique_path (), nano::dev::constants) },
	ledger_m{ *store_m, stats_m, nano::dev::constants },
	blocks_m{ blocks }
{
	debug_assert (!store_m->init_error ());
	auto tx = store_m->tx_begin_write ();
	store_m->initialize (tx, ledger_m.cache, ledger_m.constants);
	for (auto const & i : blocks_m)
	{
		auto process_result = ledger_m.process (tx, *i);
		debug_assert (process_result.code == nano::process_result::progress);
	}
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

std::deque<std::shared_ptr<nano::block>> const & nano::test::context::ledger_context::blocks () const
{
	return blocks_m;
}

auto nano::test::context::ledger_empty () -> ledger_context
{
	return ledger_context{};
}

auto nano::test::context::ledger_send_receive () -> ledger_context
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	auto send = builder.state ()
				.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.link (nano::dev::genesis_key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build_shared ();
	blocks.push_back (send);
	auto receive = builder.state ()
				   .make_block ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (send->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount)
				   .link (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build_shared ();
	blocks.push_back (receive);
	return ledger_context{ std::move (blocks) };
}

auto nano::test::context::ledger_send_receive_legacy () -> ledger_context
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	auto send = builder.send ()
				.make_block ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build_shared ();
	blocks.push_back (send);
	auto receive = builder.receive ()
				   .make_block ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build_shared ();
	blocks.push_back (receive);
	return ledger_context{ std::move (blocks) };
}
