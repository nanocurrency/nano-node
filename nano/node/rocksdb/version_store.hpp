#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
namespace rocksdb
{
	class store;
	class version_store : public nano::version_store
	{
	protected:
		nano::rocksdb::store & store;

	public:
		explicit version_store (nano::rocksdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, int version_a) override;
		int get (nano::transaction const & transaction_a) const override;
	};
}
}
