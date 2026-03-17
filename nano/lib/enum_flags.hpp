#pragma once

#include <iosfwd>
#include <string>
#include <type_traits>

namespace nano
{
template <typename E>
class enum_flags;

template <typename E>
std::ostream & operator<< (std::ostream &, enum_flags<E> const &);

template <typename E>
std::string to_string (enum_flags<E> const &);

/**
 * Type-safe wrapper for flag enums (bitfields)
 * Provides a std::bitset-like API
 * The underlying enum should define power-of-2 values (1 << 0, 1 << 1, etc.) and a zero value
 * Stream operator and to_string definitions are in enum_flags_templ.hpp
 */
template <typename E>
class enum_flags
{
public:
	using underlying_t = std::underlying_type_t<E>;

	constexpr enum_flags () = default;
	constexpr enum_flags (E flag) :
		value{ static_cast<underlying_t> (flag) }
	{
		check_layout ();
	}

	static constexpr enum_flags from_underlying (underlying_t v)
	{
		enum_flags f;
		f.value = v;
		return f;
	}

	bool test (E flag) const
	{
		return (value & static_cast<underlying_t> (flag)) != 0;
	}

	enum_flags & set (E flag)
	{
		value |= static_cast<underlying_t> (flag);
		return *this;
	}

	enum_flags & reset (E flag)
	{
		value &= ~static_cast<underlying_t> (flag);
		return *this;
	}

	bool any () const
	{
		return value != 0;
	}

	bool none () const
	{
		return value == 0;
	}

	enum_flags operator| (enum_flags other) const
	{
		return from_underlying (value | other.value);
	}

	enum_flags operator& (enum_flags other) const
	{
		return from_underlying (value & other.value);
	}

	enum_flags & operator|= (enum_flags other)
	{
		value |= other.value;
		return *this;
	}

	enum_flags & operator&= (enum_flags other)
	{
		value &= other.value;
		return *this;
	}

	bool operator== (enum_flags other) const
	{
		return value == other.value;
	}

	bool operator!= (enum_flags other) const
	{
		return value != other.value;
	}

	explicit operator bool () const
	{
		return any ();
	}

	underlying_t const & underlying () const
	{
		return value;
	}

	underlying_t & underlying ()
	{
		return value;
	}

private:
	underlying_t value{ 0 };

	static constexpr void check_layout ()
	{
		static_assert (std::is_standard_layout_v<enum_flags>, "Standard layout required for safe underlying type punning");
		static_assert (std::is_trivially_copyable_v<enum_flags>, "Trivially copyable required for safe underlying type punning");
		static_assert (sizeof (enum_flags) == sizeof (underlying_t), "Size of enum_flags must be the same as underlying type");
	}
};
}
