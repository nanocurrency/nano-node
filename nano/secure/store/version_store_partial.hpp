#pragma once

#include <nano/secure/blockstore_partial.hpp>

namespace nano
{
template <typename Val, typename Derived_Store>
class block_store_partial;

template <typename Val, typename Derived_Store>
class version_store_partial : public version_store
{
protected:
	nano::block_store_partial<Val, Derived_Store> & block_store;

public:
	explicit version_store_partial (nano::block_store_partial<Val, Derived_Store> & block_store_a) :
		block_store (block_store_a){};

	void put (nano::write_transaction const & transaction_a, int version_a) override
	{
		nano::uint256_union version_key (1);
		nano::uint256_union version_value (version_a);
		auto status (block_store.put (transaction_a, tables::meta, nano::db_val<Val> (version_key), nano::db_val<Val> (version_value)));
		release_assert_success (block_store, status);
	}

	int get (nano::transaction const & transaction_a) const override
	{
		nano::uint256_union version_key (1);
		nano::db_val<Val> data;
		auto status = block_store.get (transaction_a, tables::meta, nano::db_val<Val> (version_key), data);
		int result (block_store.minimum_version);
		if (block_store.success (status))
		{
			nano::uint256_union version_value (data);
			debug_assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
			result = version_value.number ().convert_to<int> ();
		}
		return result;
	}
};

}
