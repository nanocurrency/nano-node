#include <nano/lib/nano_pow.hpp>

nano::nano_pow::nano_pow (nano::legacy_pow pow)
{
	bytes.fill (0);
	for (auto i (bytes.rbegin ()), n (bytes.rend ()); i != n; ++i)
	{
		*i = static_cast<uint8_t> (pow & static_cast<uint8_t> (0xff));
		pow >>= 8;
	}
}

nano::nano_pow::nano_pow (std::array<uint8_t, proof_size> const & bytes_a)
{
	std::memcpy (bytes.data (), &bytes_a, sizeof (bytes));
}

nano::nano_pow::nano_pow (uint96_t pow)
{
	boost::multiprecision::export_bits (pow, bytes.rbegin (), 8, false);
}

nano::uint96_t nano::nano_pow::number () const
{
	uint96_t result;
	boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
	return result;
}

nano::legacy_pow nano::nano_pow::as_legacy () const
{
	using legacy_pow_mp = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<sizeof (nano::legacy_pow) * 8, sizeof (nano::legacy_pow) * 8, boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
	legacy_pow_mp result;
	boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
	return result.convert_to<nano::legacy_pow> ();
}

std::string nano::to_string_hex (nano::nano_pow const & value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (value_a.bytes.size () * 2) << std::setfill ('0') << value_a;
	return stream.str ();
}

bool nano::nano_pow::operator== (nano::nano_pow const & other_a) const
{
	return bytes == other_a.bytes;
}

nano::nano_pow & nano::nano_pow::operator++ ()
{
	// Could optimize this
	*this = ++number ();
	return *this;
}

std::istream & nano::operator>> (std::istream & input, nano::nano_pow & pow)
{
	nano::uint96_t num;
	input >> num;
	pow = num;
	return input;
}

std::ostream & nano::operator<< (std::ostream & output, nano::nano_pow const & num)
{
	output << num.number ();
	return output;
}
