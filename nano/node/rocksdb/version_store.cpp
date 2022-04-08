#include <nano/node/rocksdb/rocksdb.hpp>
#include <nano/node/rocksdb/version_store.hpp>

nano::version_store_rocksdb::version_store_rocksdb (nano::rocksdb_store & store_a) :
	store{ store_a } {};

void nano::version_store_rocksdb::put (nano::write_transaction const & transaction_a, int version)
{
	nano::uint256_union version_key{ 1 };
	nano::uint256_union version_value (version);
	auto status = store.put (transaction_a, tables::meta, version_key, version_value);
	release_assert_success (store, status);
}

int nano::version_store_rocksdb::get (nano::transaction const & transaction_a) const
{
	nano::uint256_union version_key{ 1 };
	nano::rocksdb_val data;
	auto status = store.get (transaction_a, tables::meta, version_key, data);
	int result = store.minimum_version;
	if (store.success (status))
	{
		nano::uint256_union version_value{ data };
		debug_assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}
