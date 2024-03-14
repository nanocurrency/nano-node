#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val_impl.hpp>
#include <nano/store/rocksdb/block.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>

nano::store::rocksdb::unconfirmed_block::unconfirmed_block (::rocksdb::DB & db)
{
	::rocksdb::ColumnFamilyOptions options;
	::rocksdb::ColumnFamilyHandle * handle;
	auto status = db.CreateColumnFamily (options, "unconfirmed_block", &handle);
	release_assert (status.ok ());
	this->handle.reset (handle);
}

auto nano::store::rocksdb::unconfirmed_block::del (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key) -> void
{
	auto status = rocksdb::del (tx, handle.get (), key);
	release_assert (status == 0);
}

auto nano::store::rocksdb::unconfirmed_block::exists (store::unconfirmed_transaction const & tx, nano::block_hash const & key) -> bool
{
	rocksdb::db_val junk;
	return !rocksdb::get (tx, handle.get (), key, junk);
}

auto nano::store::rocksdb::unconfirmed_block::get (store::unconfirmed_transaction const & tx, nano::block_hash const & key) const -> std::shared_ptr<nano::block>
{
	rocksdb::db_val value;
	auto status = rocksdb::get (tx, handle.get (), key, value);
	if (status != 0)
	{
		return nullptr;
	}
	nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
	nano::block_type type;
	auto error = try_read (stream, type);
	release_assert (!error);
	auto result = nano::deserialize_block (stream, type);
	release_assert (result != nullptr);
	nano::block_sideband sideband;
	error = sideband.deserialize (stream, type);
	release_assert (!error);
	result->sideband_set (sideband);
	return result;
}

auto nano::store::rocksdb::unconfirmed_block::put (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key, nano::block const & value) -> void
{
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream (vector);
		nano::serialize_block (stream, value);
		value.sideband ().serialize (stream, value.type ());
	}
	rocksdb::put (tx, handle.get (), key, nano::store::rocksdb::db_val{ vector.size (), (void *)vector.data () });
}
