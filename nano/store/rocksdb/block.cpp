#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/rocksdb/block.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>

nano::store::rocksdb::block::block (nano::store::rocksdb::component & store_a) :
	store{ store_a } {};

void nano::store::rocksdb::block::put (store::write_transaction const & transaction, nano::block_hash const & hash, nano::block const & block)
{
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream (vector);
		nano::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (transaction, vector, hash);
}

void nano::store::rocksdb::block::raw_put (store::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a)
{
	nano::store::rocksdb::db_val value{ data.size (), (void *)data.data () };
	auto status = store.put (transaction_a, tables::blocks, hash_a, value);
	store.release_assert_success (status);
}

std::shared_ptr<nano::block> nano::store::rocksdb::block::get (store::transaction const & transaction, nano::block_hash const & hash) const
{
	nano::store::rocksdb::db_val value;
	block_raw_get (transaction, hash, value);
	std::shared_ptr<nano::block> result;
	if (value.size () != 0)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		nano::block_type type;
		auto error (try_read (stream, type));
		release_assert (!error);
		result = nano::deserialize_block (stream, type);
		release_assert (result != nullptr);
		nano::block_sideband sideband;
		error = (sideband.deserialize (stream, type));
		release_assert (!error);
		result->sideband_set (sideband);
	}
	return result;
}
std::shared_ptr<nano::block> nano::store::rocksdb::block::random (store::transaction const & transaction)
{
	nano::block_hash hash;
	nano::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
	auto existing = begin (transaction, hash);
	if (existing == end ())
	{
		existing = begin (transaction);
	}
	debug_assert (existing != end ());
	return existing->second.block;
}

void nano::store::rocksdb::block::del (store::write_transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status = store.del (transaction_a, tables::blocks, hash_a);
	store.release_assert_success (status);
}

bool nano::store::rocksdb::block::exists (store::transaction const & transaction, nano::block_hash const & hash)
{
	nano::store::rocksdb::db_val junk;
	block_raw_get (transaction, hash, junk);
	return junk.size () != 0;
}

uint64_t nano::store::rocksdb::block::count (store::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::blocks);
}
nano::store::iterator<nano::block_hash, nano::store::block_w_sideband> nano::store::rocksdb::block::begin (store::transaction const & transaction) const
{
	return store.make_iterator<nano::block_hash, nano::store::block_w_sideband> (transaction, tables::blocks);
}

nano::store::iterator<nano::block_hash, nano::store::block_w_sideband> nano::store::rocksdb::block::begin (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return store.make_iterator<nano::block_hash, nano::store::block_w_sideband> (transaction, tables::blocks, hash);
}

nano::store::iterator<nano::block_hash, nano::store::block_w_sideband> nano::store::rocksdb::block::end () const
{
	return store::iterator<nano::block_hash, nano::store::block_w_sideband> (nullptr);
}

void nano::store::rocksdb::block::for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::block_hash, block_w_sideband>, store::iterator<nano::block_hash, block_w_sideband>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}

void nano::store::rocksdb::block::block_raw_get (store::transaction const & transaction, nano::block_hash const & hash, nano::store::rocksdb::db_val & value) const
{
	auto status = store.get (transaction, tables::blocks, hash, value);
	release_assert (store.success (status) || store.not_found (status));
}

nano::block_type nano::store::rocksdb::block::block_type_from_raw (void * data_a)
{
	// The block type is the first byte
	return static_cast<nano::block_type> ((reinterpret_cast<uint8_t const *> (data_a))[0]);
}
