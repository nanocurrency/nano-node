#pragma once

#include <concepts>
#include <limits>
#include <type_traits>

namespace nano
{
// Built-in integer types with saturating arithmetic: every cv-unqualified integer type except bool and the character types, as for std::add_sat
template <typename T>
concept saturating_builtin_integer = std::integral<T> && std::same_as<T, std::remove_cv_t<T>> && !std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

// Fixed-width unsigned integer classes that wrap on overflow, such as nano::uint256_t
template <typename T>
concept saturating_wide_unsigned = std::is_class_v<T> && std::same_as<T, std::remove_cv_t<T>> && std::numeric_limits<T>::is_integer && std::numeric_limits<T>::is_bounded && std::numeric_limits<T>::is_modulo && !std::numeric_limits<T>::is_signed;

// Types the saturating arithmetic functions accept
template <typename T>
concept saturating_integer = nano::saturating_builtin_integer<T> || nano::saturating_wide_unsigned<T>;

// Sum of x and y clamped to the range of T
template <nano::saturating_integer T>
constexpr T add_sat (T x, T y) noexcept
{
	if constexpr (std::numeric_limits<T>::is_signed)
	{
		// Unsigned addition wraps without UB; an overflowed sum has the opposite sign of both operands
		using unsigned_type = std::make_unsigned_t<T>;
		T const sum = static_cast<T> (static_cast<unsigned_type> (x) + static_cast<unsigned_type> (y));
		bool const overflowed = ((x ^ sum) & (y ^ sum)) < 0;
		if (overflowed)
		{
			// Both operands have the sign of the bound the sum passed
			return x < 0 ? std::numeric_limits<T>::min () : std::numeric_limits<T>::max ();
		}
		else
		{
			// Without overflow the wrapped sum is the exact one
			return sum;
		}
	}
	else
	{
		// Unsigned addition wraps, so a sum below an operand overflowed
		T const sum = static_cast<T> (x + y);
		return sum < x ? std::numeric_limits<T>::max () : sum;
	}
}

// Difference of x and y clamped to the range of T
template <nano::saturating_integer T>
constexpr T sub_sat (T x, T y) noexcept
{
	if constexpr (std::numeric_limits<T>::is_signed)
	{
		// Unsigned subtraction wraps without UB; overflow needs operands of opposite signs and flips the sign of x
		using unsigned_type = std::make_unsigned_t<T>;
		T const difference = static_cast<T> (static_cast<unsigned_type> (x) - static_cast<unsigned_type> (y));
		bool const overflowed = ((x ^ y) & (x ^ difference)) < 0;
		if (overflowed)
		{
			// The difference passed the bound on the side of x
			return x < 0 ? std::numeric_limits<T>::min () : std::numeric_limits<T>::max ();
		}
		else
		{
			// Without overflow the wrapped difference is the exact one
			return difference;
		}
	}
	else
	{
		return x < y ? std::numeric_limits<T>::min () : static_cast<T> (x - y);
	}
}

// x plus one, clamped to the maximum of T
template <nano::saturating_integer T>
constexpr T inc_sat (T x) noexcept
{
	return nano::add_sat (x, T{ 1 });
}

// x minus one, clamped to the minimum of T
template <nano::saturating_integer T>
constexpr T dec_sat (T x) noexcept
{
	return nano::sub_sat (x, T{ 1 });
}
}
