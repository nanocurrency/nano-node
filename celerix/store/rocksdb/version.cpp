#include <celerix/store/rocksdb/rocksdb.hpp>
#include <celerix/store/rocksdb/version.hpp>

celerix::store::rocksdb::version::version (celerix::store::rocksdb::component & store_a) :
	store{ store_a } {};

void celerix::store::rocksdb::version::put (store::write_transaction const & transaction_a, int version)
{
	celerix::uint256_union version_key{ 1 };
	celerix::uint256_union version_value (version);
	auto status = store.put (transaction_a, tables::meta, version_key, version_value);
	store.release_assert_success (status);
}

int celerix::store::rocksdb::version::get (store::transaction const & transaction_a) const
{
	celerix::uint256_union version_key{ 1 };
	celerix::store::rocksdb::db_val data;
	auto status = store.get (transaction_a, tables::meta, version_key, data);
	int result = store.version_minimum;
	if (store.success (status))
	{
		celerix::uint256_union version_value{ data };
		debug_assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}
