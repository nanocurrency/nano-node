#include <nano/lib/numbers.hpp>
#include <nano/lib/saturate.hpp>

#include <gtest/gtest.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace
{
// True when add_sat, sub_sat, inc_sat and dec_sat all accept operands of type T
template <typename T>
concept all_saturating_calls_compile = requires (T x) {
	nano::add_sat (x, x);
	nano::sub_sat (x, x);
	nano::inc_sat (x);
	nano::dec_sat (x);
};

// True when any of add_sat, sub_sat, inc_sat and dec_sat accepts operands of type T
template <typename T>
concept any_saturating_call_compiles = requires (T x) { nano::add_sat (x, x); } || requires (T x) { nano::sub_sat (x, x); } || requires (T x) { nano::inc_sat (x); } || requires (T x) { nano::dec_sat (x); };

// True when add_sat or sub_sat accepts a left operand of type T with a right operand of type U
template <typename T, typename U>
concept binary_saturating_call_compiles = requires (T x, U y) { nano::add_sat (x, y); } || requires (T x, U y) { nano::sub_sat (x, y); };

// Boundary cases of add_sat for a built-in unsigned type
template <typename T>
void check_add_unsigned ()
{
	T const max = std::numeric_limits<T>::max ();

	// Sums that fit come out exact, including ones that land exactly on the maximum
	ASSERT_EQ (nano::add_sat (T{ 0 }, T{ 0 }), T{ 0 });
	ASSERT_EQ (nano::add_sat (T{ 2 }, T{ 3 }), T{ 5 });
	ASSERT_EQ (nano::add_sat (max, T{ 0 }), max);
	ASSERT_EQ (nano::add_sat (T{ 0 }, max), max);
	ASSERT_EQ (nano::add_sat (T (max - 1), T{ 1 }), max);
	ASSERT_EQ (nano::add_sat (T (max / 2), T (max / 2 + 1)), max);

	// Sums past the maximum clamp to it, including one whose wrapped value would be zero
	ASSERT_EQ (nano::add_sat (max, T{ 1 }), max);
	ASSERT_EQ (nano::add_sat (T{ 1 }, max), max);
	ASSERT_EQ (nano::add_sat (max, max), max);
	ASSERT_EQ (nano::add_sat (T (max / 2 + 1), T (max / 2 + 1)), max);
}

// Boundary cases of add_sat for a built-in signed type
template <typename T>
void check_add_signed ()
{
	T const min = std::numeric_limits<T>::min ();
	T const max = std::numeric_limits<T>::max ();

	// Sums that fit come out exact for every combination of signs
	ASSERT_EQ (nano::add_sat (T{ 0 }, T{ 0 }), T{ 0 });
	ASSERT_EQ (nano::add_sat (T{ 2 }, T{ 3 }), T{ 5 });
	ASSERT_EQ (nano::add_sat (T{ -2 }, T{ -3 }), T{ -5 });
	ASSERT_EQ (nano::add_sat (T{ 2 }, T{ -3 }), T{ -1 });
	ASSERT_EQ (nano::add_sat (T{ -2 }, T{ 3 }), T{ 1 });

	// Operands of opposite signs never overflow, even at the extremes
	ASSERT_EQ (nano::add_sat (min, max), T{ -1 });
	ASSERT_EQ (nano::add_sat (max, min), T{ -1 });
	ASSERT_EQ (nano::add_sat (max, T{ -1 }), T (max - 1));
	ASSERT_EQ (nano::add_sat (min, T{ 1 }), T (min + 1));

	// Sums landing exactly on a bound are not clamped
	ASSERT_EQ (nano::add_sat (T (max - 1), T{ 1 }), max);
	ASSERT_EQ (nano::add_sat (T (min + 1), T{ -1 }), min);
	ASSERT_EQ (nano::add_sat (max, T{ 0 }), max);
	ASSERT_EQ (nano::add_sat (min, T{ 0 }), min);

	// Positive overflow clamps to the maximum
	ASSERT_EQ (nano::add_sat (max, T{ 1 }), max);
	ASSERT_EQ (nano::add_sat (T{ 1 }, max), max);
	ASSERT_EQ (nano::add_sat (max, max), max);

	// Negative overflow clamps to the minimum
	ASSERT_EQ (nano::add_sat (min, T{ -1 }), min);
	ASSERT_EQ (nano::add_sat (T{ -1 }, min), min);
	ASSERT_EQ (nano::add_sat (min, min), min);
}

// Boundary cases of sub_sat for a built-in unsigned type
template <typename T>
void check_sub_unsigned ()
{
	T const max = std::numeric_limits<T>::max ();

	// Differences that fit come out exact, including ones that land exactly on zero
	ASSERT_EQ (nano::sub_sat (T{ 5 }, T{ 3 }), T{ 2 });
	ASSERT_EQ (nano::sub_sat (max, T{ 0 }), max);
	ASSERT_EQ (nano::sub_sat (max, T (max - 1)), T{ 1 });
	ASSERT_EQ (nano::sub_sat (max, max), T{ 0 });
	ASSERT_EQ (nano::sub_sat (T{ 0 }, T{ 0 }), T{ 0 });

	// Differences below zero clamp to zero
	ASSERT_EQ (nano::sub_sat (T{ 0 }, T{ 1 }), T{ 0 });
	ASSERT_EQ (nano::sub_sat (T{ 3 }, T{ 5 }), T{ 0 });
	ASSERT_EQ (nano::sub_sat (T (max - 1), max), T{ 0 });
	ASSERT_EQ (nano::sub_sat (T{ 0 }, max), T{ 0 });
}

// Boundary cases of sub_sat for a built-in signed type
template <typename T>
void check_sub_signed ()
{
	T const min = std::numeric_limits<T>::min ();
	T const max = std::numeric_limits<T>::max ();

	// Differences that fit come out exact for every combination of signs
	ASSERT_EQ (nano::sub_sat (T{ 5 }, T{ 3 }), T{ 2 });
	ASSERT_EQ (nano::sub_sat (T{ 3 }, T{ 5 }), T{ -2 });
	ASSERT_EQ (nano::sub_sat (T{ -3 }, T{ 5 }), T{ -8 });
	ASSERT_EQ (nano::sub_sat (T{ 3 }, T{ -5 }), T{ 8 });
	ASSERT_EQ (nano::sub_sat (T{ -3 }, T{ -5 }), T{ 2 });

	// Operands of equal signs never overflow, even at the extremes
	ASSERT_EQ (nano::sub_sat (max, max), T{ 0 });
	ASSERT_EQ (nano::sub_sat (min, min), T{ 0 });
	ASSERT_EQ (nano::sub_sat (min, T{ -1 }), T (min + 1));
	ASSERT_EQ (nano::sub_sat (T{ 0 }, max), T (min + 1));

	// Differences landing exactly on a bound are not clamped
	ASSERT_EQ (nano::sub_sat (T (max - 1), T{ -1 }), max);
	ASSERT_EQ (nano::sub_sat (T (min + 1), T{ 1 }), min);
	ASSERT_EQ (nano::sub_sat (T{ -1 }, min), max);
	ASSERT_EQ (nano::sub_sat (T{ -1 }, max), min);

	// Positive overflow clamps to the maximum, including the negation of the minimum
	ASSERT_EQ (nano::sub_sat (max, T{ -1 }), max);
	ASSERT_EQ (nano::sub_sat (T{ 0 }, min), max);
	ASSERT_EQ (nano::sub_sat (max, min), max);

	// Negative overflow clamps to the minimum
	ASSERT_EQ (nano::sub_sat (min, T{ 1 }), min);
	ASSERT_EQ (nano::sub_sat (T{ -2 }, max), min);
	ASSERT_EQ (nano::sub_sat (min, max), min);
}

// Boundary cases of inc_sat and dec_sat for a built-in type
template <typename T>
void check_inc_dec ()
{
	T const min = std::numeric_limits<T>::min ();
	T const max = std::numeric_limits<T>::max ();

	// Away from the bounds the value moves by one
	ASSERT_EQ (nano::inc_sat (T{ 0 }), T{ 1 });
	ASSERT_EQ (nano::dec_sat (T{ 1 }), T{ 0 });
	ASSERT_EQ (nano::inc_sat (T (max - 1)), max);
	ASSERT_EQ (nano::dec_sat (max), T (max - 1));
	ASSERT_EQ (nano::inc_sat (min), T (min + 1));
	ASSERT_EQ (nano::dec_sat (T (min + 1)), min);

	// At the bounds the value stays put
	ASSERT_EQ (nano::inc_sat (max), max);
	ASSERT_EQ (nano::dec_sat (min), min);

	// Signed values step across zero
	if constexpr (std::numeric_limits<T>::is_signed)
	{
		ASSERT_EQ (nano::dec_sat (T{ 0 }), T{ -1 });
		ASSERT_EQ (nano::inc_sat (T{ -1 }), T{ 0 });
	}
}

// Boundary cases of add_sat for a fixed-width multiprecision type, including carries between 64-bit limbs
template <typename T>
void check_add_multiprecision ()
{
	T const max = std::numeric_limits<T>::max ();
	T const half = max >> 1;
	T const limb_max{ std::numeric_limits<uint64_t>::max () };
	T const limb_carry = T{ 1 } << 64;

	// Sums that fit come out exact, including ones that carry into the next limb or land exactly on the maximum
	ASSERT_EQ (nano::add_sat (T{ 0 }, T{ 0 }), T{ 0 });
	ASSERT_EQ (nano::add_sat (T{ 2 }, T{ 3 }), T{ 5 });
	ASSERT_EQ (nano::add_sat (limb_max, T{ 1 }), limb_carry);
	ASSERT_EQ (nano::add_sat (limb_max, limb_max), T (limb_carry + limb_max - 1));
	ASSERT_EQ (nano::add_sat (half, half), T (max - 1));
	ASSERT_EQ (nano::add_sat (half, T (half + 1)), max);
	ASSERT_EQ (nano::add_sat (T (max - 1), T{ 1 }), max);
	ASSERT_EQ (nano::add_sat (max, T{ 0 }), max);

	// Sums past the maximum clamp to it, including one whose wrapped value would be zero
	ASSERT_EQ (nano::add_sat (max, T{ 1 }), max);
	ASSERT_EQ (nano::add_sat (T{ 1 }, max), max);
	ASSERT_EQ (nano::add_sat (max, max), max);
	ASSERT_EQ (nano::add_sat (max, limb_carry), max);
	ASSERT_EQ (nano::add_sat (T (half + 1), T (half + 1)), max);
}

// Boundary cases of sub_sat for a fixed-width multiprecision type, including borrows between 64-bit limbs
template <typename T>
void check_sub_multiprecision ()
{
	T const max = std::numeric_limits<T>::max ();
	T const limb_max{ std::numeric_limits<uint64_t>::max () };
	T const limb_carry = T{ 1 } << 64;

	// Differences that fit come out exact, including ones that borrow from the next limb or land exactly on zero
	ASSERT_EQ (nano::sub_sat (T{ 5 }, T{ 3 }), T{ 2 });
	ASSERT_EQ (nano::sub_sat (limb_carry, T{ 1 }), limb_max);
	ASSERT_EQ (nano::sub_sat (max, limb_max), T (max - limb_max));
	ASSERT_EQ (nano::sub_sat (max, T{ 0 }), max);
	ASSERT_EQ (nano::sub_sat (max, T (max - 1)), T{ 1 });
	ASSERT_EQ (nano::sub_sat (max, max), T{ 0 });

	// Differences below zero clamp to zero
	ASSERT_EQ (nano::sub_sat (T{ 0 }, T{ 1 }), T{ 0 });
	ASSERT_EQ (nano::sub_sat (limb_max, limb_carry), T{ 0 });
	ASSERT_EQ (nano::sub_sat (T (max - 1), max), T{ 0 });
	ASSERT_EQ (nano::sub_sat (T{ 0 }, max), T{ 0 });
}

// Boundary cases of inc_sat and dec_sat for a fixed-width multiprecision type
template <typename T>
void check_inc_dec_multiprecision ()
{
	T const max = std::numeric_limits<T>::max ();
	T const limb_max{ std::numeric_limits<uint64_t>::max () };
	T const limb_carry = T{ 1 } << 64;

	// Away from the bounds the value moves by one, carrying or borrowing across a limb
	ASSERT_EQ (nano::inc_sat (T{ 0 }), T{ 1 });
	ASSERT_EQ (nano::dec_sat (T{ 1 }), T{ 0 });
	ASSERT_EQ (nano::inc_sat (limb_max), limb_carry);
	ASSERT_EQ (nano::dec_sat (limb_carry), limb_max);
	ASSERT_EQ (nano::inc_sat (T (max - 1)), max);
	ASSERT_EQ (nano::dec_sat (max), T (max - 1));

	// At the bounds the value stays put
	ASSERT_EQ (nano::inc_sat (max), max);
	ASSERT_EQ (nano::dec_sat (T{ 0 }), T{ 0 });
}
}

/*
 * add_sat returns the exact sum while it fits a built-in unsigned type and clamps to the maximum past it, at every width
 */
TEST (saturate, add_unsigned)
{
	// Every width from 8 to 64 bits, plus std::size_t, which is a distinct type from uint64_t on some platforms
	check_add_unsigned<uint8_t> ();
	check_add_unsigned<uint16_t> ();
	check_add_unsigned<uint32_t> ();
	check_add_unsigned<uint64_t> ();
	check_add_unsigned<std::size_t> ();
}

/*
 * add_sat returns the exact sum while it fits a built-in signed type and clamps to the bound it overflows past, at every width and for every combination of signs
 */
TEST (saturate, add_signed)
{
	// Every width from 8 to 64 bits
	check_add_signed<int8_t> ();
	check_add_signed<int16_t> ();
	check_add_signed<int32_t> ();
	check_add_signed<int64_t> ();
}

/*
 * sub_sat returns the exact difference while it is not negative for a built-in unsigned type and clamps to zero below it, at every width
 */
TEST (saturate, sub_unsigned)
{
	// Every width from 8 to 64 bits, plus std::size_t
	check_sub_unsigned<uint8_t> ();
	check_sub_unsigned<uint16_t> ();
	check_sub_unsigned<uint32_t> ();
	check_sub_unsigned<uint64_t> ();
	check_sub_unsigned<std::size_t> ();
}

/*
 * sub_sat returns the exact difference while it fits a built-in signed type and clamps to the bound it overflows past, including subtracting the minimum, whose negation does not fit
 */
TEST (saturate, sub_signed)
{
	// Every width from 8 to 64 bits
	check_sub_signed<int8_t> ();
	check_sub_signed<int16_t> ();
	check_sub_signed<int32_t> ();
	check_sub_signed<int64_t> ();
}

/*
 * inc_sat and dec_sat move a built-in integer by one and stay put at the maximum and the minimum respectively
 */
TEST (saturate, inc_dec)
{
	// Unsigned types at every width, where the minimum is zero
	check_inc_dec<uint8_t> ();
	check_inc_dec<uint16_t> ();
	check_inc_dec<uint32_t> ();
	check_inc_dec<uint64_t> ();

	// Signed types at every width
	check_inc_dec<int8_t> ();
	check_inc_dec<int16_t> ();
	check_inc_dec<int32_t> ();
	check_inc_dec<int64_t> ();
}

/*
 * Every pair of uint8_t operands, and every single operand of inc_sat and dec_sat, gives the exact result computed in int and clamped to the uint8_t range
 */
TEST (saturate, exhaustive_uint8)
{
	int const min = std::numeric_limits<uint8_t>::min ();
	int const max = std::numeric_limits<uint8_t>::max ();

	// Sums and differences of every pair
	for (int x = min; x <= max; ++x)
	{
		for (int y = min; y <= max; ++y)
		{
			ASSERT_EQ (nano::add_sat (static_cast<uint8_t> (x), static_cast<uint8_t> (y)), static_cast<uint8_t> (std::clamp (x + y, min, max)));
			ASSERT_EQ (nano::sub_sat (static_cast<uint8_t> (x), static_cast<uint8_t> (y)), static_cast<uint8_t> (std::clamp (x - y, min, max)));
		}
	}

	// Increments and decrements of every value
	for (int x = min; x <= max; ++x)
	{
		ASSERT_EQ (nano::inc_sat (static_cast<uint8_t> (x)), static_cast<uint8_t> (std::clamp (x + 1, min, max)));
		ASSERT_EQ (nano::dec_sat (static_cast<uint8_t> (x)), static_cast<uint8_t> (std::clamp (x - 1, min, max)));
	}
}

/*
 * Every pair of int8_t operands, and every single operand of inc_sat and dec_sat, gives the exact result computed in int and clamped to the int8_t range
 */
TEST (saturate, exhaustive_int8)
{
	int const min = std::numeric_limits<int8_t>::min ();
	int const max = std::numeric_limits<int8_t>::max ();

	// Sums and differences of every pair
	for (int x = min; x <= max; ++x)
	{
		for (int y = min; y <= max; ++y)
		{
			ASSERT_EQ (nano::add_sat (static_cast<int8_t> (x), static_cast<int8_t> (y)), static_cast<int8_t> (std::clamp (x + y, min, max)));
			ASSERT_EQ (nano::sub_sat (static_cast<int8_t> (x), static_cast<int8_t> (y)), static_cast<int8_t> (std::clamp (x - y, min, max)));
		}
	}

	// Increments and decrements of every value
	for (int x = min; x <= max; ++x)
	{
		ASSERT_EQ (nano::inc_sat (static_cast<int8_t> (x)), static_cast<int8_t> (std::clamp (x + 1, min, max)));
		ASSERT_EQ (nano::dec_sat (static_cast<int8_t> (x)), static_cast<int8_t> (std::clamp (x - 1, min, max)));
	}
}

/*
 * add_sat on the node's 128, 256 and 512-bit integers carries between limbs like plain addition and clamps to the maximum instead of wrapping
 */
TEST (saturate, multiprecision_add)
{
	// Each fixed width the node uses
	check_add_multiprecision<nano::uint128_t> ();
	check_add_multiprecision<nano::uint256_t> ();
	check_add_multiprecision<nano::uint512_t> ();
}

/*
 * sub_sat on the node's 128, 256 and 512-bit integers borrows between limbs like plain subtraction and clamps to zero instead of wrapping
 */
TEST (saturate, multiprecision_sub)
{
	// Each fixed width the node uses
	check_sub_multiprecision<nano::uint128_t> ();
	check_sub_multiprecision<nano::uint256_t> ();
	check_sub_multiprecision<nano::uint512_t> ();
}

/*
 * inc_sat and dec_sat on the node's 128, 256 and 512-bit integers move by one, carrying or borrowing across limbs, and stay put at the maximum and at zero
 */
TEST (saturate, multiprecision_inc_dec)
{
	// Each fixed width the node uses
	check_inc_dec_multiprecision<nano::uint128_t> ();
	check_inc_dec_multiprecision<nano::uint256_t> ();
	check_inc_dec_multiprecision<nano::uint512_t> ();
}

/*
 * For built-in types the functions evaluate in constant expressions, and every function returns exactly its operand type without promotion and cannot throw
 */
TEST (saturate, constant_evaluation)
{
	// Results computed at compile time, exact and clamped at both ends
	static_assert (nano::add_sat (int8_t{ 100 }, int8_t{ 27 }) == 127);
	static_assert (nano::add_sat (int8_t{ 100 }, int8_t{ 100 }) == 127);
	static_assert (nano::add_sat (int8_t{ -100 }, int8_t{ -100 }) == -128);
	static_assert (nano::sub_sat (int8_t{ -100 }, int8_t{ 100 }) == -128);
	static_assert (nano::sub_sat (int8_t{ 100 }, int8_t{ -100 }) == 127);
	static_assert (nano::add_sat (uint8_t{ 200 }, uint8_t{ 100 }) == 255);
	static_assert (nano::sub_sat (uint8_t{ 100 }, uint8_t{ 200 }) == 0);
	static_assert (nano::add_sat (std::numeric_limits<int64_t>::max (), int64_t{ 1 }) == std::numeric_limits<int64_t>::max ());
	static_assert (nano::sub_sat (int64_t{ 0 }, std::numeric_limits<int64_t>::min ()) == std::numeric_limits<int64_t>::max ());
	static_assert (nano::sub_sat (uint64_t{ 0 }, uint64_t{ 1 }) == 0);
	static_assert (nano::inc_sat (std::numeric_limits<uint32_t>::max ()) == std::numeric_limits<uint32_t>::max ());
	static_assert (nano::dec_sat (std::numeric_limits<int32_t>::min ()) == std::numeric_limits<int32_t>::min ());

	// The result has the operand type, not the type it promotes to
	static_assert (std::is_same_v<decltype (nano::add_sat (uint8_t{}, uint8_t{})), uint8_t>);
	static_assert (std::is_same_v<decltype (nano::sub_sat (int16_t{}, int16_t{})), int16_t>);
	static_assert (std::is_same_v<decltype (nano::inc_sat (int8_t{})), int8_t>);
	static_assert (std::is_same_v<decltype (nano::dec_sat (uint16_t{})), uint16_t>);
	static_assert (std::is_same_v<decltype (nano::add_sat (nano::uint256_t{}, nano::uint256_t{})), nano::uint256_t>);
	static_assert (std::is_same_v<decltype (nano::inc_sat (nano::uint512_t{})), nano::uint512_t>);

	// None of the functions can throw, for built-in or multiprecision operands
	static_assert (noexcept (nano::add_sat (int64_t{}, int64_t{})));
	static_assert (noexcept (nano::sub_sat (nano::uint128_t{}, nano::uint128_t{})));
	static_assert (noexcept (nano::inc_sat (nano::uint256_t{})));
	static_assert (noexcept (nano::dec_sat (nano::uint512_t{})));
}

/*
 * The functions accept the same built-in types as std::add_sat plus the node's fixed-width unsigned multiprecision integers, and reject everything else at compile time
 * Rejected are bool, the character types, floating point, cv-qualified types, multiprecision types that are signed, unbounded or throw on overflow, and mixed operand types
 */
TEST (saturate, accepted_types)
{
	// Every standard integer type, including signed char and unsigned char
	static_assert (all_saturating_calls_compile<signed char>);
	static_assert (all_saturating_calls_compile<unsigned char>);
	static_assert (all_saturating_calls_compile<short>);
	static_assert (all_saturating_calls_compile<unsigned short>);
	static_assert (all_saturating_calls_compile<int>);
	static_assert (all_saturating_calls_compile<unsigned int>);
	static_assert (all_saturating_calls_compile<long>);
	static_assert (all_saturating_calls_compile<unsigned long>);
	static_assert (all_saturating_calls_compile<long long>);
	static_assert (all_saturating_calls_compile<unsigned long long>);

	// The node's fixed-width unsigned multiprecision integers
	static_assert (all_saturating_calls_compile<nano::uint128_t>);
	static_assert (all_saturating_calls_compile<nano::uint256_t>);
	static_assert (all_saturating_calls_compile<nano::uint512_t>);

	// bool and the character types, as std::add_sat rejects them
	static_assert (!any_saturating_call_compiles<bool>);
	static_assert (!any_saturating_call_compiles<char>);
	static_assert (!any_saturating_call_compiles<wchar_t>);
	static_assert (!any_saturating_call_compiles<char8_t>);
	static_assert (!any_saturating_call_compiles<char16_t>);
	static_assert (!any_saturating_call_compiles<char32_t>);

	// Floating point types
	static_assert (!any_saturating_call_compiles<float>);
	static_assert (!any_saturating_call_compiles<double>);
	static_assert (!any_saturating_call_compiles<long double>);

	// cv-qualified types, which only an explicit template argument can request
	static_assert (!nano::saturating_integer<int const>);
	static_assert (!nano::saturating_integer<bool const>);
	static_assert (!nano::saturating_integer<nano::uint256_t const>);

	// Multiprecision types that are signed, unbounded or throw on overflow, and the node's number unions
	static_assert (!any_saturating_call_compiles<boost::multiprecision::int256_t>);
	static_assert (!any_saturating_call_compiles<boost::multiprecision::cpp_int>);
	static_assert (!any_saturating_call_compiles<boost::multiprecision::checked_uint256_t>);
	static_assert (!any_saturating_call_compiles<nano::uint256_union>);

	// Operands of different types, even when both are accepted on their own
	static_assert (!binary_saturating_call_compiles<int, long>);
	static_assert (!binary_saturating_call_compiles<int, unsigned int>);
	static_assert (!binary_saturating_call_compiles<uint32_t, uint64_t>);
	static_assert (!binary_saturating_call_compiles<uint8_t, int>);
	static_assert (!binary_saturating_call_compiles<nano::uint256_t, int>);
	static_assert (!binary_saturating_call_compiles<uint64_t, nano::uint256_t>);
	static_assert (!binary_saturating_call_compiles<nano::uint128_t, nano::uint256_t>);
}
