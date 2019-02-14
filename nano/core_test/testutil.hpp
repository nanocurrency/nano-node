#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#include <string>

#define GTEST_TEST_ERROR_CODE(expression, text, actual, expected, fail)                       \
	GTEST_AMBIGUOUS_ELSE_BLOCKER_                                                             \
	if (const ::testing::AssertionResult gtest_ar_ = ::testing::AssertionResult (expression)) \
		;                                                                                     \
	else                                                                                      \
		fail (::testing::internal::GetBoolAssertionFailureMessage (                           \
		gtest_ar_, text, actual, expected)                                                    \
		      .c_str ())

/** Extends gtest with a std::error_code assert that prints the error code message when non-zero */
#define ASSERT_NO_ERROR(condition)                                                      \
	GTEST_TEST_ERROR_CODE (!(condition), #condition, condition.message ().c_str (), "", \
	GTEST_FATAL_FAILURE_)

/** Extends gtest with a std::error_code assert that expects an error */
#define ASSERT_IS_ERROR(condition)                                                            \
	GTEST_TEST_ERROR_CODE ((condition.value () > 0), #condition, "An error was expected", "", \
	GTEST_FATAL_FAILURE_)

/* Convenience globals for core_test */
namespace nano
{
using uint128_t = boost::multiprecision::uint128_t;
class keypair;
union uint256_union;
extern nano::keypair const & zero_key;
extern nano::keypair const & test_genesis_key;
extern std::string const & nano_test_genesis;
extern std::string const & genesis_block;
extern nano::uint256_union const & nano_test_account;
extern nano::uint256_union const & genesis_account;
extern nano::uint256_union const & burn_account;
extern nano::uint128_t const & genesis_amount;
// A block hash that compares inequal to any real block hash
extern nano::uint256_union const & not_a_block;
// An account number that compares inequal to any real account number
extern nano::uint256_union const & not_an_account;
}
