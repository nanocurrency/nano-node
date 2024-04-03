#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/rep_weight.hpp>
#include <nano/test_common/make_store.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <iostream>

TEST (rep_weight_store, empty)
{
	auto store = nano::test::make_store ();
	ASSERT_TRUE (!store->init_error ());
	auto txn{ store->tx_begin_read () };
	ASSERT_EQ (0, store->rep_weight.count (txn));
}

TEST (rep_weight_store, add_item)
{
	auto store = nano::test::make_store ();
	ASSERT_TRUE (!store->init_error ());
	auto txn{ store->tx_begin_write () };

	nano::account representative{ 123 };
	nano::uint128_t weight{ 456 };
	store->rep_weight.put (txn, representative, weight);

	ASSERT_EQ (1, store->rep_weight.count (txn));
	ASSERT_EQ (weight, store->rep_weight.get (txn, representative));
}

TEST (rep_weight_store, del)
{
	auto store = nano::test::make_store ();
	ASSERT_TRUE (!store->init_error ());
	auto txn{ store->tx_begin_write () };

	store->rep_weight.put (txn, 1, 100);
	store->rep_weight.put (txn, 2, 200);
	store->rep_weight.put (txn, 3, 300);

	store->rep_weight.del (txn, 2);

	ASSERT_EQ (2, store->rep_weight.count (txn));
	ASSERT_EQ (0, store->rep_weight.get (txn, 200));
}

TEST (rep_weight_store, for_each_par)
{
	auto store = nano::test::make_store ();
	ASSERT_TRUE (!store->init_error ());
	{
		auto txn{ store->tx_begin_write () };
		for (auto i = 0; i < 50; ++i)
		{
			store->rep_weight.put (txn, i, 100);
		}
	}

	std::atomic_size_t rep_total{ 0 };
	std::atomic_size_t weight_total{ 0 };

	store->rep_weight.for_each_par (
	[&rep_total, &weight_total] (auto const &, auto i, auto n) {
		for (; i != n; ++i)
		{
			rep_total.fetch_add (static_cast<std::size_t> (i->first.number ()));
			weight_total.fetch_add (static_cast<std::size_t> (i->second.number ()));
		}
	});

	ASSERT_EQ (1225, rep_total.load ());
	ASSERT_EQ (50 * 100, weight_total.load ());
}
