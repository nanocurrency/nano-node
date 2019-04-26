#pragma once

#include <atomic>
#include <boost/multiprecision/cpp_int.hpp>
#include <condition_variable>
#include <mutex>
#include <nano/lib/timer.hpp>
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

namespace util
{
	/**
	 * Helper to signal completion of async handlers in tests.
	 * Subclasses implement specific conditions for completion.
	 */
	class completion_signal
	{
	public:
		~completion_signal ()
		{
			notify ();
		}

		/** Explicitly notify the completion */
		void notify ()
		{
			cv.notify_all ();
		}

	protected:
		std::condition_variable cv;
		std::mutex mutex;
	};

	/**
	 * Signals completion when a count is reached.
	 */
	class counted_completion : public completion_signal
	{
	public:
		/**
		 * Constructor
		 * @param required_count_a When increment() reaches this count within the deadline, await_count_for() will return true.
		 */
		counted_completion (size_t required_count_a) :
		required_count (required_count_a)
		{
		}

		/**
		 * Wait for increment() to signal completion, or reaching the deadline.
		 * @param deadline_duration_a Deadline as a std::chrono duration
		 * @return true if the count is reached within the deadline
		 */
		template <typename UNIT>
		bool await_count_for (UNIT deadline_duration_a)
		{
			nano::timer<UNIT> timer (nano::timer_state::started);
			bool success = false;
			while (!success)
			{
				success = count >= required_count;
				if (!success && timer.before_deadline (deadline_duration_a))
				{
					std::unique_lock<std::mutex> lock (mutex);
					cv.wait_for (lock, std::chrono::milliseconds (1));
				}
			}
			return success && timer.before_deadline (deadline_duration_a);
		}

		/** Increments the current count. If the required count is reached, await_count_for() waiters are notified. */
		size_t increment ()
		{
			auto val (count.fetch_add (1));
			if (val >= required_count)
			{
				notify ();
			}
			return val;
		}

	private:
		std::atomic<size_t> count{ 0 };
		size_t required_count;
	};
}
}
