#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/lmdb/version.hpp>

celerix::store::lmdb::version::version (celerix::store::lmdb::component & store_a) :
	store{ store_a } {};

void celerix::store::lmdb::version::put (store::write_transaction const & transaction_a, int version)
{
	celerix::uint256_union version_key{ 1 };
	celerix::uint256_union version_value (version);
	auto status = store.put (transaction_a, tables::meta, version_key, version_value);
	store.release_assert_success (status);
}

int celerix::store::lmdb::version::get (store::transaction const & transaction_a) const
{
	celerix::uint256_union version_key{ 1 };
	celerix::store::lmdb::db_val data;
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
