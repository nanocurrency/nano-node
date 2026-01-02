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
	nano::uint256_union db_key{ version_key };
	nano::uint256_union db_value{ version };
	auto status = backend.put (txn, nano::store::table::meta, db_key, db_value);
	backend.release_assert_success (status);
}

auto meta_view::get_version (nano::store::transaction const & txn) const -> uint64_t
{
	nano::uint256_union db_key{ version_key };
	nano::store::db_val data;
	auto status = backend.get (txn, nano::store::table::meta, db_key, data);
	uint64_t result = 0; // Default minimum version
	if (backend.success (status))
	{
		nano::uint256_union db_value{ data };
		debug_assert (db_value.qwords[2] == 0 && db_value.qwords[1] == 0 && db_value.qwords[0] == 0);
		result = db_value.number ().convert_to<uint64_t> ();
	}
	return result;
}

bool meta_view::version_exists (nano::store::transaction const & txn) const
{
	nano::uint256_union db_key{ version_key };
	return backend.exists (txn, nano::store::table::meta, db_key);
}
}
