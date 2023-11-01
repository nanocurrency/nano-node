#include <nano/store/rocksdb/component.hpp>
#include <nano/store/rocksdb/version.hpp>

nano::store::rocksdb::version::version (nano::store::rocksdb::component & store_a) :
	store{ store_a } {};

void nano::store::rocksdb::version::put (store::write_transaction const & transaction_a, int version)
{
	nano::uint256_union version_key{ 1 };
	nano::uint256_union version_value (version);
	auto status = store.put (transaction_a, tables::meta, version_key, version_value);
	store.release_assert_success (status);
}

int nano::store::rocksdb::version::get (store::transaction const & transaction_a) const
{
	nano::uint256_union version_key{ 1 };
	nano::store::rocksdb::db_val data;
	auto status = store.get (transaction_a, tables::meta, version_key, data);
	int result = store.version_minimum;
	if (store.success (status))
	{
		nano::uint256_union version_value{ data };
		debug_assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}
