#include <nano/node/lmdb/version_store.hpp>

#include <nano/node/lmdb/lmdb.hpp>

nano::version_store_mdb::version_store_mdb (nano::mdb_store & store_a) :
	store{ store_a }
{
};

void nano::version_store_mdb::put (nano::write_transaction const & transaction_a, int version)
{
	nano::uint256_union version_key{ 1 };
	nano::uint256_union version_value(version);
	auto status = store.put (transaction_a, tables::meta, version_key, version_value);
	release_assert_success (store, status);
}

int nano::version_store_mdb::get (nano::transaction const & transaction_a) const
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
