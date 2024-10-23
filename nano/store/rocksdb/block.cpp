#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val_impl.hpp>
#include <nano/store/rocksdb/block.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/utility.hpp>

namespace nano
{
/**
 * Fill in our predecessors
 */
class block_predecessor_rocksdb_set : public nano::block_visitor
{
public:
	block_predecessor_rocksdb_set (store::write_transaction const & transaction_a, nano::store::rocksdb::block & block_store_a);
	virtual ~block_predecessor_rocksdb_set () = default;
	void fill_value (nano::block const & block_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	store::write_transaction const & transaction;
	nano::store::rocksdb::block & block_store;
};
}

nano::store::rocksdb::block::block (nano::store::rocksdb::component & store_a) :
	store{ store_a } {};

void nano::store::rocksdb::block::put (store::write_transaction const & transaction, nano::block_hash const & hash, nano::block const & block)
{
	debug_assert (block.sideband ().successor.is_zero () || exists (transaction, block.sideband ().successor));
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream (vector);
		nano::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (transaction, vector, hash);
	block_predecessor_rocksdb_set predecessor (transaction, *this);
	block.visit (predecessor);
	debug_assert (block.previous ().is_zero () || successor (transaction, block.previous ()) == hash);
}

void nano::store::rocksdb::block::raw_put (store::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a)
{
	nano::store::rocksdb::db_val value{ data.size (), (void *)data.data () };
	auto status = store.put (transaction_a, tables::blocks, hash_a, value);
	store.release_assert_success (status);
}

std::optional<nano::block_hash> nano::store::rocksdb::block::successor (store::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	nano::store::rocksdb::db_val value;
	block_raw_get (transaction_a, hash_a, value);
	nano::block_hash result;
	if (value.size () != 0)
	{
		debug_assert (value.size () >= result.bytes.size ());
		auto type = block_type_from_raw (value.data ());
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset (transaction_a, value.size (), type), result.bytes.size ());
		auto error (nano::try_read (stream, result.bytes));
		(void)error;
		debug_assert (!error);
	}
	else
	{
		result.clear ();
	}
	if (result.is_zero ())
	{
		return std::nullopt;
	}
	return result;
}

void nano::store::rocksdb::block::successor_clear (store::write_transaction const & transaction, nano::block_hash const & hash)
{
	nano::store::rocksdb::db_val value;
	block_raw_get (transaction, hash, value);
	debug_assert (value.size () != 0);
	auto type = block_type_from_raw (value.data ());
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
	std::fill_n (data.begin () + block_successor_offset (transaction, value.size (), type), sizeof (nano::block_hash), uint8_t{ 0 });
	raw_put (transaction, data, hash);
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
	if (existing == end (transaction))
	{
		existing = begin (transaction);
	}
	debug_assert (existing != end (transaction));
	return existing->second.block;
}

void nano::store::rocksdb::block::del (store::write_transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status = store.del (transaction_a, tables::blocks, hash_a);
	store.release_assert_success (status);
}

bool nano::store::rocksdb::block::exists (store::transaction const & transaction, nano::block_hash const & hash)
{
	return store.exists (transaction, tables::blocks, hash);
}

uint64_t nano::store::rocksdb::block::count (store::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::blocks);
}

auto nano::store::rocksdb::block::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::begin (store.db.get (), rocksdb::tx (transaction), store.table_to_column_family (tables::blocks)) } };
}

auto nano::store::rocksdb::block::begin (store::transaction const & transaction, nano::block_hash const & hash) const -> iterator
{
	rocksdb::db_val val{ hash };
	return iterator{ store::iterator{ rocksdb::iterator::lower_bound (store.db.get (), rocksdb::tx (transaction), store.table_to_column_family (tables::blocks), val) } };
}

auto nano::store::rocksdb::block::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::end (store.db.get (), rocksdb::tx (transaction_a), store.table_to_column_family (tables::blocks)) } };
}

void nano::store::rocksdb::block::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}

void nano::store::rocksdb::block::block_raw_get (store::transaction const & transaction, nano::block_hash const & hash, nano::store::rocksdb::db_val & value) const
{
	auto status = store.get (transaction, tables::blocks, hash, value);
	release_assert (store.success (status) || store.not_found (status));
}

size_t nano::store::rocksdb::block::block_successor_offset (store::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const
{
	return entry_size_a - nano::block_sideband::size (type_a);
}

nano::block_type nano::store::rocksdb::block::block_type_from_raw (void * data_a)
{
	// The block type is the first byte
	return static_cast<nano::block_type> ((reinterpret_cast<uint8_t const *> (data_a))[0]);
}

nano::block_predecessor_rocksdb_set::block_predecessor_rocksdb_set (store::write_transaction const & transaction_a, nano::store::rocksdb::block & block_store_a) :
	transaction{ transaction_a },
	block_store{ block_store_a }
{
}

void nano::block_predecessor_rocksdb_set::fill_value (nano::block const & block_a)
{
	auto hash = block_a.hash ();
	nano::store::rocksdb::db_val value;
	block_store.block_raw_get (transaction, block_a.previous (), value);
	debug_assert (value.size () != 0);
	auto type = block_store.block_type_from_raw (value.data ());
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
	std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + block_store.block_successor_offset (transaction, value.size (), type));
	block_store.raw_put (transaction, data, block_a.previous ());
}

void nano::block_predecessor_rocksdb_set::send_block (nano::send_block const & block_a)
{
	fill_value (block_a);
}

void nano::block_predecessor_rocksdb_set::receive_block (nano::receive_block const & block_a)
{
	fill_value (block_a);
}

void nano::block_predecessor_rocksdb_set::open_block (nano::open_block const & block_a)
{
	// Open blocks don't have a predecessor
}

void nano::block_predecessor_rocksdb_set::change_block (nano::change_block const & block_a)
{
	fill_value (block_a);
}

void nano::block_predecessor_rocksdb_set::state_block (nano::state_block const & block_a)
{
	if (!block_a.previous ().is_zero ())
	{
		fill_value (block_a);
	}
}
