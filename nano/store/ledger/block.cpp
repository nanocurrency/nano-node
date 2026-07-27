#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/successor.hpp>

namespace nano::store::ledger
{
block_view::block_view (nano::store::backend & backend_a, nano::store::ledger::successor_view & successor_store_a) :
	backend{ backend_a },
	successor_store{ successor_store_a }
{
}

void block_view::put (nano::store::write_transaction const & txn, nano::block_hash const & hash, nano::block const & block)
{
	release_assert (block.hash () == hash);
	std::vector<uint8_t> vector;
	{
		nano::vectorstream stream{ vector };
		nano::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (txn, vector, hash);
	if (!block.previous ().is_zero ())
	{
		successor_store.put (txn, block.previous (), hash);
	}
	debug_assert (block.previous ().is_zero () || successor_store.get (txn, block.previous ()) == hash);
}

void block_view::raw_put (nano::store::write_transaction const & txn, std::vector<uint8_t> const & data, nano::block_hash const & hash)
{
	nano::store::db_val value{ data.size (), (void *)data.data () };
	auto status = backend.put (txn, nano::store::table::blocks, hash, value);
	backend.release_assert_success (status);
}

std::shared_ptr<nano::block> block_view::get (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	nano::store::db_val value;
	block_raw_get (txn, hash, value);
	std::shared_ptr<nano::block> result;
	if (value.size () != 0)
	{
		nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		nano::block_type type;
		bool error = try_read (stream, type);
		release_assert (!error);
		result = nano::deserialize_block (stream, type);
		release_assert (result != nullptr);
		nano::block_sideband sideband;
		error = sideband.deserialize (stream, type);
		release_assert (!error);
		result->sideband_set (sideband);
	}
	return result;
}

void block_view::del (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	auto status = backend.del (txn, nano::store::table::blocks, hash);
	backend.release_assert_success (status);
}

bool block_view::exists (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	return backend.exists (txn, nano::store::table::blocks, hash);
}

uint64_t block_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::blocks);
}

auto block_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::blocks) };
}

auto block_view::begin (nano::store::transaction const & txn, nano::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::blocks, hash) };
}

auto block_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::blocks) };
}

void block_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint256_t> (
	[&action, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}

void block_view::block_raw_get (nano::store::transaction const & txn, nano::block_hash const & hash, nano::store::db_val & value) const
{
	auto status = backend.get (txn, nano::store::table::blocks, hash, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
}

}
