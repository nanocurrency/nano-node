#pragma once

#include <boost/functional/hash.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <compare>
#include <limits>
#include <ostream>

#include <fmt/ostream.h>

namespace nano
{
using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;

// SI dividers
nano::uint128_t const Knano_ratio = nano::uint128_t ("1000000000000000000000000000000000"); // 10^33 = 1000 nano
nano::uint128_t const nano_ratio = nano::uint128_t ("1000000000000000000000000000000"); // 10^30 = 1 nano
nano::uint128_t const raw_ratio = nano::uint128_t ("1"); // 10^0

class uint128_union
{
public:
	// Type that is implicitly convertible to this union
	using underlying_type = nano::uint128_t;

public:
	uint128_union () = default;
	uint128_union (uint64_t value) :
		uint128_union (nano::uint128_t{ value }){};
	uint128_union (nano::uint128_t const & value)
	{
		bytes.fill (0);
		boost::multiprecision::export_bits (value, bytes.rbegin (), 8, false);
	}

	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	explicit uint128_union (std::string const &);

	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &, bool = false);
	bool decode_dec (std::string const &, nano::uint128_t);

	std::string format_balance (nano::uint128_t scale, int precision, bool group_digits) const;
	std::string format_balance (nano::uint128_t scale, int precision, bool group_digits, std::locale const & locale) const;

	void clear ()
	{
		qwords.fill (0);
	}
	bool is_zero () const
	{
		return qwords[0] == 0 && qwords[1] == 0;
	}

	nano::uint128_t number () const
	{
		nano::uint128_t result;
		boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
		return result;
	}

	std::string to_string () const;
	std::string to_string_dec () const;

public:
	union
	{
		std::array<uint8_t, 16> bytes;
		std::array<char, 16> chars;
		std::array<uint32_t, 4> dwords;
		std::array<uint64_t, 2> qwords;
	};

public: // Keep operators inlined
	std::strong_ordering operator<=> (nano::uint128_union const & other) const
	{
		return std::memcmp (bytes.data (), other.bytes.data (), 16) <=> 0;
	};
	bool operator== (nano::uint128_union const & other) const
	{
		return *this <=> other == 0;
	}
	operator nano::uint128_t () const
	{
		return number ();
	}
	uint128_union const & as_union () const
	{
		return *this;
	}
};
static_assert (std::is_nothrow_move_constructible<uint128_union>::value, "uint128_union should be noexcept MoveConstructible");

// Balances are 128 bit.
class amount : public uint128_union
{
public:
	using uint128_union::uint128_union;

	auto operator<=> (nano::amount const & other) const
	{
		return uint128_union::operator<=> (other);
	}
	operator nano::uint128_t () const
	{
		return number ();
	}
};

class raw_key;

class uint256_union
{
public:
	// Type that is implicitly convertible to this union
	using underlying_type = nano::uint256_t;

public:
	uint256_union () = default;
	uint256_union (uint64_t value) :
		uint256_union (nano::uint256_t{ value }){};
	uint256_union (nano::uint256_t const & value)
	{
		bytes.fill (0);
		boost::multiprecision::export_bits (value, bytes.rbegin (), 8, false);
	}

	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	explicit uint256_union (std::string const &);

	void encrypt (nano::raw_key const &, nano::raw_key const &, uint128_union const &);

	uint256_union & operator^= (uint256_union const &);
	uint256_union operator^ (uint256_union const &) const;

	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);

	void clear ()
	{
		qwords.fill (0);
	}
	bool is_zero () const
	{
		return owords[0].is_zero () && owords[1].is_zero ();
	}

	nano::uint256_t number () const
	{
		nano::uint256_t result;
		boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
		return result;
	}

	std::string to_string () const;

public:
	union
	{
		std::array<uint8_t, 32> bytes;
		std::array<char, 32> chars;
		std::array<uint32_t, 8> dwords;
		std::array<uint64_t, 4> qwords;
		std::array<uint128_union, 2> owords;
	};

public: // Keep operators inlined
	std::strong_ordering operator<=> (nano::uint256_union const & other) const
	{
		return std::memcmp (bytes.data (), other.bytes.data (), 32) <=> 0;
	};
	bool operator== (nano::uint256_union const & other) const
	{
		return *this <=> other == 0;
	}
	operator nano::uint256_t () const
	{
		return number ();
	}
	uint256_union const & as_union () const
	{
		return *this;
	}
};
static_assert (std::is_nothrow_move_constructible<uint256_union>::value, "uint256_union should be noexcept MoveConstructible");

// All keys and hashes are 256 bit.
class block_hash final : public uint256_union
{
public:
	using uint256_union::uint256_union;

public: // Keep operators inlined
	auto operator<=> (nano::block_hash const & other) const
	{
		return uint256_union::operator<=> (other);
	}
	bool operator== (nano::block_hash const & other) const
	{
		return *this <=> other == 0;
	}
	operator nano::uint256_t () const
	{
		return number ();
	}
};

class public_key final : public uint256_union
{
public:
	using uint256_union::uint256_union;

	public_key () :
		uint256_union{ 0 } {};

	static const public_key & null ();

	bool decode_node_id (std::string const &);
	void encode_account (std::string &) const;
	bool decode_account (std::string const &);

	std::string to_node_id () const;
	std::string to_account () const;

public: // Keep operators inlined
	auto operator<=> (nano::public_key const & other) const
	{
		return uint256_union::operator<=> (other);
	}
	bool operator== (nano::public_key const & other) const
	{
		return *this <=> other == 0;
	}
	bool operator== (std::nullptr_t) const
	{
		return *this == null ();
	}
	operator nano::uint256_t () const
	{
		return number ();
	}
};

class wallet_id : public uint256_union
{
	using uint256_union::uint256_union;
};

// These are synonymous
using account = public_key;

class hash_or_account
{
public:
	// Type that is implicitly convertible to this union
	using underlying_type = nano::uint256_t;

public:
	hash_or_account () :
		account{} {};
	hash_or_account (uint64_t value) :
		raw{ value } {};
	hash_or_account (uint256_union const & value) :
		raw{ value } {};

	void clear ()
	{
		raw.clear ();
	}
	bool is_zero () const
	{
		return raw.is_zero ();
	}

	bool decode_hex (std::string const &);
	bool decode_account (std::string const &);

	std::string to_string () const;
	std::string to_account () const;

public:
	union
	{
		std::array<uint8_t, 32> bytes;
		nano::uint256_union raw; // This can be used when you don't want to explicitly mention either of the types
		nano::account account;
		nano::block_hash hash;
	};

public: // Keep operators inlined
	auto operator<=> (nano::hash_or_account const & other) const
	{
		return raw <=> other.raw;
	};
	bool operator== (nano::hash_or_account const & other) const
	{
		return *this <=> other == 0;
	}
	explicit operator nano::uint256_t () const
	{
		return raw.number ();
	}
	explicit operator nano::uint256_union () const
	{
		return raw;
	}
	nano::account const & as_account () const
	{
		return account;
	}
	nano::block_hash const & as_block_hash () const
	{
		return hash;
	}
	nano::uint256_union const & as_union () const
	{
		return raw;
	}
};

// A link can either be a destination account or source hash
class link final : public hash_or_account
{
public:
	using hash_or_account::hash_or_account;

public: // Keep operators inlined
	auto operator<=> (nano::link const & other) const
	{
		return hash_or_account::operator<=> (other);
	}
	bool operator== (nano::link const & other) const
	{
		return *this <=> other == 0;
	}
};

// A root can either be an open block hash or a previous hash
class root final : public hash_or_account
{
public:
	using hash_or_account::hash_or_account;

	nano::block_hash const & previous () const
	{
		return hash;
	}

public: // Keep operators inlined
	auto operator<=> (nano::root const & other) const
	{
		return hash_or_account::operator<=> (other);
	}
	bool operator== (nano::root const & other) const
	{
		return *this <=> other == 0;
	}
};

// The seed or private key
class raw_key final : public uint256_union
{
public:
	using uint256_union::uint256_union;
	~raw_key ();
	void decrypt (nano::uint256_union const &, nano::raw_key const &, uint128_union const &);
};

class uint512_union
{
public:
	// Type that is implicitly convertible to this union
	using underlying_type = nano::uint512_t;

public:
	uint512_union () = default;
	uint512_union (nano::uint512_t const & value)
	{
		bytes.fill (0);
		boost::multiprecision::export_bits (value, bytes.rbegin (), 8, false);
	}
	uint512_union (nano::uint256_union const & upper, nano::uint256_union const & lower) :
		uint256s{ upper, lower } {};

	nano::uint512_union & operator^= (nano::uint512_union const & other)
	{
		uint256s[0] ^= other.uint256s[0];
		uint256s[1] ^= other.uint256s[1];
		return *this;
	}

	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);

	void clear ()
	{
		bytes.fill (0);
	}
	bool is_zero () const
	{
		return uint256s[0].is_zero () && uint256s[1].is_zero ();
	}

	nano::uint512_t number () const
	{
		nano::uint512_t result;
		boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
		return result;
	}

	std::string to_string () const;

public:
	union
	{
		std::array<uint8_t, 64> bytes;
		std::array<uint32_t, 16> dwords;
		std::array<uint64_t, 8> qwords;
		std::array<uint256_union, 2> uint256s;
	};

public: // Keep operators inlined
	std::strong_ordering operator<=> (nano::uint512_union const & other) const
	{
		return std::memcmp (bytes.data (), other.bytes.data (), 64) <=> 0;
	};
	bool operator== (nano::uint512_union const & other) const
	{
		return *this <=> other == 0;
	}
	operator nano::uint512_t () const
	{
		return number ();
	}
	uint512_union const & as_union () const
	{
		return *this;
	}
};
static_assert (std::is_nothrow_move_constructible<uint512_union>::value, "uint512_union should be noexcept MoveConstructible");

class signature : public uint512_union
{
public:
	using uint512_union::uint512_union;
};

class qualified_root : public uint512_union
{
public:
	qualified_root () = default;
	qualified_root (nano::root const & root, nano::block_hash const & previous) :
		uint512_union{ root.as_union (), previous.as_union () } {};
	qualified_root (nano::uint512_t const & value) :
		uint512_union{ value } {};

	nano::root root () const
	{
		return nano::root{ uint256s[0] };
	}
	nano::block_hash previous () const
	{
		return nano::block_hash{ uint256s[1] };
	}
};

nano::signature sign_message (nano::raw_key const &, nano::public_key const &, nano::uint256_union const &);
nano::signature sign_message (nano::raw_key const &, nano::public_key const &, uint8_t const *, size_t);
bool validate_message (nano::public_key const &, nano::uint256_union const &, nano::signature const &);
bool validate_message (nano::public_key const &, uint8_t const *, size_t, nano::signature const &);
nano::raw_key deterministic_key (nano::raw_key const &, uint32_t);
nano::public_key pub_key (nano::raw_key const &);

/* Conversion methods */
std::string to_string_hex (uint64_t const);
std::string to_string_hex (uint16_t const);
bool from_string_hex (std::string const &, uint64_t &);

/* Printing adapters */
std::ostream & operator<< (std::ostream &, const uint128_union &);
std::ostream & operator<< (std::ostream &, const uint256_union &);
std::ostream & operator<< (std::ostream &, const uint512_union &);
std::ostream & operator<< (std::ostream &, const hash_or_account &);

/**
 * Convert a double to string in fixed format
 * @param precision_a (optional) use a specific precision (default is the maximum)
 */
std::string to_string (double const, int const precision_a = std::numeric_limits<double>::digits10);

namespace difficulty
{
	uint64_t from_multiplier (double const, uint64_t const);
	double to_multiplier (uint64_t const, uint64_t const);
}
}

/*
 * Hashing
 */

namespace std
{
template <>
struct hash<::nano::uint128_union>
{
	size_t operator() (::nano::uint128_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1];
	}
};
template <>
struct hash<::nano::uint256_union>
{
	size_t operator() (::nano::uint256_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1] + value.qwords[2] + value.qwords[3];
	}
};
template <>
struct hash<::nano::public_key>
{
	size_t operator() (::nano::public_key const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::block_hash>
{
	size_t operator() (::nano::block_hash const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::hash_or_account>
{
	size_t operator() (::nano::hash_or_account const & value) const noexcept
	{
		return hash<::nano::block_hash>{}(value.as_block_hash ());
	}
};
template <>
struct hash<::nano::root>
{
	size_t operator() (::nano::root const & value) const noexcept
	{
		return hash<::nano::hash_or_account>{}(value);
	}
};
template <>
struct hash<::nano::link>
{
	size_t operator() (::nano::link const & value) const noexcept
	{
		return hash<::nano::hash_or_account>{}(value);
	}
};
template <>
struct hash<::nano::raw_key>
{
	size_t operator() (::nano::raw_key const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::wallet_id>
{
	size_t operator() (::nano::wallet_id const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::uint512_union>
{
	size_t operator() (::nano::uint512_union const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value.uint256s[0]) + hash<::nano::uint256_union> () (value.uint256s[1]);
	}
};
template <>
struct hash<::nano::qualified_root>
{
	size_t operator() (::nano::qualified_root const & value) const noexcept
	{
		return hash<::nano::uint512_union>{}(value);
	}
};
}

namespace boost
{
template <>
struct hash<::nano::uint128_union>
{
	size_t operator() (::nano::uint128_union const & value) const noexcept
	{
		return std::hash<::nano::uint128_union> () (value);
	}
};
template <>
struct hash<::nano::uint256_union>
{
	size_t operator() (::nano::uint256_union const & value) const noexcept
	{
		return std::hash<::nano::uint256_union> () (value);
	}
};
template <>
struct hash<::nano::public_key>
{
	size_t operator() (::nano::public_key const & value) const noexcept
	{
		return std::hash<::nano::public_key> () (value);
	}
};
template <>
struct hash<::nano::block_hash>
{
	size_t operator() (::nano::block_hash const & value) const noexcept
	{
		return std::hash<::nano::block_hash> () (value);
	}
};
template <>
struct hash<::nano::hash_or_account>
{
	size_t operator() (::nano::hash_or_account const & value) const noexcept
	{
		return std::hash<::nano::hash_or_account> () (value);
	}
};
template <>
struct hash<::nano::root>
{
	size_t operator() (::nano::root const & value) const noexcept
	{
		return std::hash<::nano::root> () (value);
	}
};
template <>
struct hash<::nano::link>
{
	size_t operator() (::nano::link const & value) const noexcept
	{
		return std::hash<::nano::link> () (value);
	}
};
template <>
struct hash<::nano::raw_key>
{
	size_t operator() (::nano::raw_key const & value) const noexcept
	{
		return std::hash<::nano::raw_key> () (value);
	}
};
template <>
struct hash<::nano::wallet_id>
{
	size_t operator() (::nano::wallet_id const & value) const noexcept
	{
		return std::hash<::nano::wallet_id> () (value);
	}
};
template <>
struct hash<::nano::uint512_union>
{
	size_t operator() (::nano::uint512_union const & value) const noexcept
	{
		return std::hash<::nano::uint512_union> () (value);
	}
};
template <>
struct hash<::nano::qualified_root>
{
	size_t operator() (::nano::qualified_root const & value) const noexcept
	{
		return std::hash<::nano::qualified_root> () (value);
	}
};
}

/*
 * Formatters
 */

template <>
struct fmt::formatter<nano::uint128_union> : fmt::ostream_formatter
{
};

template <>
struct fmt::formatter<nano::uint256_union> : fmt::ostream_formatter
{
};

template <>
struct fmt::formatter<nano::uint512_union> : fmt::ostream_formatter
{
};

template <>
struct fmt::formatter<nano::hash_or_account> : fmt::ostream_formatter
{
};

template <>
struct fmt::formatter<nano::block_hash> : fmt::formatter<nano::uint256_union>
{
};

template <>
struct fmt::formatter<nano::public_key> : fmt::formatter<nano::uint256_union>
{
};

template <>
struct fmt::formatter<nano::qualified_root> : fmt::formatter<nano::uint512_union>
{
};
