#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/work.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/extended/account_block_by_height.hpp>
#include <nano/store/ledger/extended/account_delegator_by_weight.hpp>
#include <nano/store/ledger/extended/account_receivable_by_amount.hpp>
#include <nano/store/ledger/extended/receive_block_by_send_block.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/test_common/make_store.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace
{
struct ledger_chain
{
	nano::keypair key1;
	nano::keypair representative;
	nano::keypair state_key;
	nano::keypair state_representative;
	std::shared_ptr<nano::block> send1;
	std::shared_ptr<nano::block> open1;
	std::shared_ptr<nano::block> send2;
	std::shared_ptr<nano::block> receive1;
	std::shared_ptr<nano::block> change1;
	std::shared_ptr<nano::block> state_send1;
	std::shared_ptr<nano::block> state_open1;
	std::shared_ptr<nano::block> state_send2;
	std::shared_ptr<nano::block> state_receive1;
	std::shared_ptr<nano::block> state_change1;
};

nano::ledger_options extended_ledger_options ()
{
	return nano::ledger_options{ .enable_extended_ledger_index = true };
}

void disable_extended_index_flags (nano::store::ledger_store & store)
{
	auto txn = store.tx_begin_write ();
	store.version.put_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled, false);
	store.version.put_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled, false);
	store.version.put_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled, false);
	store.version.put_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled, false);
}

void expect_extended_flags (nano::ledger const & ledger, bool expected)
{
	ASSERT_EQ (expected, ledger.flags.receive_block_by_send_block_index);
	ASSERT_EQ (expected, ledger.flags.account_receivable_by_amount_index);
	ASSERT_EQ (expected, ledger.flags.account_delegator_by_weight_index);
	ASSERT_EQ (expected, ledger.flags.account_block_by_height_index);
}

void expect_persisted_extended_flags (nano::store::ledger_store & store, bool expected)
{
	auto txn = store.tx_begin_read ();
	ASSERT_EQ (expected, store.version.get_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled));
	ASSERT_EQ (expected, store.version.get_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled));
	ASSERT_EQ (expected, store.version.get_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled));
	ASSERT_EQ (expected, store.version.get_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled));
}

void expect_receive_block_by_send_block_entry (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::block_hash const & send_hash, nano::block_hash const & receive_hash)
{
	auto entry = store.receive_block_by_send_block.get (txn, send_hash);
	ASSERT_TRUE (entry.has_value ());
	ASSERT_EQ (receive_hash, entry.value ());
}

void expect_account_block_by_height_entry (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::account const & account, uint64_t height, nano::block_hash const & hash)
{
	auto entry = store.account_block_by_height.get (txn, { account, height });
	ASSERT_TRUE (entry.has_value ());
	ASSERT_EQ (hash, entry.value ());
}

void expect_account_receivable_by_amount_entry (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::account const & account, nano::amount const & amount, nano::block_hash const & send_hash, nano::account const & source, nano::epoch epoch)
{
	auto it = store.account_receivable_by_amount.begin (txn, { account, amount, send_hash });
	ASSERT_NE (store.account_receivable_by_amount.end (txn), it);
	ASSERT_EQ (account, it->first.account);
	ASSERT_EQ (amount, it->first.amount);
	ASSERT_EQ (send_hash, it->first.send_block_hash);
	ASSERT_EQ (source, it->second.source);
	ASSERT_EQ (epoch, it->second.epoch);
}

void expect_no_account_receivable_by_amount_entry (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::account const & account, nano::amount const & amount, nano::block_hash const & send_hash)
{
	auto const key = nano::account_receivable_by_amount_key{ account, amount, send_hash };
	auto const end = store.account_receivable_by_amount.end (txn);
	auto it = store.account_receivable_by_amount.begin (txn, key);
	ASSERT_TRUE (it == end || it->first != key);
}

void expect_account_delegator_by_weight_entry (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::account const & representative, nano::amount const & weight, nano::account const & delegator)
{
	auto it = store.account_delegator_by_weight.begin (txn, { representative, weight, delegator });
	ASSERT_NE (store.account_delegator_by_weight.end (txn), it);
	ASSERT_EQ (representative, it->first.representative);
	ASSERT_EQ (weight, it->first.weight);
	ASSERT_EQ (delegator, it->first.delegator);
}

void expect_no_account_delegator_by_weight_entry (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::account const & representative, nano::amount const & weight, nano::account const & delegator)
{
	auto const key = nano::account_delegator_by_weight_key{ representative, weight, delegator };
	auto const end = store.account_delegator_by_weight.end (txn);
	auto it = store.account_delegator_by_weight.begin (txn, key);
	ASSERT_TRUE (it == end || it->first != key);
}

void process_legacy_chain (nano::ledger & ledger, nano::work_pool & pool, ledger_chain & chain)
{
	nano::block_builder builder;
	auto txn = ledger.tx_begin_write ();
	auto genesis_info = ledger.any.account_get (txn, nano::dev::genesis_key.pub);
	ASSERT_TRUE (genesis_info);

	chain.send1 = builder.send ()
				  .previous (genesis_info->head)
				  .destination (chain.key1.pub)
				  .balance (nano::dev::constants.genesis_amount - 10)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (genesis_info->head))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.send1));

	chain.open1 = builder.open ()
				  .source (chain.send1->hash ())
				  .representative (chain.key1.pub)
				  .account (chain.key1.pub)
				  .sign (chain.key1.prv, chain.key1.pub)
				  .work (*pool.generate (chain.key1.pub))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.open1));

	chain.send2 = builder.send ()
				  .previous (chain.send1->hash ())
				  .destination (chain.key1.pub)
				  .balance (nano::dev::constants.genesis_amount - 30)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (chain.send1->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.send2));

	chain.receive1 = builder.receive ()
					 .previous (chain.open1->hash ())
					 .source (chain.send2->hash ())
					 .sign (chain.key1.prv, chain.key1.pub)
					 .work (*pool.generate (chain.open1->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.receive1));

	chain.change1 = builder.change ()
					.previous (chain.receive1->hash ())
					.representative (chain.representative.pub)
					.sign (chain.key1.prv, chain.key1.pub)
					.work (*pool.generate (chain.receive1->hash ()))
					.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.change1));
}

void process_state_chain (nano::ledger & ledger, nano::work_pool & pool, ledger_chain & chain)
{
	nano::block_builder builder;
	auto txn = ledger.tx_begin_write ();

	chain.state_send1 = builder.state ()
						.account (nano::dev::genesis_key.pub)
						.previous (chain.send2->hash ())
						.representative (nano::dev::genesis_key.pub)
						.balance (nano::dev::constants.genesis_amount - 40)
						.link (chain.state_key.pub)
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*pool.generate (chain.send2->hash ()))
						.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.state_send1));

	chain.state_open1 = builder.state ()
						.account (chain.state_key.pub)
						.previous (0)
						.representative (chain.state_key.pub)
						.balance (10)
						.link (chain.state_send1->hash ())
						.sign (chain.state_key.prv, chain.state_key.pub)
						.work (*pool.generate (chain.state_key.pub))
						.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.state_open1));

	chain.state_send2 = builder.state ()
						.account (nano::dev::genesis_key.pub)
						.previous (chain.state_send1->hash ())
						.representative (nano::dev::genesis_key.pub)
						.balance (nano::dev::constants.genesis_amount - 60)
						.link (chain.state_key.pub)
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*pool.generate (chain.state_send1->hash ()))
						.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.state_send2));

	chain.state_receive1 = builder.state ()
						   .account (chain.state_key.pub)
						   .previous (chain.state_open1->hash ())
						   .representative (chain.state_key.pub)
						   .balance (30)
						   .link (chain.state_send2->hash ())
						   .sign (chain.state_key.prv, chain.state_key.pub)
						   .work (*pool.generate (chain.state_open1->hash ()))
						   .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.state_receive1));

	chain.state_change1 = builder.state ()
						  .account (chain.state_key.pub)
						  .previous (chain.state_receive1->hash ())
						  .representative (chain.state_representative.pub)
						  .balance (30)
						  .link (0)
						  .sign (chain.state_key.prv, chain.state_key.pub)
						  .work (*pool.generate (chain.state_receive1->hash ()))
						  .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, chain.state_change1));
}

void process_legacy_and_state_chains (nano::ledger & ledger, nano::work_pool & pool, ledger_chain & chain)
{
	process_legacy_chain (ledger, pool, chain);
	process_state_chain (ledger, pool, chain);
}

void process_genesis_legacy_send (nano::ledger & ledger, nano::work_pool & pool, nano::block_hash const & previous, nano::account const & destination, nano::amount const & balance, std::shared_ptr<nano::block> & send)
{
	nano::block_builder builder;
	auto txn = ledger.tx_begin_write ();
	send = builder.send ()
		   .previous (previous)
		   .destination (destination)
		   .balance (balance)
		   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
		   .work (*pool.generate (previous))
		   .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send));
}

void process_genesis_state_send (nano::ledger & ledger, nano::work_pool & pool, nano::block_hash const & previous, nano::account const & destination, nano::amount const & balance, std::shared_ptr<nano::block> & send)
{
	nano::block_builder builder;
	auto txn = ledger.tx_begin_write ();
	send = builder.state ()
		   .account (nano::dev::genesis_key.pub)
		   .previous (previous)
		   .representative (nano::dev::genesis_key.pub)
		   .balance (balance)
		   .link (destination)
		   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
		   .work (*pool.generate (previous))
		   .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send));
}
}

/*
 * Populates the receive-block lookup index for an existing ledger whose
 * extended index flags are disabled. The test first builds legacy and state
 * receive/open blocks without index entries, then runs the single-index
 * populate method and checks that received sends map to their receive blocks
 * while an unreceived send does not get an entry.
 */
TEST (ledger_extended_index, receive_block_by_send_block_populate)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };

	ledger_chain chain;
	nano::keypair key2;
	std::shared_ptr<nano::block> unreceived_send;
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		ASSERT_FALSE (ledger.flags.receive_block_by_send_block_index);
		process_legacy_and_state_chains (ledger, pool, chain);
		process_genesis_state_send (ledger, pool, chain.state_send2->hash (), key2.pub, nano::dev::constants.genesis_amount - 70, unreceived_send);
	}

	{
		auto txn = store->tx_begin_read ();
		ASSERT_FALSE (store->receive_block_by_send_block.get (txn, chain.send1->hash ()).has_value ());
		ASSERT_FALSE (store->receive_block_by_send_block.get (txn, chain.send2->hash ()).has_value ());
		ASSERT_FALSE (store->receive_block_by_send_block.get (txn, chain.state_send1->hash ()).has_value ());
		ASSERT_FALSE (store->receive_block_by_send_block.get (txn, chain.state_send2->hash ()).has_value ());
		ASSERT_FALSE (store->receive_block_by_send_block.get (txn, unreceived_send->hash ()).has_value ());
	}

	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		ASSERT_FALSE (ledger.flags.receive_block_by_send_block_index);
		ledger.populate_receive_block_by_send_block_index ();
		ASSERT_TRUE (ledger.flags.receive_block_by_send_block_index);
	}

	auto txn = store->tx_begin_read ();
	expect_receive_block_by_send_block_entry (*store, txn, chain.send1->hash (), chain.open1->hash ());
	expect_receive_block_by_send_block_entry (*store, txn, chain.send2->hash (), chain.receive1->hash ());
	expect_receive_block_by_send_block_entry (*store, txn, chain.state_send1->hash (), chain.state_open1->hash ());
	expect_receive_block_by_send_block_entry (*store, txn, chain.state_send2->hash (), chain.state_receive1->hash ());
	ASSERT_FALSE (store->receive_block_by_send_block.get (txn, unreceived_send->hash ()).has_value ());
	ASSERT_FALSE (store->receive_block_by_send_block.get (txn, chain.change1->hash ()).has_value ());
	ASSERT_FALSE (store->receive_block_by_send_block.get (txn, chain.state_change1->hash ()).has_value ());
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled));
}

/*
 * Verifies runtime maintenance of the receive-block lookup index. With
 * extended indices enabled, a send alone must not create a lookup entry, a
 * following open block must add the send->open mapping, and rolling the open
 * back must remove that mapping through the normal ledger delete hook.
 */
TEST (ledger_extended_index, receive_block_by_send_block_runtime)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };

	nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
	ASSERT_TRUE (ledger.flags.receive_block_by_send_block_index);
	nano::keypair key;
	nano::block_builder builder;

	std::shared_ptr<nano::block> send;
	process_genesis_legacy_send (ledger, pool, nano::dev::genesis->hash (), key.pub, nano::dev::constants.genesis_amount - 10, send);
	{
		auto txn = ledger.tx_begin_read ();
		ASSERT_FALSE (store->receive_block_by_send_block.get (txn, send->hash ()).has_value ());
	}

	std::shared_ptr<nano::block> open;
	{
		auto txn = ledger.tx_begin_write ();
		open = builder.open ()
			   .source (send->hash ())
			   .representative (key.pub)
			   .account (key.pub)
			   .sign (key.prv, key.pub)
			   .work (*pool.generate (key.pub))
			   .build ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open));
		expect_receive_block_by_send_block_entry (*store, txn, send->hash (), open->hash ());

		ASSERT_FALSE (ledger.rollback (txn, open->hash ()));
		ASSERT_FALSE (store->receive_block_by_send_block.get (txn, send->hash ()).has_value ());
	}
}

/*
 * Populates the receivable amount index from pending entries that already
 * exist in the ledger. The setup creates multiple legacy sends plus a state
 * send while the index flag is disabled; populate then backfills keys by
 * destination account, amount, and send hash and stores source/epoch metadata.
 */
TEST (ledger_extended_index, account_receivable_by_amount_populate)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };

	nano::keypair key;
	nano::keypair state_key;
	std::shared_ptr<nano::block> send1;
	std::shared_ptr<nano::block> send2;
	std::shared_ptr<nano::block> send3;
	std::shared_ptr<nano::block> state_send;
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		process_genesis_legacy_send (ledger, pool, nano::dev::genesis->hash (), key.pub, nano::dev::constants.genesis_amount - 10, send1);
		process_genesis_legacy_send (ledger, pool, send1->hash (), key.pub, nano::dev::constants.genesis_amount - 30, send2);
		process_genesis_legacy_send (ledger, pool, send2->hash (), key.pub, nano::dev::constants.genesis_amount - 50, send3);
		process_genesis_state_send (ledger, pool, send3->hash (), state_key.pub, nano::dev::constants.genesis_amount - 80, state_send);
	}

	{
		auto txn = store->tx_begin_read ();
		ASSERT_EQ (0, store->account_receivable_by_amount.count (txn));
		ASSERT_EQ (4, store->pending.count (txn));
	}

	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		ASSERT_FALSE (ledger.flags.account_receivable_by_amount_index);
		ledger.populate_account_receivable_by_amount_index ();
		ASSERT_TRUE (ledger.flags.account_receivable_by_amount_index);
	}

	auto txn = store->tx_begin_read ();
	ASSERT_EQ (store->pending.count (txn), store->account_receivable_by_amount.count (txn));
	expect_account_receivable_by_amount_entry (*store, txn, key.pub, 10, send1->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
	expect_account_receivable_by_amount_entry (*store, txn, key.pub, 20, send2->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
	expect_account_receivable_by_amount_entry (*store, txn, key.pub, 20, send3->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
	expect_account_receivable_by_amount_entry (*store, txn, state_key.pub, 30, state_send->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled));
}

/*
 * Verifies runtime maintenance of the receivable amount index through normal
 * ledger processing. A send adds both pending and amount-index entries, an open
 * consumes and removes them, rolling the open back restores them, and rolling
 * the send back removes them again.
 */
TEST (ledger_extended_index, account_receivable_by_amount_runtime)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };
	nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
	nano::keypair key;
	nano::block_builder builder;

	std::shared_ptr<nano::block> send;
	process_genesis_legacy_send (ledger, pool, nano::dev::genesis->hash (), key.pub, nano::dev::constants.genesis_amount - 10, send);
	{
		auto txn = ledger.tx_begin_read ();
		expect_account_receivable_by_amount_entry (*store, txn, key.pub, 10, send->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
		ASSERT_TRUE (store->pending.exists (txn, { key.pub, send->hash () }));
	}

	std::shared_ptr<nano::block> open;
	{
		auto txn = ledger.tx_begin_write ();
		open = builder.open ()
			   .source (send->hash ())
			   .representative (key.pub)
			   .account (key.pub)
			   .sign (key.prv, key.pub)
			   .work (*pool.generate (key.pub))
			   .build ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open));
		expect_no_account_receivable_by_amount_entry (*store, txn, key.pub, 10, send->hash ());
		ASSERT_FALSE (store->pending.exists (txn, { key.pub, send->hash () }));

		ASSERT_FALSE (ledger.rollback (txn, open->hash ()));
		expect_account_receivable_by_amount_entry (*store, txn, key.pub, 10, send->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
		ASSERT_TRUE (store->pending.exists (txn, { key.pub, send->hash () }));

		ASSERT_FALSE (ledger.rollback (txn, send->hash ()));
		expect_no_account_receivable_by_amount_entry (*store, txn, key.pub, 10, send->hash ());
		ASSERT_FALSE (store->pending.exists (txn, { key.pub, send->hash () }));
	}
}

/*
 * Populates the delegator-by-weight index from existing account records. The
 * mixed legacy/state chain is processed while the index flag is disabled, then
 * populate backfills one row per account keyed by representative, balance, and
 * delegator account.
 */
TEST (ledger_extended_index, account_delegator_by_weight_populate)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };

	ledger_chain chain;
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		process_legacy_and_state_chains (ledger, pool, chain);
	}

	{
		auto txn = store->tx_begin_read ();
		ASSERT_EQ (0, store->account_delegator_by_weight.count (txn));
	}
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		ASSERT_FALSE (ledger.flags.account_delegator_by_weight_index);
		ledger.populate_account_delegator_by_weight_index ();
		ASSERT_TRUE (ledger.flags.account_delegator_by_weight_index);
	}

	auto txn = store->tx_begin_read ();
	ASSERT_EQ (store->account.count (txn), store->account_delegator_by_weight.count (txn));
	expect_account_delegator_by_weight_entry (*store, txn, nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount - 60, nano::dev::genesis_key.pub);
	expect_account_delegator_by_weight_entry (*store, txn, chain.representative.pub, 30, chain.key1.pub);
	expect_account_delegator_by_weight_entry (*store, txn, chain.state_representative.pub, 30, chain.state_key.pub);
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled));
}

/*
 * Verifies runtime updates of the delegator-by-weight index. Opening an account
 * inserts its initial representative/weight row, receiving more funds replaces
 * the old balance row, and changing representatives removes the old
 * representative row and inserts the new one.
 */
TEST (ledger_extended_index, account_delegator_by_weight_runtime)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };
	nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
	nano::keypair key;
	nano::keypair new_rep;
	nano::block_builder builder;

	std::shared_ptr<nano::block> send1;
	process_genesis_legacy_send (ledger, pool, nano::dev::genesis->hash (), key.pub, nano::dev::constants.genesis_amount - 10, send1);
	std::shared_ptr<nano::block> open;
	{
		auto txn = ledger.tx_begin_write ();
		open = builder.open ()
			   .source (send1->hash ())
			   .representative (key.pub)
			   .account (key.pub)
			   .sign (key.prv, key.pub)
			   .work (*pool.generate (key.pub))
			   .build ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open));
		expect_account_delegator_by_weight_entry (*store, txn, key.pub, 10, key.pub);
	}

	std::shared_ptr<nano::block> send2;
	process_genesis_legacy_send (ledger, pool, send1->hash (), key.pub, nano::dev::constants.genesis_amount - 30, send2);
	std::shared_ptr<nano::block> receive;
	std::shared_ptr<nano::block> change;
	{
		auto txn = ledger.tx_begin_write ();
		receive = builder.receive ()
				  .previous (open->hash ())
				  .source (send2->hash ())
				  .sign (key.prv, key.pub)
				  .work (*pool.generate (open->hash ()))
				  .build ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive));
		expect_no_account_delegator_by_weight_entry (*store, txn, key.pub, 10, key.pub);
		expect_account_delegator_by_weight_entry (*store, txn, key.pub, 30, key.pub);

		change = builder.change ()
				 .previous (receive->hash ())
				 .representative (new_rep.pub)
				 .sign (key.prv, key.pub)
				 .work (*pool.generate (receive->hash ()))
				 .build ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, change));
		expect_no_account_delegator_by_weight_entry (*store, txn, key.pub, 30, key.pub);
		expect_account_delegator_by_weight_entry (*store, txn, new_rep.pub, 30, key.pub);
	}
}

/*
 * Verifies that rolling back an account open removes the account's delegator
 * index entry. The test opens an account with extended indices enabled, checks
 * the index row, rolls back the open, and confirms both the account table and
 * delegator index no longer contain that account.
 */
TEST (ledger_extended_index, account_delegator_by_weight_rollback)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };
	nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
	nano::keypair key;
	nano::block_builder builder;

	std::shared_ptr<nano::block> send;
	process_genesis_legacy_send (ledger, pool, nano::dev::genesis->hash (), key.pub, nano::dev::constants.genesis_amount - 10, send);
	auto txn = ledger.tx_begin_write ();
	auto open = builder.open ()
				.source (send->hash ())
				.representative (key.pub)
				.account (key.pub)
				.sign (key.prv, key.pub)
				.work (*pool.generate (key.pub))
				.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open));
	expect_account_delegator_by_weight_entry (*store, txn, key.pub, 10, key.pub);

	ASSERT_FALSE (ledger.rollback (txn, open->hash ()));
	ASSERT_FALSE (store->account.exists (txn, key.pub));
	expect_no_account_delegator_by_weight_entry (*store, txn, key.pub, 10, key.pub);
}

/*
 * Populates the account block height index for existing account chains.
 * The mixed legacy/state chain is built while the index flag is disabled, then
 * populate walks the account chains and backfills { account, height } -> block
 * hash entries for genesis and both non-genesis accounts.
 */
TEST (ledger_extended_index, account_block_by_height_populate)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };

	ledger_chain chain;
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		process_legacy_and_state_chains (ledger, pool, chain);
	}

	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		ASSERT_FALSE (ledger.flags.account_block_by_height_index);
		ledger.populate_account_block_by_height_index ();
		ASSERT_TRUE (ledger.flags.account_block_by_height_index);
	}

	auto txn = store->tx_begin_read ();
	ASSERT_EQ (store->block.count (txn), store->account_block_by_height.count (txn));
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 1, nano::dev::genesis->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 2, chain.send1->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 3, chain.send2->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 4, chain.state_send1->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 5, chain.state_send2->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 1, chain.open1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 2, chain.receive1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 3, chain.change1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 1, chain.state_open1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 2, chain.state_receive1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 3, chain.state_change1->hash ());
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled));
}

/*
 * Verifies runtime maintenance of the account block height index. With
 * extended indices enabled, processing blocks writes the expected height rows;
 * rolling back tip blocks removes only those rows and leaves lower account
 * heights intact.
 */
TEST (ledger_extended_index, account_block_by_height_runtime)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };
	nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
	ledger_chain chain;
	process_legacy_and_state_chains (ledger, pool, chain);

	auto txn = ledger.tx_begin_write ();
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 1, nano::dev::genesis->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 2, chain.send1->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 3, chain.send2->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 4, chain.state_send1->hash ());
	expect_account_block_by_height_entry (*store, txn, nano::dev::genesis_key.pub, 5, chain.state_send2->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 1, chain.open1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 2, chain.receive1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 3, chain.change1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 1, chain.state_open1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 2, chain.state_receive1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 3, chain.state_change1->hash ());

	ASSERT_FALSE (ledger.rollback (txn, chain.state_change1->hash ()));
	ASSERT_FALSE (store->account_block_by_height.get (txn, { chain.state_key.pub, 3 }).has_value ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 2, chain.state_receive1->hash ());

	ASSERT_FALSE (ledger.rollback (txn, chain.state_receive1->hash ()));
	ASSERT_FALSE (store->account_block_by_height.get (txn, { chain.state_key.pub, 2 }).has_value ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 1, chain.state_open1->hash ());

	ASSERT_FALSE (ledger.rollback (txn, chain.change1->hash ()));
	ASSERT_FALSE (store->account_block_by_height.get (txn, { chain.key1.pub, 3 }).has_value ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 2, chain.receive1->hash ());

	ASSERT_FALSE (ledger.rollback (txn, chain.receive1->hash ()));
	ASSERT_FALSE (store->account_block_by_height.get (txn, { chain.key1.pub, 2 }).has_value ());
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 1, chain.open1->hash ());
}

/*
 * Exercises account block height lookup bounds while keeping index verification
 * direct. With the index enabled, the test checks backing table entries for
 * both legacy and state account chains and verifies lookup results; after
 * disabling only the account block height flag, it verifies that the fallback
 * traversal returns the same valid hashes.
 */
TEST (ledger_extended_index, account_block_by_height_find)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };
	ledger_chain chain;
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
		process_legacy_and_state_chains (ledger, pool, chain);
		auto txn = ledger.tx_begin_read ();
		ASSERT_FALSE (ledger.find_block_hash_by_height (txn, chain.key1.pub, 0).has_value ());
		expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 2, chain.receive1->hash ());
		expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 3, chain.change1->hash ());
		expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 2, chain.state_receive1->hash ());
		expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 3, chain.state_change1->hash ());

		auto indexed_middle = ledger.find_block_hash_by_height (txn, chain.key1.pub, 2);
		ASSERT_TRUE (indexed_middle.has_value ());
		ASSERT_EQ (chain.receive1->hash (), indexed_middle.value ());
		auto indexed_head = ledger.find_block_hash_by_height (txn, chain.key1.pub, 3);
		ASSERT_TRUE (indexed_head.has_value ());
		ASSERT_EQ (chain.change1->hash (), indexed_head.value ());
		auto indexed_state_middle = ledger.find_block_hash_by_height (txn, chain.state_key.pub, 2);
		ASSERT_TRUE (indexed_state_middle.has_value ());
		ASSERT_EQ (chain.state_receive1->hash (), indexed_state_middle.value ());
		auto indexed_state_head = ledger.find_block_hash_by_height (txn, chain.state_key.pub, 3);
		ASSERT_TRUE (indexed_state_head.has_value ());
		ASSERT_EQ (chain.state_change1->hash (), indexed_state_head.value ());
		ASSERT_FALSE (ledger.find_block_hash_by_height (txn, chain.key1.pub, 4).has_value ());
		ASSERT_FALSE (ledger.find_block_hash_by_height (txn, chain.state_key.pub, 4).has_value ());
	}
	{
		auto txn = store->tx_begin_write ();
		store->version.put_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled, false);
	}
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		ASSERT_FALSE (ledger.flags.account_block_by_height_index);
		auto txn = ledger.tx_begin_read ();
		auto fallback_middle = ledger.find_block_hash_by_height (txn, chain.key1.pub, 2);
		ASSERT_TRUE (fallback_middle.has_value ());
		ASSERT_EQ (chain.receive1->hash (), fallback_middle.value ());
		auto fallback_head = ledger.find_block_hash_by_height (txn, chain.key1.pub, 3);
		ASSERT_TRUE (fallback_head.has_value ());
		ASSERT_EQ (chain.change1->hash (), fallback_head.value ());
		auto fallback_state_middle = ledger.find_block_hash_by_height (txn, chain.state_key.pub, 2);
		ASSERT_TRUE (fallback_state_middle.has_value ());
		ASSERT_EQ (chain.state_receive1->hash (), fallback_state_middle.value ());
		auto fallback_state_head = ledger.find_block_hash_by_height (txn, chain.state_key.pub, 3);
		ASSERT_TRUE (fallback_state_head.has_value ());
		ASSERT_EQ (chain.state_change1->hash (), fallback_state_head.value ());
		ASSERT_FALSE (ledger.find_block_hash_by_height (txn, chain.key1.pub, 4).has_value ());
		ASSERT_FALSE (ledger.find_block_hash_by_height (txn, chain.state_key.pub, 4).has_value ());
	}
}

/*
 * Verifies the aggregate extended-index populate path. The ledger is built with
 * all extended index flags disabled, including received legacy and state chains
 * plus an extra unreceived state send so every extended table has source data.
 * The aggregate populate call must fill all four tables, set all flags, and
 * remain idempotent when called again.
 */
TEST (ledger_extended_index, extended_ledger_indices_populate)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };

	ledger_chain chain;
	nano::keypair receivable_key;
	std::shared_ptr<nano::block> unreceived_send;
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		process_legacy_and_state_chains (ledger, pool, chain);
		process_genesis_state_send (ledger, pool, chain.state_send2->hash (), receivable_key.pub, nano::dev::constants.genesis_amount - 70, unreceived_send);
	}

	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		expect_extended_flags (ledger, false);
		ledger.populate_extended_ledger_indices ();
		expect_extended_flags (ledger, true);
		uint64_t receive_block_count;
		uint64_t receivable_amount_count;
		uint64_t delegator_count;
		uint64_t account_block_count;
		{
			auto txn = ledger.tx_begin_read ();
			ASSERT_EQ (store->block.count (txn), store->account_block_by_height.count (txn));
			receive_block_count = store->receive_block_by_send_block.count (txn);
			receivable_amount_count = store->account_receivable_by_amount.count (txn);
			delegator_count = store->account_delegator_by_weight.count (txn);
			account_block_count = store->account_block_by_height.count (txn);
		}
		ledger.populate_extended_ledger_indices ();
		{
			auto txn = ledger.tx_begin_read ();
			ASSERT_EQ (receive_block_count, store->receive_block_by_send_block.count (txn));
			ASSERT_EQ (receivable_amount_count, store->account_receivable_by_amount.count (txn));
			ASSERT_EQ (delegator_count, store->account_delegator_by_weight.count (txn));
			ASSERT_EQ (account_block_count, store->account_block_by_height.count (txn));
		}
	}

	auto txn = store->tx_begin_read ();
	expect_receive_block_by_send_block_entry (*store, txn, chain.send1->hash (), chain.open1->hash ());
	expect_receive_block_by_send_block_entry (*store, txn, chain.state_send1->hash (), chain.state_open1->hash ());
	expect_account_receivable_by_amount_entry (*store, txn, receivable_key.pub, 10, unreceived_send->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
	expect_account_delegator_by_weight_entry (*store, txn, chain.representative.pub, 30, chain.key1.pub);
	expect_account_delegator_by_weight_entry (*store, txn, chain.state_representative.pub, 30, chain.state_key.pub);
	expect_account_block_by_height_entry (*store, txn, chain.key1.pub, 3, chain.change1->hash ());
	expect_account_block_by_height_entry (*store, txn, chain.state_key.pub, 3, chain.state_change1->hash ());
	expect_persisted_extended_flags (*store, true);
}

/*
 * Verifies the shared drop path for extended indices. After building a ledger
 * with all extended tables populated, including mixed legacy/state chains and
 * one unreceived state send for the receivable amount table,
 * drop_extended_ledger_indices must clear all four tables and set both
 * in-memory and persisted flags to false.
 */
TEST (ledger_extended_index, extended_ledger_indices_drop)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };

	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
		ledger_chain chain;
		nano::keypair receivable_key;
		std::shared_ptr<nano::block> unreceived_send;
		process_legacy_and_state_chains (ledger, pool, chain);
		process_genesis_state_send (ledger, pool, chain.state_send2->hash (), receivable_key.pub, nano::dev::constants.genesis_amount - 70, unreceived_send);
		auto txn = ledger.tx_begin_read ();
		ASSERT_FALSE (store->receive_block_by_send_block.empty (txn));
		ASSERT_FALSE (store->account_receivable_by_amount.empty (txn));
		ASSERT_FALSE (store->account_delegator_by_weight.empty (txn));
		ASSERT_FALSE (store->account_block_by_height.empty (txn));
	}

	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
		expect_extended_flags (ledger, true);
		ledger.drop_extended_ledger_indices ();
		expect_extended_flags (ledger, false);
	}

	auto txn = store->tx_begin_read ();
	ASSERT_TRUE (store->receive_block_by_send_block.empty (txn));
	ASSERT_TRUE (store->account_receivable_by_amount.empty (txn));
	ASSERT_TRUE (store->account_delegator_by_weight.empty (txn));
	ASSERT_TRUE (store->account_block_by_height.empty (txn));
	expect_persisted_extended_flags (*store, false);
}

/*
 * Verifies disabling behavior on reopen. The test first creates persisted
 * extended index data, reopens the ledger without the extended option so flags
 * are marked disabled, then processes another send and checks that disabled
 * runtime hooks do not append new extended-index rows.
 */
TEST (ledger_extended_index, extended_ledger_indices_disabled_on_reopen)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::work_pool pool{ nano::dev::network_params.network, 1 };
	nano::keypair key;

	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger, extended_ledger_options () };
		std::shared_ptr<nano::block> send;
		process_genesis_legacy_send (ledger, pool, nano::dev::genesis->hash (), key.pub, nano::dev::constants.genesis_amount - 10, send);
		auto txn = ledger.tx_begin_read ();
		expect_account_receivable_by_amount_entry (*store, txn, key.pub, 10, send->hash (), nano::dev::genesis_key.pub, nano::epoch::epoch_0);
	}
	{
		nano::ledger ledger{ *store, nano::dev::network_params, stats, logger };
		expect_extended_flags (ledger, false);
		expect_persisted_extended_flags (*store, false);

		auto latest = ledger.any.account_head (ledger.tx_begin_read (), nano::dev::genesis_key.pub);
		std::shared_ptr<nano::block> send;
		process_genesis_legacy_send (ledger, pool, latest, key.pub, nano::dev::constants.genesis_amount - 20, send);
		auto txn = ledger.tx_begin_read ();
		expect_no_account_receivable_by_amount_entry (*store, txn, key.pub, 10, send->hash ());
		ASSERT_FALSE (store->account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 3 }).has_value ());
	}
}
