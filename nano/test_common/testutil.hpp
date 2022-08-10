#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/timer.hpp>

#include <boost/iostreams/concepts.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
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

/** Asserts that the condition becomes true within the deadline */
#define ASSERT_TIMELY(time, condition)    \
	system.deadline_set (time);           \
	while (!(condition))                  \
	{                                     \
		ASSERT_NO_ERROR (system.poll ()); \
	}

/* Convenience globals for gtest projects */
namespace nano
{
using uint128_t = boost::multiprecision::uint128_t;
class keypair;
class public_key;
class block_hash;
class telemetry_data;
class network_params;

extern nano::uint128_t const & genesis_amount;

namespace test
{
	class system;

	class stringstream_mt_sink : public boost::iostreams::sink
	{
	public:
		stringstream_mt_sink () = default;
		stringstream_mt_sink (stringstream_mt_sink const & sink)
		{
			nano::lock_guard<nano::mutex> guard (mutex);
			ss << sink.ss.str ();
		}

		std::streamsize write (char const * string_to_write, std::streamsize size)
		{
			nano::lock_guard<nano::mutex> guard (mutex);
			ss << std::string (string_to_write, size);
			return size;
		}

		std::string str ()
		{
			nano::lock_guard<nano::mutex> guard (mutex);
			return ss.str ();
		}

	private:
		mutable nano::mutex mutex;
		std::stringstream ss;
	};

	class boost_log_cerr_redirect
	{
	public:
		boost_log_cerr_redirect (std::streambuf * new_buffer) :
			old (std::cerr.rdbuf (new_buffer))
		{
			console_sink = (boost::log::add_console_log (std::cerr, boost::log::keywords::format = "%Message%"));
		}

		~boost_log_cerr_redirect ()
		{
			std::cerr.rdbuf (old);
			boost::log::core::get ()->remove_sink (console_sink);
		}

	private:
		std::streambuf * old;
		boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>> console_sink;
	};

	class cout_redirect
	{
	public:
		cout_redirect (std::streambuf * new_buffer)
		{
			std::cout.rdbuf (new_buffer);
		}

		~cout_redirect ()
		{
			std::cout.rdbuf (old);
		}

	private:
		std::streambuf * old{ std::cout.rdbuf () };
	};

	/**
	 * Helper to signal completion of async handlers in tests.
	 * Subclasses implement specific conditions for completion.
	 */
	class completion_signal
	{
	public:
		virtual ~completion_signal ()
		{
			notify ();
		}

		/** Explicitly notify the completion */
		void notify ()
		{
			cv.notify_all ();
		}

	protected:
		nano::condition_variable cv;
		nano::mutex mutex;
	};

	/**
	 * Signals completion when a count is reached.
	 */
	class counted_completion : public completion_signal
	{
	public:
		/**
		 * Constructor
		 * @param required_count_a When increment() reaches this count within the deadline, await_count_for() will return false.
		 */
		counted_completion (unsigned required_count_a) :
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
			bool error = true;
			while (error && timer.before_deadline (deadline_duration_a))
			{
				error = count < required_count;
				if (error)
				{
					nano::unique_lock<nano::mutex> lock (mutex);
					cv.wait_for (lock, std::chrono::milliseconds (1));
				}
			}
			return error;
		}

		/** Increments the current count. If the required count is reached, await_count_for() waiters are notified. */
		unsigned increment ()
		{
			auto val (count.fetch_add (1));
			if (val >= required_count)
			{
				notify ();
			}
			return val;
		}

		void increment_required_count ()
		{
			++required_count;
		}

	private:
		std::atomic<unsigned> count{ 0 };
		std::atomic<unsigned> required_count;
	};

	void wait_peer_connections (nano::test::system &);
}
}
