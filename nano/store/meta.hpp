#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/fwd.hpp>

#include <cstdint>
#include <optional>

namespace nano::store
{
enum class meta_key : uint64_t
{
	version = 1,
	topo_index_enabled = 8,
	receive_block_by_send_block_index_enabled = 9,
	account_receivable_by_amount_index_enabled = 10,
};

class meta_view
{
public:
	explicit meta_view (nano::store::backend &);

	void put_version (nano::store::write_transaction const &, uint64_t version);
	uint64_t get_version (nano::store::transaction const &) const;
	bool version_exists (nano::store::transaction const &) const;

	bool get_flag (nano::store::transaction const &, nano::store::meta_key) const;
	void put_flag (nano::store::write_transaction const &, nano::store::meta_key, bool value);

private:
	std::optional<nano::uint256_union> get_value (nano::store::transaction const &, nano::store::meta_key) const;
	void put_value (nano::store::write_transaction const &, nano::store::meta_key, nano::uint256_union const & value);

	nano::store::backend & backend;
};
}
