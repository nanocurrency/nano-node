#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/extended/account_block_by_height_key.hpp>
#include <nano/store/backend.hpp>

#include <optional>

namespace nano::store::ledger
{
class account_block_by_height_view
{
public:
	explicit account_block_by_height_view (nano::store::backend &);

	void put (nano::store::write_transaction const &, nano::account_block_by_height_key const &, nano::block_hash const &);
	std::optional<nano::block_hash> get (nano::store::transaction const &, nano::account_block_by_height_key const &) const;
	void del (nano::store::write_transaction const &, nano::account_block_by_height_key const &);
	bool empty (nano::store::transaction const &) const;
	uint64_t count (nano::store::transaction const &) const;
	void clear ();

private:
	nano::store::backend & backend;
};
}
