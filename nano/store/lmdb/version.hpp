#pragma once

#include <nano/store/version.hpp>

namespace nano
{
namespace lmdb
{
	class store;
	class version_store : public nano::version_store
	{
	protected:
		nano::lmdb::store & store;

	public:
		explicit version_store (nano::lmdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, int version_a) override;
		int get (nano::transaction const & transaction_a) const override;
	};
}
}
