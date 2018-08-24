#include <banano/secure/versioning.hpp>

rai::account_info_v1::account_info_v1 () :
head (0),
rep_block (0),
balance (0),
modified (0)
{
}

rai::account_info_v1::account_info_v1 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

rai::account_info_v1::account_info_v1 (rai::block_hash const & head_a, rai::block_hash const & rep_block_a, rai::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
balance (balance_a),
modified (modified_a)
{
}

void rai::account_info_v1::serialize (rai::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, rep_block.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
}

bool rai::account_info_v1::deserialize (rai::stream & stream_a)
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

rai::mdb_val rai::account_info_v1::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast<rai::account_info_v1 *> (this));
}

rai::pending_info_v3::pending_info_v3 () :
source (0),
amount (0),
destination (0)
{
}

rai::pending_info_v3::pending_info_v3 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (source) + sizeof (amount) + sizeof (destination) == sizeof (*this), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

rai::pending_info_v3::pending_info_v3 (rai::account const & source_a, rai::amount const & amount_a, rai::account const & destination_a) :
source (source_a),
amount (amount_a),
destination (destination_a)
{
}

void rai::pending_info_v3::serialize (rai::stream & stream_a) const
{
	rai::write (stream_a, source.bytes);
	rai::write (stream_a, amount.bytes);
	rai::write (stream_a, destination.bytes);
}

bool rai::pending_info_v3::deserialize (rai::stream & stream_a)
{
	auto error (rai::read (stream_a, source.bytes));
	if (!error)
	{
		error = rai::read (stream_a, amount.bytes);
		if (!error)
		{
			error = rai::read (stream_a, destination.bytes);
		}
	}
	return error;
}

bool rai::pending_info_v3::operator== (rai::pending_info_v3 const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && destination == other_a.destination;
}

rai::mdb_val rai::pending_info_v3::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast<rai::pending_info_v3 *> (this));
}

rai::account_info_v5::account_info_v5 () :
head (0),
rep_block (0),
open_block (0),
balance (0),
modified (0)
{
}

rai::account_info_v5::account_info_v5 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

rai::account_info_v5::account_info_v5 (rai::block_hash const & head_a, rai::block_hash const & rep_block_a, rai::block_hash const & open_block_a, rai::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a)
{
}

void rai::account_info_v5::serialize (rai::stream & stream_a) const
{
	write (stream_a, head.bytes);
	write (stream_a, rep_block.bytes);
	write (stream_a, open_block.bytes);
	write (stream_a, balance.bytes);
	write (stream_a, modified);
}

bool rai::account_info_v5::deserialize (rai::stream & stream_a)
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

rai::mdb_val rai::account_info_v5::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast<rai::account_info_v5 *> (this));
}
