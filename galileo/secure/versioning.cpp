#include <galileo/secure/versioning.hpp>

galileo::account_info_v1::account_info_v1 () :
head (0),
rep_block (0),
balance (0),
modified (0)
{
}

galileo::account_info_v1::account_info_v1 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

galileo::account_info_v1::account_info_v1 (galileo::block_hash const & head_a, galileo::block_hash const & rep_block_a, galileo::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
balance (balance_a),
modified (modified_a)
{
}

void galileo::account_info_v1::serialize (galileo::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, rep_block.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
}

bool galileo::account_info_v1::deserialize (galileo::stream & stream_a)
{
	auto error (read (stream_a, head.bytes));
	if (!error)
	{
		error = read (stream_a, rep_block.bytes);
		if (!error)
		{
			error = read (stream_a, balance.bytes);
			if (!error)
			{
				error = read (stream_a, modified);
			}
		}
	}
	return error;
}

galileo::mdb_val galileo::account_info_v1::val () const
{
	return galileo::mdb_val (sizeof (*this), const_cast<galileo::account_info_v1 *> (this));
}

galileo::pending_info_v3::pending_info_v3 () :
source (0),
amount (0),
destination (0)
{
}

galileo::pending_info_v3::pending_info_v3 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (source) + sizeof (amount) + sizeof (destination) == sizeof (*this), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

galileo::pending_info_v3::pending_info_v3 (galileo::account const & source_a, galileo::amount const & amount_a, galileo::account const & destination_a) :
source (source_a),
amount (amount_a),
destination (destination_a)
{
}

void galileo::pending_info_v3::serialize (galileo::stream & stream_a) const
{
	galileo::write (stream_a, source.bytes);
	galileo::write (stream_a, amount.bytes);
	galileo::write (stream_a, destination.bytes);
}

bool galileo::pending_info_v3::deserialize (galileo::stream & stream_a)
{
	auto error (galileo::read (stream_a, source.bytes));
	if (!error)
	{
		error = galileo::read (stream_a, amount.bytes);
		if (!error)
		{
			error = galileo::read (stream_a, destination.bytes);
		}
	}
	return error;
}

bool galileo::pending_info_v3::operator== (galileo::pending_info_v3 const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && destination == other_a.destination;
}

galileo::mdb_val galileo::pending_info_v3::val () const
{
	return galileo::mdb_val (sizeof (*this), const_cast<galileo::pending_info_v3 *> (this));
}

galileo::account_info_v5::account_info_v5 () :
head (0),
rep_block (0),
open_block (0),
balance (0),
modified (0)
{
}

galileo::account_info_v5::account_info_v5 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

galileo::account_info_v5::account_info_v5 (galileo::block_hash const & head_a, galileo::block_hash const & rep_block_a, galileo::block_hash const & open_block_a, galileo::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a)
{
}

void galileo::account_info_v5::serialize (galileo::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, rep_block.bytes);
	write (stream_a, open_block.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
}

bool galileo::account_info_v5::deserialize (galileo::stream & stream_a)
{
	auto error (read (stream_a, head.bytes));
	if (!error)
	{
		error = read (stream_a, rep_block.bytes);
		if (!error)
		{
			error = read (stream_a, open_block.bytes);
			if (!error)
			{
				error = read (stream_a, balance.bytes);
				if (!error)
				{
					error = read (stream_a, modified);
				}
			}
		}
	}
	return error;
}

galileo::mdb_val galileo::account_info_v5::val () const
{
	return galileo::mdb_val (sizeof (*this), const_cast<galileo::account_info_v5 *> (this));
}
