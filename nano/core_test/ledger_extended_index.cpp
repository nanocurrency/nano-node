#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/work.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/extended/account_block_by_height.hpp>
#include <nano/store/ledger/extended/account_delegator_by_weight.hpp>
#include <nano/store/ledger/extended/account_receivable_by_amount.hpp>
#include <nano/store/ledger/extended/receive_block_by_send_block.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger/version.hpp>
#include <nano/store/meta.hpp>
#include <nano/test_common/make_store.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
// True when the delegator index holds exactly this key
bool delegator_exists (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::account_delegator_by_weight_key const & key)
{
	auto it = store.extended.account_delegator_by_weight.begin (txn, key);
	return it != store.extended.account_delegator_by_weight.end (txn) && it->first == key;
}

// Exact-match lookup in the receivable index
std::optional<nano::account_receivable_by_amount_info> receivable_get (nano::store::ledger_store & store, nano::store::transaction const & txn, nano::account_receivable_by_amount_key const & key)
{
	auto it = store.extended.account_receivable_by_amount.begin (txn, key);
	if (it != store.extended.account_receivable_by_amount.end (txn) && it->first == key)
	{
		return it->second;
	}
	return std::nullopt;
}

/*
 * Cross-check every extended index against its primary table:
 * each primary row must have exactly one matching index row (forward containment + count equality covers both directions)
 */
void assert_extended_indices_consistent (nano::ledger & ledger, nano::secure::transaction const & txn)
{
	auto & store = ledger.store;

	// Delegator index <-> account table
	uint64_t account_count{ 0 };
	for (auto i = store.account.begin (txn), n = store.account.end (txn); i != n; ++i)
	{
		nano::account_delegator_by_weight_key key{ i->second.representative, i->second.balance, i->first };
		ASSERT_TRUE (delegator_exists (store, txn, key)) << "missing delegator entry for account " << i->first.to_account ();
		++account_count;
	}
	ASSERT_EQ (account_count, store.extended.account_delegator_by_weight.count (txn));

	// Receivable index <-> pending table
	uint64_t pending_count{ 0 };
	for (auto i = store.pending.begin (txn), n = store.pending.end (txn); i != n; ++i)
	{
		nano::account_receivable_by_amount_key key{ i->first.account, i->second.amount, i->first.hash };
		auto info = receivable_get (store, txn, key);
		ASSERT_TRUE (info.has_value ()) << "missing receivable entry for send " << i->first.hash.to_string ();
		ASSERT_EQ (i->second.source, info->source);
		ASSERT_EQ (i->second.epoch, info->epoch);
		++pending_count;
	}
	ASSERT_EQ (pending_count, store.extended.account_receivable_by_amount.count (txn));

	// Height index and receive lookup index <-> block table
	uint64_t block_count{ 0 };
	uint64_t receive_count{ 0 };
	for (auto i = store.block.begin (txn), n = store.block.end (txn); i != n; ++i)
	{
		auto const & block = i->second.block;
		auto indexed = store.extended.account_block_by_height.get (txn, { block->account (), i->second.sideband.height });
		ASSERT_TRUE (indexed.has_value ()) << "missing height entry for block " << i->first.to_string ();
		ASSERT_EQ (i->first, indexed.value ());
		if (block->is_receive ())
		{
			auto receive = store.extended.receive_block_by_send_block.get (txn, block->source ());
			ASSERT_TRUE (receive.has_value ()) << "missing receive entry for block " << i->first.to_string ();
			ASSERT_EQ (i->first, receive.value ());
			++receive_count;
		}
		++block_count;
	}
	ASSERT_EQ (block_count, store.extended.account_block_by_height.count (txn));
	ASSERT_EQ (receive_count, store.extended.receive_block_by_send_block.count (txn));
}
}

/*
 * Exact-match round-trip on the account block height table, including a height beyond 32 bits
 */
TEST (ledger_extended_index, account_block_by_height_roundtrip)
{
	auto store = nano::test::make_store ();
	nano::account account{ 9 };
	nano::block_hash hash1{ 101 };
	nano::block_hash hash2{ 102 };
	{
		auto txn = store->tx_begin_write ();
		ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
		store->extended.account_block_by_height.put (txn, { account, 1 }, hash1);
		store->extended.account_block_by_height.put (txn, { account, uint64_t{ 1 } << 40 }, hash2);
		ASSERT_EQ (2, store->extended.account_block_by_height.count (txn));
		ASSERT_EQ (hash1, store->extended.account_block_by_height.get (txn, { account, 1 }).value ());
		ASSERT_EQ (hash2, store->extended.account_block_by_height.get (txn, { account, uint64_t{ 1 } << 40 }).value ());
		ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { account, 2 }).has_value ());
		ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { nano::account{ 10 }, 1 }).has_value ());
		store->extended.account_block_by_height.del (txn, { account, 1 });
		ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { account, 1 }).has_value ());
		ASSERT_EQ (1, store->extended.account_block_by_height.count (txn));
	}
	store->extended.account_block_by_height.clear ();
	auto txn = store->tx_begin_read ();
	ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
}

/*
 * Exact-match round-trip on the receive block lookup table
 */
TEST (ledger_extended_index, receive_block_by_send_block_roundtrip)
{
	auto store = nano::test::make_store ();
	nano::block_hash send1{ 1 };
	nano::block_hash send2{ 2 };
	nano::block_hash receive1{ 11 };
	nano::block_hash receive2{ 12 };
	{
		auto txn = store->tx_begin_write ();
		ASSERT_TRUE (store->extended.receive_block_by_send_block.empty (txn));
		store->extended.receive_block_by_send_block.put (txn, send1, receive1);
		store->extended.receive_block_by_send_block.put (txn, send2, receive2);
		ASSERT_EQ (2, store->extended.receive_block_by_send_block.count (txn));
		ASSERT_EQ (receive1, store->extended.receive_block_by_send_block.get (txn, send1).value ());
		ASSERT_EQ (receive2, store->extended.receive_block_by_send_block.get (txn, send2).value ());
		ASSERT_FALSE (store->extended.receive_block_by_send_block.get (txn, nano::block_hash{ 3 }).has_value ());
		store->extended.receive_block_by_send_block.del (txn, send1);
		ASSERT_FALSE (store->extended.receive_block_by_send_block.get (txn, send1).has_value ());
		ASSERT_EQ (1, store->extended.receive_block_by_send_block.count (txn));
	}
	store->extended.receive_block_by_send_block.clear ();
	auto txn = store->tx_begin_read ();
	ASSERT_TRUE (store->extended.receive_block_by_send_block.empty (txn));
}

/*
 * Delegator index iteration must order entries by (representative, weight, delegator) numerically.
 * Weights crossing byte boundaries (1 vs 256 vs 2^64 vs 2^100) catch endianness mistakes in key serialization.
 */
TEST (ledger_extended_index, delegator_by_weight_ordering)
{
	auto store = nano::test::make_store ();
	auto & view = store->extended.account_delegator_by_weight;

	std::vector<nano::account_delegator_by_weight_key> expected{
		{ nano::account{ 5 }, nano::amount{ 1 }, nano::account{ 10 } },
		{ nano::account{ 5 }, nano::amount{ 1 }, nano::account{ 11 } },
		{ nano::account{ 5 }, nano::amount{ 256 }, nano::account{ 3 } },
		{ nano::account{ 5 }, nano::amount{ nano::uint128_t{ 1 } << 64 }, nano::account{ 2 } },
		{ nano::account{ 5 }, nano::amount{ nano::uint128_t{ 1 } << 100 }, nano::account{ 1 } },
		{ nano::account{ 7 }, nano::amount{ 0 }, nano::account{ 9 } },
	};

	{
		auto txn = store->tx_begin_write ();
		ASSERT_TRUE (view.empty (txn));

		// Insert in shuffled order
		view.put (txn, expected[3]);
		view.put (txn, expected[0]);
		view.put (txn, expected[5]);
		view.put (txn, expected[2]);
		view.put (txn, expected[4]);
		view.put (txn, expected[1]);
		ASSERT_FALSE (view.empty (txn));

		std::vector<nano::account_delegator_by_weight_key> actual;
		for (auto i = view.begin (txn), n = view.end (txn); i != n; ++i)
		{
			actual.push_back (i->first);
		}
		ASSERT_EQ (expected, actual);

		// Seeking begin: exact key, between keys, past the last key
		ASSERT_EQ (expected[2], view.begin (txn, expected[2])->first);
		ASSERT_EQ (expected[2], view.begin (txn, { nano::account{ 5 }, nano::amount{ 2 }, nano::account{ 0 } })->first);
		ASSERT_TRUE (view.begin (txn, { nano::account{ 8 }, nano::amount{ 0 }, nano::account{ 0 } }) == view.end (txn));

		view.del (txn, expected[0]);
		ASSERT_EQ (5, view.count (txn));
	}

	view.clear ();
	auto txn = store->tx_begin_read ();
	ASSERT_TRUE (view.empty (txn));
	ASSERT_EQ (0, view.count (txn));
}

/*
 * upper_bound must return the highest-weight entry of the given representative and end () otherwise.
 * rupper_bound must walk that representative's entries in descending weight order and terminate at rend () when iteration passes the first entry of the table.
 */
TEST (ledger_extended_index, delegator_by_weight_upper_bound)
{
	auto store = nano::test::make_store ();
	auto txn = store->tx_begin_write ();
	auto & view = store->extended.account_delegator_by_weight;

	// Empty table
	ASSERT_TRUE (view.upper_bound (txn, nano::account{ 5 }) == view.end (txn));
	ASSERT_TRUE (view.rupper_bound (txn, nano::account{ 5 }) == view.rend (txn));

	nano::account_delegator_by_weight_key low{ nano::account{ 5 }, nano::amount{ 1 }, nano::account{ 10 } };
	nano::account_delegator_by_weight_key mid{ nano::account{ 5 }, nano::amount{ 256 }, nano::account{ 3 } };
	nano::account_delegator_by_weight_key high{ nano::account{ 5 }, nano::amount{ nano::uint128_t{ 1 } << 100 }, nano::account{ 1 } };
	nano::account_delegator_by_weight_key other{ nano::account{ 7 }, nano::amount{ 0 }, nano::account{ 9 } };
	view.put (txn, low);
	view.put (txn, mid);
	view.put (txn, high);
	view.put (txn, other);

	// Representative below the first entry, present, absent between entries, present again, absent above all entries
	ASSERT_TRUE (view.upper_bound (txn, nano::account{ 4 }) == view.end (txn));
	ASSERT_EQ (high, view.upper_bound (txn, nano::account{ 5 })->first);
	ASSERT_TRUE (view.upper_bound (txn, nano::account{ 6 }) == view.end (txn));
	ASSERT_EQ (other, view.upper_bound (txn, nano::account{ 7 })->first);
	ASSERT_TRUE (view.upper_bound (txn, nano::account{ 8 }) == view.end (txn));

	// Maximum representative value exercises the saturation branch
	nano::account max_rep{ std::numeric_limits<nano::uint256_t>::max () };
	ASSERT_TRUE (view.upper_bound (txn, max_rep) == view.end (txn));
	nano::account_delegator_by_weight_key max_key{ max_rep, nano::amount{ 5 }, nano::account{ 1 } };
	view.put (txn, max_key);
	ASSERT_EQ (max_key, view.upper_bound (txn, max_rep)->first);

	// Reverse iteration yields rep 5 entries in descending weight order, then wraps past the table start to rend
	std::vector<nano::account_delegator_by_weight_key> reverse;
	for (auto i = view.rupper_bound (txn, nano::account{ 5 }), n = view.rend (txn); i != n; ++i)
	{
		reverse.push_back (i->first);
	}
	std::vector<nano::account_delegator_by_weight_key> reverse_expected{ high, mid, low };
	ASSERT_EQ (reverse_expected, reverse);
}

/*
 * rlower_bound must position the reverse iterator at the last entry ordered strictly before the given key,
 * crossing representative boundaries, and rend when nothing precedes the key
 */
TEST (ledger_extended_index, delegator_by_weight_rlower_bound)
{
	auto store = nano::test::make_store ();
	auto txn = store->tx_begin_write ();
	auto & view = store->extended.account_delegator_by_weight;

	// Empty table
	ASSERT_TRUE (view.rlower_bound (txn, { nano::account{ 5 }, nano::amount{ 1 }, nano::account{ 1 } }) == view.rend (txn));

	nano::account_delegator_by_weight_key low{ nano::account{ 5 }, nano::amount{ 1 }, nano::account{ 10 } };
	nano::account_delegator_by_weight_key mid{ nano::account{ 5 }, nano::amount{ 256 }, nano::account{ 3 } };
	nano::account_delegator_by_weight_key high{ nano::account{ 5 }, nano::amount{ nano::uint128_t{ 1 } << 100 }, nano::account{ 1 } };
	nano::account_delegator_by_weight_key other{ nano::account{ 7 }, nano::amount{ 0 }, nano::account{ 9 } };
	view.put (txn, low);
	view.put (txn, mid);
	view.put (txn, high);
	view.put (txn, other);

	// Nothing precedes the first entry
	ASSERT_TRUE (view.rlower_bound (txn, low) == view.rend (txn));
	// Strictly-below positioning within a representative
	ASSERT_EQ (low, view.rlower_bound (txn, mid)->first);
	ASSERT_EQ (mid, view.rlower_bound (txn, high)->first);
	// A key between entries resolves to the nearest predecessor
	ASSERT_EQ (mid, view.rlower_bound (txn, { nano::account{ 5 }, nano::amount{ 256 }, nano::account{ 4 } })->first);
	// Crossing into the previous representative is the caller's concern
	ASSERT_EQ (high, view.rlower_bound (txn, other)->first);
	ASSERT_EQ (other, view.rlower_bound (txn, { nano::account{ 8 }, nano::amount{ 0 }, nano::account{ 0 } })->first);

	// Reverse walk from a cursor visits every preceding entry and terminates at rend
	std::vector<nano::account_delegator_by_weight_key> reverse;
	for (auto i = view.rlower_bound (txn, high), n = view.rend (txn); i != n; ++i)
	{
		reverse.push_back (i->first);
	}
	std::vector<nano::account_delegator_by_weight_key> reverse_expected{ mid, low };
	ASSERT_EQ (reverse_expected, reverse);
}

/*
 * Receivable index: value round-trip, numeric amount ordering and upper_bound behavior
 */
TEST (ledger_extended_index, receivable_by_amount_roundtrip_and_bounds)
{
	auto store = nano::test::make_store ();
	auto txn = store->tx_begin_write ();
	auto & view = store->extended.account_receivable_by_amount;

	nano::account_receivable_by_amount_key small{ nano::account{ 5 }, nano::amount{ 1 }, nano::block_hash{ 21 } };
	nano::account_receivable_by_amount_key medium{ nano::account{ 5 }, nano::amount{ 256 }, nano::block_hash{ 22 } };
	nano::account_receivable_by_amount_key large{ nano::account{ 5 }, nano::amount{ nano::uint128_t{ 1 } << 64 }, nano::block_hash{ 23 } };
	nano::account_receivable_by_amount_key other{ nano::account{ 7 }, nano::amount{ 3 }, nano::block_hash{ 24 } };
	nano::account_receivable_by_amount_info info{ nano::account{ 42 }, nano::epoch::epoch_2 };

	view.put (txn, medium, info);
	view.put (txn, small, info);
	view.put (txn, other, info);
	view.put (txn, large, info);
	ASSERT_EQ (4, view.count (txn));

	// Value round-trip
	auto it = view.begin (txn, small);
	ASSERT_TRUE (it != view.end (txn));
	ASSERT_EQ (small, it->first);
	ASSERT_EQ (info, it->second);

	// Forward iteration in ascending amount order within the account
	std::vector<nano::account_receivable_by_amount_key> actual;
	for (auto i = view.begin (txn), n = view.end (txn); i != n; ++i)
	{
		actual.push_back (i->first);
	}
	std::vector<nano::account_receivable_by_amount_key> expected{ small, medium, large, other };
	ASSERT_EQ (expected, actual);

	// upper_bound points at the largest amount of the account
	ASSERT_EQ (large, view.upper_bound (txn, nano::account{ 5 })->first);
	ASSERT_TRUE (view.upper_bound (txn, nano::account{ 6 }) == view.end (txn));

	// Descending walk over account 5 terminates when reaching the table start
	std::vector<nano::account_receivable_by_amount_key> reverse;
	for (auto i = view.rupper_bound (txn, nano::account{ 5 }), n = view.rend (txn); i != n; ++i)
	{
		reverse.push_back (i->first);
	}
	std::vector<nano::account_receivable_by_amount_key> reverse_expected{ large, medium, small };
	ASSERT_EQ (reverse_expected, reverse);

	view.del (txn, small);
	ASSERT_EQ (3, view.count (txn));
	ASSERT_TRUE (view.begin (txn, small) == view.end (txn) || view.begin (txn, small)->first != small);
}

/*
 * A fresh ledger created with the option enabled must enable and persist all four index flags and index the genesis state.
 * The genesis open block is indexed in the receive lookup table under its source field, which is the genesis public key rather than a real send hash.
 */
TEST (ledger_extended_index, fresh_ledger_enables_flags)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });

	ASSERT_TRUE (ledger.flags.account_delegator_by_weight_index);
	ASSERT_TRUE (ledger.flags.account_receivable_by_amount_index);
	ASSERT_TRUE (ledger.flags.receive_block_by_send_block_index);
	ASSERT_TRUE (ledger.flags.account_block_by_height_index);
	ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());

	auto txn = ledger.tx_begin_read ();
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled));
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled));
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled));
	ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled));

	ASSERT_EQ (1, store->extended.account_delegator_by_weight.count (txn));
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount, nano::dev::genesis_key.pub }));
	ASSERT_EQ (1, store->extended.account_block_by_height.count (txn));
	ASSERT_EQ (nano::dev::genesis->hash (), store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 1 }).value ());
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	ASSERT_EQ (1, store->extended.receive_block_by_send_block.count (txn));
	ASSERT_EQ (nano::dev::genesis->hash (), store->extended.receive_block_by_send_block.get (txn, nano::dev::genesis->source_field ().value ()).value ());

	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
}

/*
 * Without the option the indices must stay disabled and their tables empty
 */
TEST (ledger_extended_index, disabled_by_default)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger);

	ASSERT_FALSE (ledger.flags.any_extended_ledger_index_enabled ());

	auto txn = ledger.tx_begin_read ();
	ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled));
	ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled));
	ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled));
	ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled));
	ASSERT_TRUE (store->extended.account_delegator_by_weight.empty (txn));
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	ASSERT_TRUE (store->extended.receive_block_by_send_block.empty (txn));
	ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
}

/*
 * Once enabled, the persisted flags remain authoritative:
 * reopening without the option must keep the flags set and keep maintaining the indices for newly processed blocks
 */
TEST (ledger_extended_index, flags_persist_and_maintain_without_option)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto const path = nano::unique_path ();
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
		ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());
	}
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
		ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());

		nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
		nano::block_builder builder;
		nano::keypair key1;
		auto send1 = builder.state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 100)
					 .link (key1.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*pool.generate (nano::dev::genesis->hash ()))
					 .build ();
		auto txn = ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
		ASSERT_TRUE (receivable_get (*store, txn, { key1.pub, 100, send1->hash () }).has_value ());
		ASSERT_EQ (send1->hash (), store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 2 }).value ());
		ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
	}
	// A third open confirms nothing rewrote the persisted flags
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
		ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());
	}
}

/*
 * Population of an existing ledger: build a mixed legacy/state/epoch ledger without the option,
 * then reopen with the option and verify every index matches the primary tables.
 * A further reopen must not disturb the indices.
 */
TEST (ledger_extended_index, populate_existing_ledger)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto const path = nano::unique_path ();
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1, key2, key3, key4, key5;

	// Legacy chain on genesis: self send/receive plus a send opening key1, which then changes its representative
	auto lsend1 = builder.send ()
				  .previous (nano::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (nano::dev::constants.genesis_amount - 100)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();
	auto lsend2 = builder.send ()
				  .previous (lsend1->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 150)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (lsend1->hash ()))
				  .build ();
	auto lreceive1 = builder.receive ()
					 .previous (lsend2->hash ())
					 .source (lsend2->hash ())
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*pool.generate (lsend2->hash ()))
					 .build ();
	auto lopen1 = builder.open ()
				  .source (lsend1->hash ())
				  .representative (key2.pub)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto lchange1 = builder.change ()
					.previous (lopen1->hash ())
					.representative (key3.pub)
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (lopen1->hash ()))
					.build ();
	// State blocks: open key4, upgrade it to epoch 1, leave a send to key5 pending
	auto ssend1 = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (lreceive1->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 125)
				  .link (key4.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (lreceive1->hash ()))
				  .build ();
	auto sopen4 = builder.state ()
				  .account (key4.pub)
				  .previous (0)
				  .representative (key4.pub)
				  .balance (25)
				  .link (ssend1->hash ())
				  .sign (key4.prv, key4.pub)
				  .work (*pool.generate (key4.pub))
				  .build ();
	auto ssend2 = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (ssend1->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 135)
				  .link (key5.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (ssend1->hash ()))
				  .build ();
	// Two more sends left pending so the pending crawl spans multiple populate batches
	auto ssend3 = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (ssend2->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 140)
				  .link (key2.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (ssend2->hash ()))
				  .build ();
	auto ssend4 = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (ssend3->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 142)
				  .link (key5.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (ssend3->hash ()))
				  .build ();

	// Build the ledger without the option; indices must remain empty
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
		auto txn = ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, lsend1));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, lsend2));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, lreceive1));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, lopen1));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, lchange1));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, ssend1));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, sopen4));
		auto epoch4 = builder.state ()
					  .account (key4.pub)
					  .previous (sopen4->hash ())
					  .representative (key4.pub)
					  .balance (25)
					  .link (ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*pool.generate (sopen4->hash ()))
					  .build ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, epoch4));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, ssend2));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, ssend3));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, ssend4));
		ASSERT_TRUE (store->extended.account_delegator_by_weight.empty (txn));
		ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
		ASSERT_TRUE (store->extended.receive_block_by_send_block.empty (txn));
		ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
	}
	// Reopen with the option; missing indices must be populated in full
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
		ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());
		auto txn = ledger.tx_begin_read ();
		ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
		ASSERT_EQ (3, store->extended.account_delegator_by_weight.count (txn));
		ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount - 142, nano::dev::genesis_key.pub }));
		ASSERT_TRUE (delegator_exists (*store, txn, { key3.pub, 100, key1.pub }));
		ASSERT_TRUE (delegator_exists (*store, txn, { key4.pub, 25, key4.pub }));
		ASSERT_EQ (3, store->extended.account_receivable_by_amount.count (txn));
		auto receivable = receivable_get (*store, txn, { key5.pub, 10, ssend2->hash () });
		ASSERT_TRUE (receivable.has_value ());
		ASSERT_EQ (nano::dev::genesis_key.pub, receivable->source);
		ASSERT_EQ (nano::epoch::epoch_0, receivable->epoch);
		ASSERT_TRUE (receivable_get (*store, txn, { key2.pub, 5, ssend3->hash () }).has_value ());
		ASSERT_TRUE (receivable_get (*store, txn, { key5.pub, 2, ssend4->hash () }).has_value ());
		ASSERT_EQ (12, store->extended.account_block_by_height.count (txn));
		ASSERT_EQ (4, store->extended.receive_block_by_send_block.count (txn));
		ASSERT_EQ (lopen1->hash (), store->extended.receive_block_by_send_block.get (txn, lsend1->hash ()).value ());
		ASSERT_EQ (lreceive1->hash (), store->extended.receive_block_by_send_block.get (txn, lsend2->hash ()).value ());
		ASSERT_EQ (sopen4->hash (), store->extended.receive_block_by_send_block.get (txn, ssend1->hash ()).value ());
	}
	// Reopening again with the option must not re-populate or disturb anything
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
		auto txn = ledger.tx_begin_read ();
		ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
		ASSERT_EQ (12, store->extended.account_block_by_height.count (txn));
		ASSERT_EQ (4, store->extended.receive_block_by_send_block.count (txn));
	}
}

/*
 * A single index can be populated in isolation;
 * the remaining indices stay disabled and are not maintained for new blocks until the aggregate populate enables them.
 * A second aggregate populate call is a no-op.
 */
TEST (ledger_extended_index, single_index_populate_and_mixed_flags)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1, key2;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	{
		auto txn = ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	}

	// Populate only the receive lookup index
	ledger.populate_receive_block_by_send_block_index ();
	ASSERT_TRUE (ledger.flags.receive_block_by_send_block_index);
	ASSERT_FALSE (ledger.flags.account_block_by_height_index);
	ASSERT_FALSE (ledger.flags.account_delegator_by_weight_index);
	ASSERT_FALSE (ledger.flags.account_receivable_by_amount_index);
	ASSERT_TRUE (ledger.flags.any_extended_ledger_index_enabled ());
	ASSERT_FALSE (ledger.flags.all_extended_ledger_indices_enabled ());
	{
		auto txn = ledger.tx_begin_read ();
		ASSERT_TRUE (store->version.get_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled));
		ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled));
		ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled));
		ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled));
		ASSERT_EQ (2, store->extended.receive_block_by_send_block.count (txn));
		ASSERT_EQ (open1->hash (), store->extended.receive_block_by_send_block.get (txn, send1->hash ()).value ());
		ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
		ASSERT_TRUE (store->extended.account_delegator_by_weight.empty (txn));
		ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	}

	// With mixed flags only the enabled index is maintained for new blocks
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 150)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto open2 = builder.state ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key2.pub)
				 .balance (50)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*pool.generate (key2.pub))
				 .build ();
	auto send3 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 175)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send2->hash ()))
				 .build ();
	{
		auto txn = ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open2));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send3));
		ASSERT_EQ (open2->hash (), store->extended.receive_block_by_send_block.get (txn, send2->hash ()).value ());
		ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
		ASSERT_TRUE (store->extended.account_delegator_by_weight.empty (txn));
		ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	}

	// The aggregate populate skips the already-enabled index and fills the rest
	ledger.populate_extended_ledger_indices ();
	ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());
	{
		auto txn = ledger.tx_begin_read ();
		ASSERT_EQ (6, store->extended.account_block_by_height.count (txn));
		ASSERT_EQ (3, store->extended.account_delegator_by_weight.count (txn));
		ASSERT_EQ (1, store->extended.account_receivable_by_amount.count (txn));
		ASSERT_EQ (3, store->extended.receive_block_by_send_block.count (txn));
		ASSERT_TRUE (receivable_get (*store, txn, { key2.pub, 25, send3->hash () }).has_value ());
		ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
	}

	// A second aggregate populate must be a no-op
	ledger.populate_extended_ledger_indices ();
	{
		auto txn = ledger.tx_begin_read ();
		ASSERT_EQ (6, store->extended.account_block_by_height.count (txn));
		ASSERT_EQ (3, store->extended.account_delegator_by_weight.count (txn));
		ASSERT_EQ (1, store->extended.account_receivable_by_amount.count (txn));
		ASSERT_EQ (3, store->extended.receive_block_by_send_block.count (txn));
		ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
	}
}

/*
 * Incremental maintenance for state blocks: send creates a receivable entry,
 * open consumes it and registers the receive lookup, a representative change moves the delegator entry
 */
TEST (ledger_extended_index, incremental_state_blocks)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1;
	auto txn = ledger.tx_begin_write ();

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	auto receivable = receivable_get (*store, txn, { key1.pub, 100, send1->hash () });
	ASSERT_TRUE (receivable.has_value ());
	ASSERT_EQ (nano::dev::genesis_key.pub, receivable->source);
	ASSERT_EQ (nano::epoch::epoch_0, receivable->epoch);
	ASSERT_EQ (send1->hash (), store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 2 }).value ());
	ASSERT_EQ (1, store->extended.account_delegator_by_weight.count (txn));
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount - 100, nano::dev::genesis_key.pub }));
	ASSERT_FALSE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount, nano::dev::genesis_key.pub }));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));

	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	ASSERT_EQ (open1->hash (), store->extended.receive_block_by_send_block.get (txn, send1->hash ()).value ());
	ASSERT_EQ (open1->hash (), store->extended.account_block_by_height.get (txn, { key1.pub, 1 }).value ());
	ASSERT_EQ (2, store->extended.account_delegator_by_weight.count (txn));
	ASSERT_TRUE (delegator_exists (*store, txn, { key1.pub, 100, key1.pub }));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));

	auto change1 = builder.state ()
				   .account (key1.pub)
				   .previous (open1->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (100)
				   .link (0)
				   .sign (key1.prv, key1.pub)
				   .work (*pool.generate (open1->hash ()))
				   .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, change1));
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, 100, key1.pub }));
	ASSERT_FALSE (delegator_exists (*store, txn, { key1.pub, 100, key1.pub }));
	ASSERT_EQ (change1->hash (), store->extended.account_block_by_height.get (txn, { key1.pub, 2 }).value ());
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));

	// A state receive on an existing account must consume the receivable, register the receive lookup and replace the balance-keyed delegator row
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 150)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (change1->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (150)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (change1->hash ()))
					.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ASSERT_TRUE (receivable_get (*store, txn, { key1.pub, 50, send2->hash () }).has_value ());
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive2));
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	ASSERT_EQ (receive2->hash (), store->extended.receive_block_by_send_block.get (txn, send2->hash ()).value ());
	ASSERT_EQ (receive2->hash (), store->extended.account_block_by_height.get (txn, { key1.pub, 3 }).value ());
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, 150, key1.pub }));
	ASSERT_FALSE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, 100, key1.pub }));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
}

/*
 * Incremental maintenance for legacy blocks.
 * Legacy open and receive blocks must both register in the receive lookup index.
 */
TEST (ledger_extended_index, incremental_legacy_blocks)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1, key2, key3;
	auto txn = ledger.tx_begin_write ();

	auto send1 = builder.send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	auto receivable = receivable_get (*store, txn, { key1.pub, 100, send1->hash () });
	ASSERT_TRUE (receivable.has_value ());
	ASSERT_EQ (nano::dev::genesis_key.pub, receivable->source);
	ASSERT_EQ (send1->hash (), store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 2 }).value ());

	auto open1 = builder.open ()
				 .source (send1->hash ())
				 .representative (key2.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	ASSERT_EQ (open1->hash (), store->extended.receive_block_by_send_block.get (txn, send1->hash ()).value ());
	ASSERT_EQ (open1->hash (), store->extended.account_block_by_height.get (txn, { key1.pub, 1 }).value ());
	ASSERT_TRUE (delegator_exists (*store, txn, { key2.pub, 100, key1.pub }));

	auto change1 = builder.change ()
				   .previous (open1->hash ())
				   .representative (key3.pub)
				   .sign (key1.prv, key1.pub)
				   .work (*pool.generate (open1->hash ()))
				   .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, change1));
	ASSERT_TRUE (delegator_exists (*store, txn, { key3.pub, 100, key1.pub }));
	ASSERT_FALSE (delegator_exists (*store, txn, { key2.pub, 100, key1.pub }));

	auto send2 = builder.send ()
				 .previous (send1->hash ())
				 .destination (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 150)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	auto receive1 = builder.receive ()
					.previous (send2->hash ())
					.source (send2->hash ())
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*pool.generate (send2->hash ()))
					.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive1));
	ASSERT_EQ (receive1->hash (), store->extended.receive_block_by_send_block.get (txn, send2->hash ()).value ());
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
}

/*
 * Epoch blocks only touch the height index.
 * An epoch open on an unopened account creates a zero-weight delegator entry under the zero representative and must not consume the receivable.
 */
TEST (ledger_extended_index, epoch_blocks)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1;
	auto txn = ledger.tx_begin_write ();

	auto epoch1 = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (nano::epoch::epoch_1))
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, epoch1));
	ASSERT_EQ (epoch1->hash (), store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 2 }).value ());
	ASSERT_EQ (1, store->extended.account_delegator_by_weight.count (txn));
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount, nano::dev::genesis_key.pub }));
	ASSERT_EQ (1, store->extended.receive_block_by_send_block.count (txn));
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));

	// Send from the epoch 1 account, the receivable entry must record the source epoch
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (epoch1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (epoch1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	auto receivable = receivable_get (*store, txn, { key1.pub, 100, send1->hash () });
	ASSERT_TRUE (receivable.has_value ());
	ASSERT_EQ (nano::epoch::epoch_1, receivable->epoch);

	// Epoch open on the unopened destination: zero representative, zero weight, receivable untouched
	auto epoch_open = builder.state ()
					  .account (key1.pub)
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*pool.generate (key1.pub))
					  .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, epoch_open));
	ASSERT_EQ (epoch_open->hash (), store->extended.account_block_by_height.get (txn, { key1.pub, 1 }).value ());
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::account{ 0 }, nano::amount{ 0 }, key1.pub }));
	ASSERT_EQ (1, store->extended.receive_block_by_send_block.count (txn));
	ASSERT_EQ (1, store->extended.account_receivable_by_amount.count (txn));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));

	// Rolling the epoch open back must remove the account's index entries and keep the receivable
	ASSERT_FALSE (ledger.rollback (txn, epoch_open->hash ()));
	ASSERT_FALSE (delegator_exists (*store, txn, { nano::account{ 0 }, nano::amount{ 0 }, key1.pub }));
	ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { key1.pub, 1 }).has_value ());
	ASSERT_EQ (1, store->extended.account_receivable_by_amount.count (txn));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
}

/*
 * Rolling back a receive must restore the receivable entry and remove the receive lookup entry;
 * rolling back the send must then remove the receivable entry again.
 * Stepping twice down each chain must only ever remove the tip's height row, leaving lower heights intact.
 */
TEST (ledger_extended_index, rollback_state_send_receive)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1;
	auto txn = ledger.tx_begin_write ();

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 150)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (open1->hash ())
					.representative (key1.pub)
					.balance (150)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive2));

	// Roll back the receive tip: its receivable is restored, other entries stay intact
	ASSERT_FALSE (ledger.rollback (txn, receive2->hash ()));
	ASSERT_TRUE (receivable_get (*store, txn, { key1.pub, 50, send2->hash () }).has_value ());
	ASSERT_FALSE (store->extended.receive_block_by_send_block.get (txn, send2->hash ()).has_value ());
	ASSERT_EQ (open1->hash (), store->extended.receive_block_by_send_block.get (txn, send1->hash ()).value ());
	ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { key1.pub, 2 }).has_value ());
	ASSERT_EQ (open1->hash (), store->extended.account_block_by_height.get (txn, { key1.pub, 1 }).value ());
	ASSERT_TRUE (delegator_exists (*store, txn, { key1.pub, 100, key1.pub }));
	ASSERT_FALSE (delegator_exists (*store, txn, { key1.pub, 150, key1.pub }));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));

	// Roll back the second send: its receivable disappears, lower genesis heights stay intact
	ASSERT_FALSE (ledger.rollback (txn, send2->hash ()));
	ASSERT_FALSE (receivable_get (*store, txn, { key1.pub, 50, send2->hash () }).has_value ());
	ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 3 }).has_value ());
	ASSERT_EQ (send1->hash (), store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 2 }).value ());
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));

	ASSERT_FALSE (ledger.rollback (txn, open1->hash ()));
	auto receivable = receivable_get (*store, txn, { key1.pub, 100, send1->hash () });
	ASSERT_TRUE (receivable.has_value ());
	ASSERT_EQ (nano::dev::genesis_key.pub, receivable->source);
	ASSERT_EQ (nano::epoch::epoch_0, receivable->epoch);
	ASSERT_FALSE (store->extended.receive_block_by_send_block.get (txn, send1->hash ()).has_value ());
	ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { key1.pub, 1 }).has_value ());
	ASSERT_FALSE (delegator_exists (*store, txn, { key1.pub, 100, key1.pub }));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));

	ASSERT_FALSE (ledger.rollback (txn, send1->hash ()));
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	ASSERT_FALSE (store->extended.account_block_by_height.get (txn, { nano::dev::genesis_key.pub, 2 }).has_value ());
	ASSERT_EQ (1, store->extended.account_delegator_by_weight.count (txn));
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount, nano::dev::genesis_key.pub }));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
}

/*
 * Rolling back a legacy send cascades through the dependent open block;
 * all index entries of both blocks must be reverted
 */
TEST (ledger_extended_index, rollback_legacy_cascade)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1, key2;
	auto txn = ledger.tx_begin_write ();

	auto send1 = builder.send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.open ()
				 .source (send1->hash ())
				 .representative (key2.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));

	// Rolling back the send rolls back the dependent open first
	ASSERT_FALSE (ledger.rollback (txn, send1->hash ()));
	ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
	ASSERT_EQ (1, store->extended.account_block_by_height.count (txn));
	ASSERT_EQ (1, store->extended.receive_block_by_send_block.count (txn));
	ASSERT_EQ (1, store->extended.account_delegator_by_weight.count (txn));
	ASSERT_TRUE (delegator_exists (*store, txn, { nano::dev::genesis_key.pub, nano::dev::constants.genesis_amount, nano::dev::genesis_key.pub }));
	ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
}

/*
 * The indexed and chain-walking implementations of find_block_hash_by_height must agree for every height,
 * including out-of-range heights and unknown accounts
 */
TEST (ledger_extended_index, find_block_hash_by_height_parity)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1;
	auto txn = ledger.tx_begin_write ();

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 150)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	auto receive1 = builder.state ()
					.account (key1.pub)
					.previous (open1->hash ())
					.representative (key1.pub)
					.balance (150)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive1));

	ASSERT_EQ (send1->hash (), ledger.find_block_hash_by_height (txn, nano::dev::genesis_key.pub, 2).value ());
	ASSERT_EQ (receive1->hash (), ledger.find_block_hash_by_height (txn, key1.pub, 2).value ());

	nano::keypair unknown;
	for (auto const & account : { nano::dev::genesis_key.pub, key1.pub, unknown.pub })
	{
		for (uint64_t height = 0; height <= 4; ++height)
		{
			auto indexed = ledger.find_block_hash_by_height (txn, account, height);
			ledger.flags.account_block_by_height_index = false;
			auto walked = ledger.find_block_hash_by_height (txn, account, height);
			ledger.flags.account_block_by_height_index = true;
			ASSERT_EQ (walked, indexed) << "height " << height << " account " << account.to_account ();
		}
	}
}

/*
 * The indexed and chain-walking implementations of find_receive_block_by_send_hash must agree:
 * null for unreceived sends, null while the receive is uncemented, the receive block once cemented
 */
TEST (ledger_extended_index, find_receive_block_by_send_hash_parity)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::test::make_store (logger, stats);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1;
	auto txn = ledger.tx_begin_write ();

	auto find_both = [&] (nano::account const & destination, nano::block_hash const & send_hash) {
		auto indexed = ledger.find_receive_block_by_send_hash (txn, destination, send_hash);
		ledger.flags.receive_block_by_send_block_index = false;
		auto walked = ledger.find_receive_block_by_send_hash (txn, destination, send_hash);
		ledger.flags.receive_block_by_send_block_index = true;
		EXPECT_TRUE ((indexed == nullptr && walked == nullptr) || (indexed && walked && indexed->hash () == walked->hash ()));
		return indexed;
	};

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));

	// Unreceived send
	ASSERT_EQ (nullptr, find_both (key1.pub, send1->hash ()));

	// Received but not yet cemented
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_EQ (nullptr, find_both (key1.pub, send1->hash ()));

	// Cementing the receive makes it visible through both implementations
	ledger.cement (txn, open1->hash ());
	auto found = find_both (key1.pub, send1->hash ());
	ASSERT_NE (nullptr, found);
	ASSERT_EQ (open1->hash (), found->hash ());
}

/*
 * Dropping the indices must clear the flags, the persisted flags and the tables;
 * reopening with the option afterwards must rebuild everything from scratch
 */
TEST (ledger_extended_index, drop_and_repopulate)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto const path = nano::unique_path ();
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::block_builder builder;
	nano::keypair key1;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
		auto txn = ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
		ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	}
	// Mirror the CLI: the drop runs on a ledger opened without the option
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
		ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());
		ledger.drop_extended_ledger_indices ();
		ASSERT_FALSE (ledger.flags.any_extended_ledger_index_enabled ());
		auto txn = ledger.tx_begin_read ();
		ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled));
		ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled));
		ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled));
		ASSERT_FALSE (store->version.get_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled));
		ASSERT_TRUE (store->extended.account_delegator_by_weight.empty (txn));
		ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
		ASSERT_TRUE (store->extended.receive_block_by_send_block.empty (txn));
		ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
	}
	// Reopening without the option must keep the indices dropped
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
		ASSERT_FALSE (ledger.flags.any_extended_ledger_index_enabled ());
		auto txn = ledger.tx_begin_read ();
		ASSERT_TRUE (store->extended.account_block_by_height.empty (txn));
	}
	// Reopening with the option must repopulate from the ledger contents
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
		ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());
		auto txn = ledger.tx_begin_read ();
		ASSERT_EQ (3, store->extended.account_block_by_height.count (txn));
		ASSERT_EQ (2, store->extended.receive_block_by_send_block.count (txn));
		ASSERT_EQ (2, store->extended.account_delegator_by_weight.count (txn));
		ASSERT_TRUE (store->extended.account_receivable_by_amount.empty (txn));
		ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
	}
}

/*
 * Population must clear any stale rows left in the index tables from a previous partial population
 */
TEST (ledger_extended_index, populate_clears_stale_rows)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto const path = nano::unique_path ();
	nano::account_delegator_by_weight_key stale_delegator{ nano::account{ 1 }, nano::amount{ 2 }, nano::account{ 3 } };
	nano::account_receivable_by_amount_key stale_receivable{ nano::account{ 4 }, nano::amount{ 5 }, nano::block_hash{ 6 } };
	nano::account_block_by_height_key stale_height{ nano::account{ 8 }, 9 };
	nano::block_hash stale_send{ 11 };
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
		ASSERT_FALSE (ledger.flags.any_extended_ledger_index_enabled ());
		// Simulate partially populated leftovers while the index flags are still disabled
		auto txn = store->tx_begin_write ();
		store->extended.account_delegator_by_weight.put (txn, stale_delegator);
		store->extended.account_receivable_by_amount.put (txn, stale_receivable, { nano::account{ 7 }, nano::epoch::epoch_0 });
		store->extended.account_block_by_height.put (txn, stale_height, nano::block_hash{ 10 });
		store->extended.receive_block_by_send_block.put (txn, stale_send, nano::block_hash{ 12 });
	}
	{
		auto store = nano::test::make_store (logger, stats, path);
		nano::ledger ledger (*store, nano::dev::network_params, stats, logger, nano::ledger_options{ .enable_extended_ledger_index = true });
		ASSERT_TRUE (ledger.flags.all_extended_ledger_indices_enabled ());
		auto txn = ledger.tx_begin_read ();
		ASSERT_FALSE (delegator_exists (*store, txn, stale_delegator));
		ASSERT_FALSE (receivable_get (*store, txn, stale_receivable).has_value ());
		ASSERT_FALSE (store->extended.account_block_by_height.get (txn, stale_height).has_value ());
		ASSERT_FALSE (store->extended.receive_block_by_send_block.get (txn, stale_send).has_value ());
		ASSERT_NO_FATAL_FAILURE (assert_extended_indices_consistent (ledger, txn));
	}
}
