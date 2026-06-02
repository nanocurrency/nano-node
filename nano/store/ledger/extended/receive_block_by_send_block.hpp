#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/backend.hpp>

#include <optional>

namespace nano::store::ledger
{
class receive_block_by_send_block_view
{
public:
	explicit receive_block_by_send_block_view (nano::store::backend &);

	void put (nano::store::write_transaction const &, nano::block_hash const &, nano::block_hash const &);
	std::optional<nano::block_hash> get (nano::store::transaction const &, nano::block_hash const &) const;
	void del (nano::store::write_transaction const &, nano::block_hash const &);
	bool empty (nano::store::transaction const &) const;
	uint64_t count (nano::store::transaction const &) const;
	void clear ();

private:
	nano::store::backend & backend;
};
}
