#include <nano/crypto/blake2/blake2.h>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/crypto_lib/secure_memory.hpp>
#include <nano/lib/balance_formatting.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <boost/io/ios_state.hpp>

#include <crypto/ed25519-donna/ed25519.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

namespace
{
char const * account_lookup ("13456789abcdefghijkmnopqrstuwxyz");
char const * account_reverse ("~0~1234567~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~89:;<=>?@AB~CDEFGHIJK~LMNO~~~~~");
char account_encode (uint8_t value)
{
	debug_assert (value < 32);
	auto result (account_lookup[value]);
	return result;
}
uint8_t account_decode (char value)
{
	debug_assert (value >= '0');
	debug_assert (value <= '~');
	auto result (account_reverse[value - 0x30]);
	if (result != '~')
	{
		result -= 0x30;
	}
	return result;
}
}

/*
 * public_key
 */

nano::public_key nano::public_key::from_account (std::string const & text)
{
	nano::public_key result;
	bool error = result.decode_account (text);
	release_assert (!error);
	return result;
}

nano::public_key nano::public_key::from_node_id (std::string const & text)
{
	nano::public_key result;
	bool error = result.decode_node_id (text);
	release_assert (!error);
	return result;
}

void nano::public_key::encode_account (std::ostream & os) const
{
	uint64_t check = 0;

	blake2b_state hash;
	blake2b_init (&hash, 5);
	blake2b_update (&hash, bytes.data (), bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&check), 5);

	nano::uint512_t number_l{ number () };
	number_l <<= 40;
	number_l |= nano::uint512_t{ check };

	// Pre-calculate all characters in reverse order
	std::array<char, 60> encoded{};
	for (auto i (0); i < 60; ++i)
	{
		uint8_t r{ number_l & static_cast<uint8_t> (0x1f) };
		number_l >>= 5;
		encoded[59 - i] = account_encode (r);
	}

	// Write prefix
	os << "nano_";

	// Write encoded characters
	os.write (encoded.data (), encoded.size ());
}

std::string nano::public_key::to_account () const
{
	std::stringstream stream;
	encode_account (stream);
	return stream.str ();
}

nano::public_key const & nano::public_key::null ()
{
	return nano::hardened_constants::get ().not_an_account;
}

std::string nano::public_key::to_node_id () const
{
	std::stringstream stream;
	encode_node_id (stream);
	return stream.str ();
}

void nano::public_key::encode_node_id (std::ostream & os) const
{
	// Same encoding as account but with "node_" prefix instead of "nano_"
	uint64_t check = 0;

	blake2b_state hash;
	blake2b_init (&hash, 5);
	blake2b_update (&hash, bytes.data (), bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&check), 5);

	nano::uint512_t number_l{ number () };
	number_l <<= 40;
	number_l |= nano::uint512_t{ check };

	std::array<char, 60> encoded{};
	for (auto i (0); i < 60; ++i)
	{
		uint8_t r{ number_l & static_cast<uint8_t> (0x1f) };
		number_l >>= 5;
		encoded[59 - i] = account_encode (r);
	}

	os << "node_";
	os.write (encoded.data (), encoded.size ());
}

bool nano::public_key::decode_node_id (std::string const & source_a)
{
	return decode_account (source_a);
}

bool nano::public_key::decode_account (std::string const & source_a)
{
	auto error (source_a.size () < 5);
	if (!error)
	{
		auto xrb_prefix (source_a[0] == 'x' && source_a[1] == 'r' && source_a[2] == 'b' && (source_a[3] == '_' || source_a[3] == '-'));
		auto nano_prefix (source_a[0] == 'n' && source_a[1] == 'a' && source_a[2] == 'n' && source_a[3] == 'o' && (source_a[4] == '_' || source_a[4] == '-'));
		auto node_id_prefix = (source_a[0] == 'n' && source_a[1] == 'o' && source_a[2] == 'd' && source_a[3] == 'e' && source_a[4] == '_');
		error = (xrb_prefix && source_a.size () != 64) || (nano_prefix && source_a.size () != 65);
		if (!error)
		{
			if (xrb_prefix || nano_prefix || node_id_prefix)
			{
				auto i (source_a.begin () + (xrb_prefix ? 4 : 5));
				if (*i == '1' || *i == '3')
				{
					nano::uint512_t number_l;
					for (auto j (source_a.end ()); !error && i != j; ++i)
					{
						uint8_t character (*i);
						error = character < 0x30 || character >= 0x80;
						if (!error)
						{
							uint8_t byte (account_decode (character));
							error = byte == '~';
							if (!error)
							{
								number_l <<= 5;
								number_l += byte;
							}
						}
					}
					if (!error)
					{
						nano::public_key temp = (number_l >> 40).convert_to<nano::uint256_t> ();
						uint64_t check (number_l & static_cast<uint64_t> (0xffffffffff));
						uint64_t validation (0);
						blake2b_state hash;
						blake2b_init (&hash, 5);
						blake2b_update (&hash, temp.bytes.data (), temp.bytes.size ());
						blake2b_final (&hash, reinterpret_cast<uint8_t *> (&validation), 5);
						error = check != validation;
						if (!error)
						{
							*this = temp;
						}
					}
				}
				else
				{
					error = true;
				}
			}
			else
			{
				error = true;
			}
		}
	}
	return error;
}

/*
 * uint256_union
 */

// Construct a uint256_union = AES_ENC_CTR (cleartext, key, iv)
void nano::uint256_union::encrypt (nano::raw_key const & cleartext, nano::raw_key const & key, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
	enc.ProcessData (bytes.data (), cleartext.bytes.data (), sizeof (cleartext.bytes));
}

nano::uint256_union & nano::uint256_union::operator^= (nano::uint256_union const & other_a)
{
	auto j (other_a.qwords.begin ());
	for (auto i (qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j)
	{
		*i ^= *j;
	}
	return *this;
}

nano::uint256_union nano::uint256_union::operator^ (nano::uint256_union const & other_a) const
{
	nano::uint256_union result;
	auto k (result.qwords.begin ());
	for (auto i (qwords.begin ()), j (other_a.qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j, ++k)
	{
		*k = *i ^ *j;
	}
	return result;
}

nano::uint256_union::uint256_union (std::string const & hex_a)
{
	auto error (decode_hex (hex_a));
	release_assert (!error);
}

void nano::uint256_union::encode_hex (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::hex << std::uppercase << std::noshowbase << std::setw (64) << std::setfill ('0');
	stream << number ();
}

bool nano::uint256_union::decode_hex (std::string const & text)
{
	auto error (false);
	if (!text.empty () && text.size () <= 64)
	{
		std::stringstream stream (text);
		stream << std::hex << std::noshowbase;
		nano::uint256_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	else
	{
		error = true;
	}
	return error;
}

void nano::uint256_union::encode_dec (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::dec << std::noshowbase;
	stream << number ();
}

bool nano::uint256_union::decode_dec (std::string const & text)
{
	auto error (text.size () > 78 || (text.size () > 1 && text.front () == '0') || (!text.empty () && text.front () == '-'));
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::dec << std::noshowbase;
		nano::uint256_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

std::string nano::uint256_union::to_string () const
{
	std::stringstream stream;
	encode_hex (stream);
	return stream.str ();
}

std::string nano::uint256_union::to_string_dec () const
{
	std::stringstream stream;
	encode_dec (stream);
	return stream.str ();
}

/*
 * uint512_union
 */

void nano::uint512_union::encode_hex (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::hex << std::uppercase << std::noshowbase << std::setw (128) << std::setfill ('0');
	stream << number ();
}

bool nano::uint512_union::decode_hex (std::string const & text)
{
	auto error (text.size () > 128);
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::hex << std::noshowbase;
		nano::uint512_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

std::string nano::uint512_union::to_string () const
{
	std::stringstream stream;
	encode_hex (stream);
	return stream.str ();
}

/*
 * raw_key
 */

nano::raw_key::~raw_key ()
{
	secure_wipe_memory (bytes.data (), bytes.size ());
}

// This this = AES_DEC_CTR (ciphertext, key, iv)
void nano::raw_key::decrypt (nano::uint256_union const & ciphertext, nano::raw_key const & key_a, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
	dec.ProcessData (bytes.data (), ciphertext.bytes.data (), sizeof (ciphertext.bytes));
}

nano::raw_key nano::deterministic_key (nano::raw_key const & seed_a, uint32_t index_a)
{
	nano::raw_key prv_key;
	blake2b_state hash;
	blake2b_init (&hash, prv_key.bytes.size ());
	blake2b_update (&hash, seed_a.bytes.data (), seed_a.bytes.size ());
	nano::uint256_union index (index_a);
	blake2b_update (&hash, reinterpret_cast<uint8_t *> (&index.dwords[7]), sizeof (uint32_t));
	blake2b_final (&hash, prv_key.bytes.data (), prv_key.bytes.size ());
	return prv_key;
}

nano::public_key nano::pub_key (nano::raw_key const & raw_key_a)
{
	nano::public_key result;
	ed25519_publickey (raw_key_a.bytes.data (), result.bytes.data ());
	return result;
}

nano::signature nano::sign_message (nano::raw_key const & private_key, nano::public_key const & public_key, uint8_t const * data, size_t size)
{
	nano::signature result;
	ed25519_sign (data, size, private_key.bytes.data (), public_key.bytes.data (), result.bytes.data ());
	return result;
}

nano::signature nano::sign_message (nano::raw_key const & private_key, nano::public_key const & public_key, nano::uint256_union const & message)
{
	return nano::sign_message (private_key, public_key, message.bytes.data (), sizeof (message.bytes));
}

bool nano::validate_message (nano::public_key const & public_key, uint8_t const * data, size_t size, nano::signature const & signature)
{
	return 0 != ed25519_sign_open (data, size, public_key.bytes.data (), signature.bytes.data ());
}

bool nano::validate_message (nano::public_key const & public_key, nano::uint256_union const & message, nano::signature const & signature)
{
	return validate_message (public_key, message.bytes.data (), sizeof (message.bytes), signature);
}

/*
 * uint128_union
 */

nano::uint128_union::uint128_union (std::string const & string_a)
{
	auto error (decode_hex (string_a));
	release_assert (!error);
}

void nano::uint128_union::encode_hex (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::hex << std::uppercase << std::noshowbase << std::setw (32) << std::setfill ('0');
	stream << number ();
}

bool nano::uint128_union::decode_hex (std::string const & text)
{
	auto error (text.size () > 32);
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::hex << std::noshowbase;
		nano::uint128_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

void nano::uint128_union::encode_dec (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::dec << std::noshowbase;
	stream << number ();
}

bool nano::uint128_union::decode_dec (std::string const & text, bool decimal)
{
	auto error (text.size () > 39 || (text.size () > 1 && text.front () == '0' && !decimal) || (!text.empty () && text.front () == '-'));
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::dec << std::noshowbase;
		boost::multiprecision::checked_uint128_t number_l;
		try
		{
			stream >> number_l;
			nano::uint128_t unchecked (number_l);
			*this = unchecked;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

bool nano::uint128_union::decode_dec (std::string const & text, nano::uint128_t scale)
{
	bool error (text.size () > 40 || (!text.empty () && text.front () == '-'));
	if (!error)
	{
		auto delimiter_position (text.find (".")); // Dot delimiter hardcoded until decision for supporting other locales
		if (delimiter_position == std::string::npos)
		{
			nano::uint128_union integer;
			error = integer.decode_dec (text);
			if (!error)
			{
				// Overflow check
				try
				{
					auto result (boost::multiprecision::checked_uint128_t (integer.number ()) * boost::multiprecision::checked_uint128_t (scale));
					error = (result > std::numeric_limits<nano::uint128_t>::max ());
					if (!error)
					{
						*this = nano::uint128_t (result);
					}
				}
				catch (std::overflow_error &)
				{
					error = true;
				}
			}
		}
		else
		{
			nano::uint128_union integer_part;
			std::string integer_text (text.substr (0, delimiter_position));
			error = (integer_text.empty () || integer_part.decode_dec (integer_text));
			if (!error)
			{
				// Overflow check
				try
				{
					error = ((boost::multiprecision::checked_uint128_t (integer_part.number ()) * boost::multiprecision::checked_uint128_t (scale)) > std::numeric_limits<nano::uint128_t>::max ());
				}
				catch (std::overflow_error &)
				{
					error = true;
				}
				if (!error)
				{
					nano::uint128_union decimal_part;
					std::string decimal_text (text.substr (delimiter_position + 1, text.length ()));
					error = (decimal_text.empty () || decimal_part.decode_dec (decimal_text, true));
					if (!error)
					{
						// Overflow check
						auto scale_length (scale.convert_to<std::string> ().length ());
						error = (scale_length <= decimal_text.length ());
						if (!error)
						{
							auto base10 = boost::multiprecision::cpp_int (10);
							release_assert ((scale_length - decimal_text.length () - 1) <= std::numeric_limits<unsigned>::max ());
							auto pow10 = boost::multiprecision::pow (base10, static_cast<unsigned> (scale_length - decimal_text.length () - 1));
							auto decimal_part_num = decimal_part.number ();
							auto integer_part_scaled = integer_part.number () * scale;
							auto decimal_part_mult_pow = decimal_part_num * pow10;
							auto result = integer_part_scaled + decimal_part_mult_pow;

							// Overflow check
							error = (result > std::numeric_limits<nano::uint128_t>::max ());
							if (!error)
							{
								*this = nano::uint128_t (result);
							}
						}
					}
				}
			}
		}
	}
	return error;
}

std::string nano::uint128_union::to_string () const
{
	std::stringstream stream;
	encode_hex (stream);
	return stream.str ();
}

std::string nano::uint128_union::to_string_dec () const
{
	std::stringstream stream;
	encode_dec (stream);
	return stream.str ();
}

void nano::uint128_union::encode_balance (std::ostream & os, nano::uint128_t scale, int precision, bool group_digits) const
{
	nano::encode_balance (os, number (), scale, precision, group_digits);
}

std::string nano::uint128_union::format_balance (nano::uint128_t scale, int precision, bool group_digits) const
{
	std::ostringstream stream;
	encode_balance (stream, scale, precision, group_digits);
	return stream.str ();
}

bool nano::hash_or_account::decode_hex (std::string const & text_a)
{
	return raw.decode_hex (text_a);
}

bool nano::hash_or_account::decode_account (std::string const & source_a)
{
	return account.decode_account (source_a);
}

std::string nano::hash_or_account::to_string () const
{
	return raw.to_string ();
}

std::string nano::hash_or_account::to_account () const
{
	return account.to_account ();
}

/*
 *
 */

void nano::encode_balance (std::ostream & os, nano::uint128_t value, nano::uint128_t scale, int precision, bool group_digits)
{
	auto thousands_sep = std::use_facet<std::numpunct<char>> (std::locale ()).thousands_sep ();
	auto decimal_point = std::use_facet<std::numpunct<char>> (std::locale ()).decimal_point ();
	std::string grouping = "\3";
	nano::encode_balance (os, value, scale, precision, group_digits, thousands_sep, decimal_point, grouping);
}

std::string nano::to_string_hex (uint64_t const value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (16) << std::setfill ('0');
	stream << value_a;
	return stream.str ();
}

std::string nano::to_string_hex (uint16_t const value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (4) << std::setfill ('0');
	stream << value_a;
	return stream.str ();
}

bool nano::from_string_hex (std::string const & value_a, uint64_t & target_a)
{
	auto error (value_a.empty ());
	if (!error)
	{
		error = value_a.size () > 16;
		if (!error)
		{
			std::stringstream stream (value_a);
			stream << std::hex << std::noshowbase;
			try
			{
				uint64_t number_l;
				stream >> number_l;
				target_a = number_l;
				if (!stream.eof ())
				{
					error = true;
				}
			}
			catch (std::runtime_error &)
			{
				error = true;
			}
		}
	}
	return error;
}

std::string nano::to_string (double const value_a, int const precision_a)
{
	std::stringstream stream;
	stream << std::setprecision (precision_a) << std::fixed;
	stream << value_a;
	return stream.str ();
}

std::ostream & nano::operator<< (std::ostream & os, const nano::uint128_union & val)
{
	val.encode_hex (os);
	return os;
}

std::ostream & nano::operator<< (std::ostream & os, const nano::uint256_union & val)
{
	val.encode_hex (os);
	return os;
}

std::ostream & nano::operator<< (std::ostream & os, const nano::uint512_union & val)
{
	val.encode_hex (os);
	return os;
}

std::ostream & nano::operator<< (std::ostream & os, const nano::hash_or_account & val)
{
	val.raw.encode_hex (os);
	return os;
}

std::ostream & nano::operator<< (std::ostream & os, const nano::account & val)
{
	val.encode_account (os);
	return os;
}

/*
 *
 */

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4146) // warning C4146: unary minus operator applied to unsigned type, result still unsigned
#endif

uint64_t nano::difficulty::from_multiplier (double const multiplier_a, uint64_t const base_difficulty_a)
{
	if (multiplier_a <= 0.)
	{
		return 0;
	}
	nano::uint128_t reverse_difficulty ((-base_difficulty_a) / multiplier_a);
	if (reverse_difficulty > std::numeric_limits<std::uint64_t>::max ())
	{
		return 0;
	}
	else if (reverse_difficulty != 0 || base_difficulty_a == 0 || multiplier_a < 1.)
	{
		return -(static_cast<uint64_t> (reverse_difficulty));
	}
	else
	{
		return std::numeric_limits<std::uint64_t>::max ();
	}
}

double nano::difficulty::to_multiplier (uint64_t const difficulty_a, uint64_t const base_difficulty_a)
{
	if (difficulty_a == 0)
	{
		return 0;
	}
	return static_cast<double> (-base_difficulty_a) / (-difficulty_a);
}

#ifdef _WIN32
#pragma warning(pop)
#endif
