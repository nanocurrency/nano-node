#include <nano/lib/blocks.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/node.hpp>
#include <nano/test_common/ledger_context.hpp>

nano::test::ledger_context::ledger_context (std::deque<std::shared_ptr<nano::block>> && blocks) :
	store_m{ nano::make_store (logger_m, nano::unique_path (), nano::dev::constants) },
	stats_m{ logger_m },
	ledger_m{ *store_m, stats_m, nano::dev::constants },
	blocks_m{ blocks },
	pool_m{ nano::dev::network_params.network, 1 }
{
	debug_assert (!store_m->init_error ());
	auto tx = ledger_m.tx_begin_write ();
	store_m->initialize (tx, ledger_m.cache, ledger_m.constants);
	for (auto const & i : blocks_m)
	{
		auto process_result = ledger_m.process (tx, i);
		debug_assert (process_result == nano::block_status::progress, to_string (process_result));
	}
}

nano::ledger & nano::test::ledger_context::ledger ()
{
	return ledger_m;
}

nano::store::component & nano::test::ledger_context::store ()
{
	return *store_m;
}

nano::stats & nano::test::ledger_context::stats ()
{
	return stats_m;
}

nano::logger & nano::test::ledger_context::logger ()
{
	return logger_m;
}

std::deque<std::shared_ptr<nano::block>> const & nano::test::ledger_context::blocks () const
{
	return blocks_m;
}

nano::work_pool & nano::test::ledger_context::pool ()
{
	return pool_m;
}

/*
 * Ledger facotries
 */

auto nano::test::ledger_empty () -> ledger_context
{
	return ledger_context{};
}

auto nano::test::ledger_send_receive () -> ledger_context
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
				.build ();
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
				   .build ();
	blocks.push_back (receive);
	return ledger_context{ std::move (blocks) };
}

auto nano::test::ledger_send_receive_legacy () -> ledger_context
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
				.build ();
	blocks.push_back (send);
	auto receive = builder.receive ()
				   .make_block ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build ();
	blocks.push_back (receive);
	return ledger_context{ std::move (blocks) };
}

auto nano::test::ledger_diamond (unsigned height) -> ledger_context
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	using account_block_pair = std::pair<nano::keypair, std::shared_ptr<nano::block>>;
	std::deque<account_block_pair> previous;
	previous.push_back ({ nano::dev::genesis_key, nano::dev::genesis });

	// Expanding tree
	for (unsigned level = 0; level < height; ++level)
	{
		std::deque<account_block_pair> current;
		while (!previous.empty ())
		{
			auto const [key, root] = previous.front ();
			previous.pop_front ();

			auto balance = root->balance_field ().value_or (nano::dev::constants.genesis_amount);

			nano::keypair target1, target2;
			nano::block_builder builder;

			auto send1 = builder.state ()
						 .make_block ()
						 .account (key.pub)
						 .previous (root->hash ())
						 .representative (nano::dev::genesis_key.pub)
						 .balance (balance.number () / 2)
						 .link (target1.pub)
						 .sign (key.prv, key.pub)
						 .work (*pool.generate (root->hash ()))
						 .build ();

			auto send2 = builder.state ()
						 .make_block ()
						 .account (key.pub)
						 .previous (send1->hash ())
						 .representative (nano::dev::genesis_key.pub)
						 .balance (0)
						 .link (target2.pub)
						 .sign (key.prv, key.pub)
						 .work (*pool.generate (send1->hash ()))
						 .build ();

			auto open1 = builder.state ()
						 .make_block ()
						 .account (target1.pub)
						 .previous (0)
						 .representative (nano::dev::genesis_key.pub)
						 .balance (balance.number () - balance.number () / 2)
						 .link (send1->hash ())
						 .sign (target1.prv, target1.pub)
						 .work (*pool.generate (target1.pub))
						 .build ();

			auto open2 = builder.state ()
						 .make_block ()
						 .account (target2.pub)
						 .previous (0)
						 .representative (nano::dev::genesis_key.pub)
						 .balance (balance.number () / 2)
						 .link (send2->hash ())
						 .sign (target2.prv, target2.pub)
						 .work (*pool.generate (target2.pub))
						 .build ();

			blocks.push_back (send1);
			blocks.push_back (send2);
			blocks.push_back (open1);
			blocks.push_back (open2);

			current.push_back ({ target1, open1 });
			current.push_back ({ target2, open2 });
		}
		previous.clear ();
		previous.swap (current);
	}

	// Contracting tree
	while (previous.size () > 1)
	{
		std::deque<account_block_pair> current;
		while (!previous.empty ())
		{
			auto const [key1, root1] = previous.front ();
			previous.pop_front ();
			auto const [key2, root2] = previous.front ();
			previous.pop_front ();

			nano::keypair target;
			nano::block_builder builder;

			auto balance1 = root1->balance_field ().value ().number ();
			auto balance2 = root2->balance_field ().value ().number ();

			auto send1 = builder.state ()
						 .make_block ()
						 .account (key1.pub)
						 .previous (root1->hash ())
						 .representative (nano::dev::genesis_key.pub)
						 .balance (0)
						 .link (target.pub)
						 .sign (key1.prv, key1.pub)
						 .work (*pool.generate (root1->hash ()))
						 .build ();

			auto send2 = builder.state ()
						 .make_block ()
						 .account (key2.pub)
						 .previous (root2->hash ())
						 .representative (nano::dev::genesis_key.pub)
						 .balance (0)
						 .link (target.pub)
						 .sign (key2.prv, key2.pub)
						 .work (*pool.generate (root2->hash ()))
						 .build ();

			auto receive1 = builder.state ()
							.make_block ()
							.account (target.pub)
							.previous (0)
							.representative (nano::dev::genesis_key.pub)
							.balance (balance1)
							.link (send1->hash ())
							.sign (target.prv, target.pub)
							.work (*pool.generate (target.pub))
							.build ();

			auto receive2 = builder.state ()
							.make_block ()
							.account (target.pub)
							.previous (receive1->hash ())
							.representative (nano::dev::genesis_key.pub)
							.balance (balance1 + balance2)
							.link (send2->hash ())
							.sign (target.prv, target.pub)
							.work (*pool.generate (receive1->hash ()))
							.build ();

			blocks.push_back (send1);
			blocks.push_back (send2);
			blocks.push_back (receive1);
			blocks.push_back (receive2);

			current.push_back ({ target, receive2 });
		}
		previous.clear ();
		previous.swap (current);
	}

	return ledger_context{ std::move (blocks) };
}

auto nano::test::ledger_single_chain (unsigned height) -> nano::test::ledger_context
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	nano::block_builder builder;
	auto previous = nano::dev::genesis;
	for (unsigned i = 0; i < height / 4; ++i)
	{
		auto send1 = builder.state ()
					 .make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (previous->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 1)
					 .link (nano::dev::genesis_key.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*pool.generate (previous->hash ()))
					 .build ();

		auto send2 = builder.state ()
					 .make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send1->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 2)
					 .link (nano::dev::genesis_key.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*pool.generate (send1->hash ()))
					 .build ();

		auto receive1 = builder.state ()
						.make_block ()
						.account (nano::dev::genesis_key.pub)
						.previous (send2->hash ())
						.representative (nano::dev::genesis_key.pub)
						.balance (nano::dev::constants.genesis_amount - 1)
						.link (send1->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*pool.generate (send2->hash ()))
						.build ();

		auto receive2 = builder.state ()
						.make_block ()
						.account (nano::dev::genesis_key.pub)
						.previous (receive1->hash ())
						.representative (nano::dev::genesis_key.pub)
						.balance (nano::dev::constants.genesis_amount)
						.link (send2->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*pool.generate (receive1->hash ()))
						.build ();

		blocks.push_back (send1);
		blocks.push_back (send2);
		blocks.push_back (receive1);
		blocks.push_back (receive2);

		previous = receive2;
	}

	return ledger_context{ std::move (blocks) };
}