#include <nano/secure/versioning.hpp>

nano::account_info_v1::account_info_v1 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

nano::account_info_v1::account_info_v1 (nano::block_hash const & head_a, nano::block_hash const & rep_block_a, nano::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
balance (balance_a),
modified (modified_a)
{
}

nano::mdb_val nano::account_info_v1::val () const
{
	return nano::mdb_val (sizeof (*this), const_cast<nano::account_info_v1 *> (this));
}

nano::pending_info_v3::pending_info_v3 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (source) + sizeof (amount) + sizeof (destination) == sizeof (*this), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

nano::pending_info_v3::pending_info_v3 (nano::account const & source_a, nano::amount const & amount_a, nano::account const & destination_a) :
source (source_a),
amount (amount_a),
destination (destination_a)
{
}

nano::mdb_val nano::pending_info_v3::val () const
{
	return nano::mdb_val (sizeof (*this), const_cast<nano::pending_info_v3 *> (this));
}

nano::account_info_v5::account_info_v5 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

nano::account_info_v5::account_info_v5 (nano::block_hash const & head_a, nano::block_hash const & rep_block_a, nano::block_hash const & open_block_a, nano::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a)
{
}

nano::mdb_val nano::account_info_v5::val () const
{
	return nano::mdb_val (sizeof (*this), const_cast<nano::account_info_v5 *> (this));
}
