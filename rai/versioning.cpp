#include <rai/versioning.hpp>

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
	std::copy (reinterpret_cast <uint8_t const *> (val_a.mv_data), reinterpret_cast <uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast <uint8_t *> (this));
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
    auto result (read (stream_a, head.bytes));
    if (!result)
    {
        result = read (stream_a, rep_block.bytes);
        if (!result)
        {
			result = read (stream_a, balance.bytes);
			if (!result)
			{
				result = read (stream_a, modified);
			}
        }
    }
    return result;
}

rai::mdb_val rai::account_info_v1::val () const
{
	return rai::mdb_val (sizeof (*this), const_cast <rai::account_info_v1 *> (this));
}
