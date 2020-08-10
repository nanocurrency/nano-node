#pragma once

#include <atomic>
#include <chrono>

namespace nano {
/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
	return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

/**
 Creates a unique 64-bit timestamp each time timestamp_now is called.
 The upper 46-bits are the number of milliseconds since unix epoch
 The lower 18 bits are a monotonically increasing counter from 0, each millisecond
 */

class timestamp_generator
{
public:
	static uint64_t component_time (uint64_t value_a)
	{
		auto result (value_a & time_mask);
		return result;
	}
	static uint64_t component_count (uint64_t value_a)
	{
		auto result (value_a & count_mask);
		return result;
	}
	static uint64_t timestamp_from_ms (uint64_t value_a)
	{
		auto result (value_a << count_bits);
		return result;
	}
	static uint64_t now ()
	{
		uint64_t ms_since_epoch (std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ());
		uint64_t result (timestamp_from_ms (ms_since_epoch));
		return result;
	}
	uint64_t timestamp_now ()
	{
		static_assert (time_bits + count_bits == 64);
		uint64_t result (0);
		while (result == 0)
		{
			result = ++last;
			auto now_l (now ());
			if (component_time (result) != now ())
			{
				if (!last.compare_exchange_weak (result, now_l))
				{
					result = 0;
				}
			}
		}
		return result;
	}
private:
	std::atomic<uint64_t> last { 0 };
	static int constexpr time_bits { 46 };
	static int constexpr count_bits { 18 };
	static uint64_t constexpr time_mask { ~0ULL << count_bits }; // Portion associated with timer
	static uint64_t constexpr count_mask { ~0ULL >> time_bits }; // 18-bit monotonic counter, 262,144 samples per ms
};
} // namespace nano
