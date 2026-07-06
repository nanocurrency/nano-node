#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/extended/account_block_by_height.hpp>

namespace nano::store::ledger
{
account_block_by_height_view::account_block_by_height_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void account_block_by_height_view::put (nano::store::write_transaction const & txn, nano::account_block_by_height_key const & key, nano::block_hash const & hash)
{
	auto status = backend.put (txn, nano::store::table::account_block_by_height, key, hash);
	backend.release_assert_success (status);
}

std::optional<nano::block_hash> account_block_by_height_view::get (nano::store::transaction const & txn, nano::account_block_by_height_key const & key) const
{
	nano::store::db_val result;
	auto status = backend.get (txn, nano::store::table::account_block_by_height, key, result);
	if (backend.success (status))
	{
		return static_cast<nano::block_hash> (result);
	}
	return std::nullopt;
}

void account_block_by_height_view::del (nano::store::write_transaction const & txn, nano::account_block_by_height_key const & key)
{
	auto status = backend.del (txn, nano::store::table::account_block_by_height, key);
	backend.release_assert_success (status);
}

bool account_block_by_height_view::empty (nano::store::transaction const & txn) const
{
	return backend.empty (txn, nano::store::table::account_block_by_height);
}

uint64_t account_block_by_height_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::account_block_by_height);
}

void account_block_by_height_view::clear ()
{
	auto status = backend.clear (nano::store::table::account_block_by_height);
	backend.release_assert_success (status);
}
}
