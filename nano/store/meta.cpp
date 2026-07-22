#include <nano/store/backend.hpp>
#include <nano/store/db_val.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/meta.hpp>

namespace nano::store
{
meta_view::meta_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void meta_view::put_version (nano::store::write_transaction const & txn, uint64_t version)
{
	put_value (txn, meta_key::version, nano::uint256_union{ version });
}

uint64_t meta_view::get_version (nano::store::transaction const & txn) const
{
	auto value = get_value (txn, meta_key::version);
	if (!value)
	{
		return 0;
	}
	debug_assert (value->qwords[2] == 0 && value->qwords[1] == 0 && value->qwords[0] == 0);
	return value->number ().convert_to<uint64_t> ();
}

bool meta_view::version_exists (nano::store::transaction const & txn) const
{
	nano::uint256_union db_key{ static_cast<uint64_t> (meta_key::version) };
	return backend.exists (txn, nano::store::table::meta, db_key);
}

bool meta_view::get_flag (nano::store::transaction const & txn, meta_key key) const
{
	auto value = get_value (txn, key);
	return value && value->number () != 0;
}

void meta_view::put_flag (nano::store::write_transaction const & txn, meta_key key, bool value)
{
	put_value (txn, key, nano::uint256_union{ value ? 1u : 0u });
}

std::optional<nano::uint256_union> meta_view::get_value (nano::store::transaction const & txn, meta_key key) const
{
	nano::uint256_union db_key{ static_cast<uint64_t> (key) };
	nano::store::db_val data;
	auto status = backend.get (txn, nano::store::table::meta, db_key, data);
	if (!backend.success (status))
	{
		return std::nullopt;
	}
	return nano::uint256_union{ data };
}

void meta_view::put_value (nano::store::write_transaction const & txn, meta_key key, nano::uint256_union const & value)
{
	nano::uint256_union db_key{ static_cast<uint64_t> (key) };
	auto status = backend.put (txn, nano::store::table::meta, db_key, value);
	backend.release_assert_success (status);
}
}
