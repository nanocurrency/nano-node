#pragma once

#include <atomic>
#include <chrono>

namespace nano
{
/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
	return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

/**
 Creates a unique 64-bit timestamp each time timestamp_now is called.
 The upper 44-bits are the number of milliseconds since unix epoch
 The lower 20 bits are a monotonically increasing counter from 0, each millisecond
 */

template <typename CLOCK>
class timestamp_generator_base
{
public:
	// If CLOCK::is_steady, this class will be a steady
	static bool constexpr is_steady = CLOCK::is_steady;

	static uint64_t mask_time (uint64_t timestamp)
	{
		auto result = timestamp & time_mask;
		return result;
	}
	static uint64_t mask_count (uint64_t timestamp)
	{
		auto result = timestamp & count_mask;
		return result;
	}
	// Return a timestamp based on `ms' the number of milliseconds since the UTC epoch
	static uint64_t timestamp_from_ms (std::chrono::milliseconds ms)
	{
		auto result = static_cast<uint64_t> (ms.count ()) << count_bits;
		return result;
	}
	// Return the number of milliseconds since the UTC epoch, represented in `timestamp'
	static std::chrono::milliseconds ms_from_timestamp (uint64_t timestamp)
	{
		auto result = timestamp >> count_bits;
		return std::chrono::milliseconds{ result };
	}
	// If CLOCK::is_steady, now is guaranteed to produce monotonically increasing timestamps
	static uint64_t now ()
	{
		uint64_t stored;
		uint64_t result;
		do
		{
			stored = last.load ();
			std::chrono::milliseconds delta = std::chrono::duration_cast<std::chrono::milliseconds> (CLOCK::now ().time_since_epoch ());
			auto now_l = timestamp_from_ms (delta);
			// If `delta` hasn't changed since the last call, increment the counter, otherwise use the current value with a zero counter.
			result = mask_time (stored) == now_l ? stored + 1 : now_l;
		} while (!last.compare_exchange_weak (stored, result));
		return result;
	}

private:
	static inline std::atomic<uint64_t> last{ 0 };
	static int constexpr time_bits{ 44 }; // 44 bits for milliseconds = 17,592,186,044,416 ~ 545 years.
	static int constexpr count_bits{ 20 }; // 20-bit monotonic counter, 1,048,576 samples per ms
	static_assert (time_bits + count_bits == 64);
	static uint64_t constexpr time_mask{ ~0ULL << count_bits }; // Portion associated with timer
	static uint64_t constexpr count_mask{ ~0ULL >> time_bits }; // Portion associated with counter
};
using timestamp_generator = timestamp_generator_base<std::chrono::system_clock>;
} // namespace nano
