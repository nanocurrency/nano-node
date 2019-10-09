#include <nano/lib/lambda_visitor.hpp>
#include <nano/node/lmdb/wallet_value.hpp>

template <typename POW>
void nano::wallet_value::deserialize_work (nano::db_val<MDB_val> const & val_a)
{
	static_assert (std::is_same<POW, legacy_pow>::value || std::is_same<POW, nano_pow>::value, "T must be a pow type");
	POW pow;
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (pow), reinterpret_cast<uint8_t *> (&pow));
	work = pow;
}

nano::wallet_value::wallet_value (nano::db_val<MDB_val> const & val_a)
{
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	if (val_a.size () == sizeof (key) + sizeof (nano::legacy_pow))
	{
		deserialize_work<nano::legacy_pow> (val_a);
	}
	else
	{
		assert (val_a.size () == sizeof (key) + sizeof (nano::nano_pow));
		deserialize_work<nano::nano_pow> (val_a);
	}
}

nano::wallet_value::wallet_value (nano::uint256_union const & key_a, nano::proof_of_work const & work_a) :
key (key_a),
work (work_a)
{
}

void nano::wallet_value::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, key.bytes);
	boost::apply_visitor (nano::make_lambda_visitor<void> ([&stream_a](auto const & pow) {
		write (stream_a, pow);
	}),
	work.pow);
}

nano::db_val<MDB_val> nano::wallet_value::val () const
{
	assert (sizeof (*this) == sizeof (key) + work.get_sizeof ()); // "Class not packed"
	return nano::db_val<MDB_val> (sizeof (*this), const_cast<nano::wallet_value *> (this));
}
