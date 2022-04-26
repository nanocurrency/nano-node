#include <nano/node/lmdb/block_store.hpp>
#include <nano/node/lmdb/lmdb.hpp>
#include <nano/secure/parallel_traversal.hpp>

namespace nano
{
/**
 * Fill in our predecessors
 */
class block_predecessor_mdb_set : public nano::block_visitor
{
public:
	block_predecessor_mdb_set (nano::write_transaction const & transaction_a, nano::lmdb::block_store & block_store_a);
	virtual ~block_predecessor_mdb_set () = default;
	void fill_value (nano::block const & block_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	nano::write_transaction const & transaction;
	nano::lmdb::block_store & block_store;
};
}

nano::lmdb::block_store::block_store (nano::lmdb::store & store_a) :
	store{ store_a },
	last_block_index{}
{
}

void nano::lmdb::block_store::put (nano::write_transaction const & transaction, nano::block_hash const & hash, nano::block const & block)
{
	debug_assert (block.sideband ().successor.is_zero () || exists (transaction, block.sideband ().successor));
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

void nano::lmdb::block_store::raw_put (nano::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a)
{
	// check to see if hash_a already exists and if so, replace its corresponding contents, otherwise insert new
	nano::mdb_val index{};
	auto status = block_index_get (transaction_a, hash_a, index);
	if (store.not_found (status))
	{
		const auto new_index = ++last_block_index;
		index = nano::mdb_val{ sizeof (uint64_t), const_cast<void *> (reinterpret_cast<const void *> (&new_index)) };
		status = store.put (transaction_a, tables::block_indexes, hash_a, index);
		store.release_assert_success (status);
	}

	nano::mdb_val contents_value{ data.size (), (void *)data.data () };
	status = store.put (transaction_a, tables::block_contents, index, contents_value, MDB_APPEND);
	store.release_assert_success (status);
}

nano::block_hash nano::lmdb::block_store::successor (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	nano::mdb_val value;
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
	return result;
}

void nano::lmdb::block_store::successor_clear (nano::write_transaction const & transaction, nano::block_hash const & hash)
{
	nano::mdb_val value;
	block_raw_get (transaction, hash, value);
	debug_assert (value.size () != 0);
	auto type = block_type_from_raw (value.data ());
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
	std::fill_n (data.begin () + block_successor_offset (transaction, value.size (), type), sizeof (nano::block_hash), uint8_t{ 0 });
	raw_put (transaction, data, hash);
}

std::shared_ptr<nano::block> nano::lmdb::block_store::get (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	nano::mdb_val value;
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

std::shared_ptr<nano::block> nano::lmdb::block_store::get_no_sideband (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	nano::mdb_val value;
	block_raw_get (transaction, hash, value);
	std::shared_ptr<nano::block> result;
	if (value.size () != 0)
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = nano::deserialize_block (stream);
		debug_assert (result != nullptr);
	}
	return result;
}

std::shared_ptr<nano::block> nano::lmdb::block_store::random (nano::transaction const & transaction)
{
	nano::block_hash hash{};
	nano::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
	auto existing = begin (transaction, hash);
	if (existing == end ())
	{
		existing = begin (transaction);
	}
	debug_assert (existing != end ());
	return existing->second.block;
}

void nano::lmdb::block_store::del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a)
{
	nano::mdb_val index{};
	auto status = block_index_get (transaction_a, hash_a, index);

	// TODO: is it an error if the hash could not be found in the block_indexes table?
	if (!store.not_found (status))
	{
		status = store.del (transaction_a, tables::block_contents, index);
		store.release_assert_success (status);

		status = store.del (transaction_a, tables::block_indexes, hash_a);
		store.release_assert_success (status);
	}
}

bool nano::lmdb::block_store::exists (nano::transaction const & transaction, nano::block_hash const & hash)
{
	nano::mdb_val junk{};
	return !store.not_found (block_index_get (transaction, hash, junk));
}

uint64_t nano::lmdb::block_store::count (nano::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::block_indexes);
}

nano::account nano::lmdb::block_store::account (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	auto block (get (transaction_a, hash_a));
	debug_assert (block != nullptr);
	return account_calculated (*block);
}

nano::account nano::lmdb::block_store::account_calculated (nano::block const & block_a) const
{
	debug_assert (block_a.has_sideband ());
	nano::account result (block_a.account ());
	if (result.is_zero ())
	{
		result = block_a.sideband ().account;
	}
	debug_assert (!result.is_zero ());
	return result;
}

nano::store_iterator<nano::block_hash, nano::block_w_sideband> nano::lmdb::block_store::begin (nano::transaction const & transaction) const
{
	return store.make_block_iterator (transaction);
}

nano::store_iterator<nano::block_hash, nano::block_w_sideband> nano::lmdb::block_store::begin (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	return store.make_block_iterator (transaction, hash);
}

nano::store_iterator<nano::block_hash, nano::block_w_sideband> nano::lmdb::block_store::end () const
{
	return nano::store_iterator<nano::block_hash, nano::block_w_sideband> (nullptr);
}

void nano::lmdb::block_store::set_last_block_index (nano::transaction const & transaction_a)
{
	std::uint64_t result{};

	const auto end = indexes_end ();
	for (auto itr = indexes_begin (transaction_a); itr != end; ++itr)
	{
		result = std::max (result, itr->second);
	}

	last_block_index = result;
}

nano::uint128_t nano::lmdb::block_store::balance (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto block (get (transaction_a, hash_a));
	release_assert (block);
	nano::uint128_t result (balance_calculated (block));
	return result;
}

nano::uint128_t nano::lmdb::block_store::balance_calculated (std::shared_ptr<nano::block> const & block_a) const
{
	nano::uint128_t result;
	switch (block_a->type ())
	{
		case nano::block_type::open:
		case nano::block_type::receive:
		case nano::block_type::change:
			result = block_a->sideband ().balance.number ();
			break;
		case nano::block_type::send:
			result = boost::polymorphic_downcast<nano::send_block *> (block_a.get ())->hashables.balance.number ();
			break;
		case nano::block_type::state:
			result = boost::polymorphic_downcast<nano::state_block *> (block_a.get ())->hashables.balance.number ();
			break;
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			release_assert (false);
			break;
	}
	return result;
}

nano::epoch nano::lmdb::block_store::version (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto block = get (transaction_a, hash_a);
	if (block && block->type () == nano::block_type::state)
	{
		return block->sideband ().details.epoch;
	}

	return nano::epoch::epoch_0;
}

void nano::lmdb::block_store::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, block_w_sideband>, nano::store_iterator<nano::block_hash, block_w_sideband>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}

// Converts a block hash to a block height
uint64_t nano::lmdb::block_store::account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	auto block = get (transaction_a, hash_a);
	return block->sideband ().height;
}

void nano::lmdb::block_store::block_raw_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::mdb_val & value) const
{
	nano::mdb_val index{};
	auto status = block_index_get (transaction_a, hash_a, index);
	if (!store.not_found (status))
	{
		status = store.get (transaction_a, tables::block_contents, index, value);
		store.release_assert_success (status);
	}
}

int nano::lmdb::block_store::block_index_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::mdb_val & value) const
{
	const auto status = store.get (transaction_a, tables::block_indexes, hash_a, value);
	release_assert (store.success (status) || store.not_found (status));

	return status;
}

nano::store_iterator<nano::block_hash, std::uint64_t> nano::lmdb::block_store::indexes_begin (nano::transaction const & transaction_a) const
{
	return store.make_iterator<nano::block_hash, std::uint64_t> (transaction_a, tables::block_indexes);
}

nano::store_iterator<nano::block_hash, std::uint64_t> nano::lmdb::block_store::indexes_end () const
{
	return nano::store_iterator<nano::block_hash, std::uint64_t> (nullptr);
}

size_t nano::lmdb::block_store::block_successor_offset (nano::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const
{
	return entry_size_a - nano::block_sideband::size (type_a);
}

nano::block_type nano::lmdb::block_store::block_type_from_raw (void * data_a)
{
	// The block type is the first byte
	return static_cast<nano::block_type> ((reinterpret_cast<uint8_t const *> (data_a))[0]);
}

nano::block_predecessor_mdb_set::block_predecessor_mdb_set (nano::write_transaction const & transaction_a, nano::lmdb::block_store & block_store_a) :
	transaction{ transaction_a },
	block_store{ block_store_a }
{
}
void nano::block_predecessor_mdb_set::fill_value (nano::block const & block_a)
{
	auto hash = block_a.hash ();
	nano::mdb_val value;
	block_store.block_raw_get (transaction, block_a.previous (), value);
	debug_assert (value.size () != 0);
	auto type = block_store.block_type_from_raw (value.data ());
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
	std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + block_store.block_successor_offset (transaction, value.size (), type));
	block_store.raw_put (transaction, data, block_a.previous ());
}
void nano::block_predecessor_mdb_set::send_block (nano::send_block const & block_a)
{
	fill_value (block_a);
}
void nano::block_predecessor_mdb_set::receive_block (nano::receive_block const & block_a)
{
	fill_value (block_a);
}
void nano::block_predecessor_mdb_set::open_block (nano::open_block const & block_a)
{
	// Open blocks don't have a predecessor
}
void nano::block_predecessor_mdb_set::change_block (nano::change_block const & block_a)
{
	fill_value (block_a);
}
void nano::block_predecessor_mdb_set::state_block (nano::state_block const & block_a)
{
	if (!block_a.previous ().is_zero ())
	{
		fill_value (block_a);
	}
}
