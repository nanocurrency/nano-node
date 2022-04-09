#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class rocksdb_store;
namespace rocksdb
{
	class version_store : public nano::version_store
	{
	protected:
		nano::rocksdb_store & store;

	public:
		explicit version_store (nano::rocksdb_store & store_a);
		void put (nano::write_transaction const & transaction_a, int version_a) override;
		int get (nano::transaction const & transaction_a) const override;
	};
}
}
