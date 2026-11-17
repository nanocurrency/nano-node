#include <nano/crypto_lib/random_pool.hpp>
#include <nano/crypto_lib/secure_memory.hpp>

#include <gtest/gtest.h>

#include <boost/config.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace
{
// Fills the stack below the caller with repeated copies of the marker, well within the span secure_wipe_stack clears
BOOST_NOINLINE void fill_stack (std::array<uint8_t, 32> const & marker)
{
	volatile uint8_t area[nano::stack_wipe_size / 2];
	for (std::size_t i = 0; i < sizeof (area); ++i)
	{
		area[i] = marker[i % marker.size ()];
	}
}

// Copies the stack below the caller, where the functions it called before kept their frames
BOOST_NOINLINE std::vector<uint8_t> stack_snapshot ()
{
	// Never written, so it holds whatever those frames left
	volatile uint8_t area[nano::stack_wipe_size];
	std::vector<uint8_t> result (sizeof (area));
	for (std::size_t i = 0; i < sizeof (area); ++i)
	{
		result[i] = area[i];
	}
	return result;
}
}

/*
 * secure_wipe_memory zeroes exactly the bytes it is given and leaves the bytes around them alone
 */
TEST (secure_memory, wipe_memory)
{
	// A buffer full of a non-zero pattern
	std::array<uint8_t, 64> buffer;
	buffer.fill (0xa5);

	// Wipe its middle half
	nano::secure_wipe_memory (buffer.data () + 16, 32);

	// Only the middle half is zero
	std::array<uint8_t, 64> expected;
	expected.fill (0xa5);
	std::fill (expected.begin () + 16, expected.begin () + 48, 0);
	ASSERT_EQ (expected, buffer);
}

/*
 * secure_wipe_stack zeroes what functions called before it left in the stack below their caller.
 * Builds that keep stack frames elsewhere, such as ASan with fake stacks, cannot observe that and skip the test.
 */
TEST (secure_memory, wipe_stack)
{
	std::array<uint8_t, 32> marker;
	nano::random_pool::generate_block (marker.data (), marker.size ());

	// A snapshot must see the marker a callee leaves behind, or this build cannot show the wipe
	fill_stack (marker);
	auto const unwiped = stack_snapshot ();
	if (std::ranges::search (unwiped, marker).empty ())
	{
		GTEST_SKIP () << "Stack frames of callees are not observable in this build";
	}

	// Leave the marker on the stack again, then wipe
	fill_stack (marker);
	nano::secure_wipe_stack ();

	// No copy of the marker is left
	auto const wiped = stack_snapshot ();
	ASSERT_TRUE (std::ranges::search (wiped, marker).empty ());
}
