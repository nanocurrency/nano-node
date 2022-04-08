#include <nano/node/lmdb/block_store.hpp>

#include <nano/node/lmdb/lmdb.hpp>

namespace nano
{
/**
 * Fill in our predecessors
 */
class block_predecessor_mdb_set : public nano::block_visitor
{
public:
	block_predecessor_mdb_set (nano::write_transaction const & transaction_a, nano::block_store_mdb & block_store_a);
	virtual ~block_predecessor_mdb_set () = default;
	void fill_value (nano::block const & block_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	nano::write_transaction const & transaction;
	nano::block_store_mdb & block_store;
};
}


nano::block_store_mdb::block_store_mdb (nano::mdb_store & store_a) :
	store{ store_a }
{
};

void nano::block_store_mdb::put (nano::write_transaction const & transaction, nano::block_hash const & hash, nano::block const & block)
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

void nano::block_store_mdb::raw_put (nano::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a)
{
	nano::mdb_val value{ data.size (), (void *)data.data () };
	auto status = store.put (transaction_a, tables::blocks, hash_a, value);
	release_assert_success (store, status);
}

nano::block_hash nano::block_store_mdb::successor (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
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

void nano::block_store_mdb::successor_clear (nano::write_transaction const & transaction, nano::block_hash const & hash)
{
	nano::mdb_val value;
	block_raw_get (transaction, hash, value);
	debug_assert (value.size () != 0);
	auto type = block_type_from_raw (value.data ());
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
	std::fill_n (data.begin () + block_successor_offset (transaction, value.size (), type), sizeof (nano::block_hash), uint8_t{ 0 });
	raw_put (transaction, data, hash);
}

std::shared_ptr<nano::block> nano::block_store_mdb::get (nano::transaction const & transaction, nano::block_hash const & hash) const
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

std::shared_ptr<nano::block> nano::block_store_mdb::get_no_sideband (nano::transaction const & transaction, nano::block_hash const & hash) const
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

std::shared_ptr<nano::block> nano::block_store_mdb::random (nano::transaction const & transaction)
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

void nano::block_store_mdb::del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status = store.del (transaction_a, tables::blocks, hash_a);
	release_assert_success (store, status);
}

bool nano::block_store_mdb::exists (nano::transaction const & transaction, nano::block_hash const & hash)
{
	nano::mdb_val junk;
	block_raw_get (transaction, hash, junk);
	return junk.size () != 0;
}

uint64_t nano::block_store_mdb::count (nano::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::blocks);
}

nano::account nano::block_store_mdb::account (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	auto block (get (transaction_a, hash_a));
	debug_assert (block != nullptr);
	return account_calculated (*block);
}

nano::account nano::block_store_mdb::account_calculated (nano::block const & block_a) const
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

nano::store_iterator<nano::block_hash, nano::block_w_sideband> nano::block_store_mdb::begin (nano::transaction const & transaction) const
{
	return static_cast<nano::store_partial<MDB_val, mdb_store> &> (store).template make_iterator<nano::block_hash, nano::block_w_sideband> (transaction, tables::blocks);
}

nano::store_iterator<nano::block_hash, nano::block_w_sideband> nano::block_store_mdb::begin (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	return static_cast<nano::store_partial<MDB_val, mdb_store> &> (store).template make_iterator<nano::block_hash, nano::block_w_sideband> (transaction, tables::blocks, hash);
}

nano::store_iterator<nano::block_hash, nano::block_w_sideband> nano::block_store_mdb::end () const
{
	return nano::store_iterator<nano::block_hash, nano::block_w_sideband> (nullptr);
}

nano::uint128_t nano::block_store_mdb::balance (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto block (get (transaction_a, hash_a));
	release_assert (block);
	nano::uint128_t result (balance_calculated (block));
	return result;
}

nano::uint128_t nano::block_store_mdb::balance_calculated (std::shared_ptr<nano::block> const & block_a) const
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

nano::epoch nano::block_store_mdb::version (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto block = get (transaction_a, hash_a);
	if (block && block->type () == nano::block_type::state)
	{
		return block->sideband ().details.epoch;
	}

	return nano::epoch::epoch_0;
}

void nano::block_store_mdb::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, block_w_sideband>, nano::store_iterator<nano::block_hash, block_w_sideband>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}

// Converts a block hash to a block height
uint64_t nano::block_store_mdb::account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	auto block = get (transaction_a, hash_a);
	return block->sideband ().height;
}

void nano::block_store_mdb::block_raw_get (nano::transaction const & transaction, nano::block_hash const & hash, nano::mdb_val & value) const
{
	auto status = store.get (transaction, tables::blocks, hash, value);
	release_assert (store.success (status) || store.not_found (status));
}

size_t nano::block_store_mdb::block_successor_offset (nano::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const
{
	return entry_size_a - nano::block_sideband::size (type_a);
}

nano::block_type nano::block_store_mdb::block_type_from_raw (void * data_a)
{
	// The block type is the first byte
	return static_cast<nano::block_type> ((reinterpret_cast<uint8_t const *> (data_a))[0]);
}

nano::block_predecessor_mdb_set::block_predecessor_mdb_set (nano::write_transaction const & transaction_a, nano::block_store_mdb & block_store_a) :
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
