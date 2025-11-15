#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/lmdb/block.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/utility.hpp>

namespace nano::store::lmdb
{
/**
 * Fill in our predecessors
 */
class block_predecessor_mdb_set : public nano::block_visitor
{
public:
	block_predecessor_mdb_set (store::write_transaction const & transaction_a, nano::store::lmdb::block & block_store_a);
	virtual ~block_predecessor_mdb_set () = default;
	void fill_value (nano::block const & block_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	store::write_transaction const & transaction;
	nano::store::lmdb::block & block_store;
};
}

nano::store::lmdb::block::block (nano::store::lmdb::component & store_a) :
	store{ store_a } {};

void nano::store::lmdb::block::put (store::write_transaction const & transaction, nano::block_hash const & hash, nano::block const & block)
{
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream (vector);
		nano::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (transaction, vector, hash);
	block_predecessor_mdb_set predecessor (transaction, *this);
	block.visit (predecessor);

	debug_assert (block.previous ().is_zero () || successor (transaction, block.previous ()) == hash);
}

void nano::store::lmdb::block::raw_put (store::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a)
{
	nano::store::lmdb::db_val value{ data.size (), (void *)data.data () };
	auto status = store.put (transaction_a, tables::blocks, hash_a, value);
	store.release_assert_success (status);
}

std::optional<nano::block_hash> nano::store::lmdb::block::successor (store::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	nano::store::lmdb::db_val value;
	auto status = store.get (transaction_a, tables::successors, hash_a, value);
	if (store.success (status))
	{
		debug_assert (value.size () == sizeof (nano::block_hash));
		nano::block_hash result;
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (nano::try_read (stream, result.bytes));
		(void)error;
		debug_assert (!error);
		if (!result.is_zero ())
		{
			return result;
		}
	}
	return std::nullopt;
}

void nano::store::lmdb::block::successor_clear (store::write_transaction const & transaction, nano::block_hash const & hash)
{
	auto status = store.del (transaction, tables::successors, hash);
	release_assert (store.success (status) || store.not_found (status), store.error_string (status));
}

std::shared_ptr<nano::block> nano::store::lmdb::block::get (store::transaction const & transaction, nano::block_hash const & hash) const
{
	nano::store::lmdb::db_val value;
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

		auto successor_hash = successor (transaction, hash);
		sideband.successor = successor_hash.value_or (0);
		result->sideband_set (sideband);
	}
	return result;
}

void nano::store::lmdb::block::del (store::write_transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status = store.del (transaction_a, tables::blocks, hash_a);
	store.release_assert_success (status);
	// Also remove from successors table
	auto successor_status = store.del (transaction_a, tables::successors, hash_a);
	release_assert (store.success (successor_status) || store.not_found (successor_status), store.error_string (successor_status));
}

bool nano::store::lmdb::block::exists (store::transaction const & transaction, nano::block_hash const & hash)
{
	return store.exists (transaction, tables::blocks, hash);
}

uint64_t nano::store::lmdb::block::count (store::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::blocks);
}

auto nano::store::lmdb::block::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction), blocks_handle) } };
}

auto nano::store::lmdb::block::begin (store::transaction const & transaction, nano::block_hash const & hash) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction), blocks_handle, to_mdb_val (hash)) } };
}

auto nano::store::lmdb::block::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), blocks_handle) } };
}

void nano::store::lmdb::block::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}

void nano::store::lmdb::block::block_raw_get (store::transaction const & transaction, nano::block_hash const & hash, nano::store::lmdb::db_val & value) const
{
	auto status = store.get (transaction, tables::blocks, hash, value);
	release_assert (store.success (status) || store.not_found (status), store.error_string (status));
}

nano::block_type nano::store::lmdb::block::block_type_from_raw (void * data_a)
{
	// The block type is the first byte
	return static_cast<nano::block_type> ((reinterpret_cast<uint8_t const *> (data_a))[0]);
}

nano::store::lmdb::block_predecessor_mdb_set::block_predecessor_mdb_set (store::write_transaction const & transaction_a, nano::store::lmdb::block & block_store_a) :
	transaction{ transaction_a },
	block_store{ block_store_a }
{
}
void nano::store::lmdb::block_predecessor_mdb_set::fill_value (nano::block const & block_a)
{
	auto hash = block_a.hash ();
	auto previous = block_a.previous ();
	nano::store::lmdb::db_val value{ sizeof (nano::block_hash), (void *)hash.bytes.data () };
	auto status = block_store.store.put (transaction, tables::successors, previous, value);
	release_assert (success (status), error_string (status));
}
void nano::store::lmdb::block_predecessor_mdb_set::send_block (nano::send_block const & block_a)
{
	fill_value (block_a);
}
void nano::store::lmdb::block_predecessor_mdb_set::receive_block (nano::receive_block const & block_a)
{
	fill_value (block_a);
}
void nano::store::lmdb::block_predecessor_mdb_set::open_block (nano::open_block const & block_a)
{
	// Open blocks don't have a predecessor
}
void nano::store::lmdb::block_predecessor_mdb_set::change_block (nano::change_block const & block_a)
{
	fill_value (block_a);
}
void nano::store::lmdb::block_predecessor_mdb_set::state_block (nano::state_block const & block_a)
{
	if (!block_a.previous ().is_zero ())
	{
		fill_value (block_a);
	}
}
