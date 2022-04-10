#include <nano/node/lmdb/lmdb.hpp>
#include <nano/node/lmdb/version_store.hpp>

nano::lmdb::version_store::version_store (nano::mdb_store & store_a) :
	store{ store_a } {};

void nano::lmdb::version_store::put (nano::write_transaction const & transaction_a, int version)
{
	nano::uint256_union version_key{ 1 };
	nano::uint256_union version_value (version);
	auto status = store.put (transaction_a, tables::meta, version_key, version_value);
	store.release_assert_success (status);
}

int nano::lmdb::version_store::get (nano::transaction const & transaction_a) const
{
	nano::uint256_union version_key{ 1 };
	nano::mdb_val data;
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
