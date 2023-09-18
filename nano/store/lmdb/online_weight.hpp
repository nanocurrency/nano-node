#pragma once

#include <nano/store/online_weight.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
namespace lmdb
{
	class online_weight_store : public nano::online_weight_store
	{
	private:
		nano::lmdb::store & store;

	public:
		explicit online_weight_store (nano::lmdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, uint64_t time_a, nano::amount const & amount_a) override;
		void del (nano::write_transaction const & transaction_a, uint64_t time_a) override;
		nano::store_iterator<uint64_t, nano::amount> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<uint64_t, nano::amount> rbegin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<uint64_t, nano::amount> end () const override;
		size_t count (nano::transaction const & transaction_a) const override;
		void clear (nano::write_transaction const & transaction_a) override;

		/**
		 * Samples of online vote weight
		 * uint64_t -> nano::amount
		 */
		MDB_dbi online_weight_handle{ 0 };
	};
}
}
