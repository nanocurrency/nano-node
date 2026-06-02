#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/extended/receive_block_by_send_block.hpp>

namespace nano::store::ledger
{
receive_block_by_send_block_view::receive_block_by_send_block_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void receive_block_by_send_block_view::put (nano::store::write_transaction const & txn, nano::block_hash const & send_block_hash, nano::block_hash const & receive_block_hash)
{
	auto status = backend.put (txn, nano::store::table::receive_block_by_send_block, send_block_hash, receive_block_hash);
	backend.release_assert_success (status);
}

std::optional<nano::block_hash> receive_block_by_send_block_view::get (nano::store::transaction const & txn, nano::block_hash const & send_block_hash) const
{
	nano::store::db_val result;
	auto status = backend.get (txn, nano::store::table::receive_block_by_send_block, send_block_hash, result);
	if (backend.success (status))
	{
		return static_cast<nano::block_hash> (result);
	}
	return std::nullopt;
}

void receive_block_by_send_block_view::del (nano::store::write_transaction const & txn, nano::block_hash const & send_block_hash)
{
	auto status = backend.del (txn, nano::store::table::receive_block_by_send_block, send_block_hash);
	backend.release_assert_success (status);
}

bool receive_block_by_send_block_view::empty (nano::store::transaction const & txn) const
{
	return backend.empty (txn, nano::store::table::receive_block_by_send_block);
}

uint64_t receive_block_by_send_block_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::receive_block_by_send_block);
}

void receive_block_by_send_block_view::clear ()
{
	auto status = backend.clear (nano::store::table::receive_block_by_send_block);
	backend.release_assert_success (status);
}
}
