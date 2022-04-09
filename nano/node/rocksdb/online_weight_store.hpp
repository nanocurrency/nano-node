#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class rocksdb_store;
namespace rocksdb
{
	class online_weight_store : public nano::online_weight_store
	{
	private:
		nano::rocksdb_store & store;

	public:
		explicit online_weight_store (nano::rocksdb_store & store_a);
		void put (nano::write_transaction const & transaction_a, uint64_t time_a, nano::amount const & amount_a) override;
		void del (nano::write_transaction const & transaction_a, uint64_t time_a) override;
		nano::store_iterator<uint64_t, nano::amount> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<uint64_t, nano::amount> rbegin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<uint64_t, nano::amount> end () const override;
		size_t count (nano::transaction const & transaction_a) const override;
		void clear (nano::write_transaction const & transaction_a) override;
	};
}
}
