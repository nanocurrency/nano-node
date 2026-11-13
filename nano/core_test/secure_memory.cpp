#include <nano/crypto/blake2/blake2.h>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/crypto_lib/secure_memory.hpp>
#include <nano/lib/kdf.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/network_params.hpp>

#include <gtest/gtest.h>

#include <boost/config.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace
{
// Stack below the test body that the helpers fill and copy, wider than secure_wipe_stack clears so deeper leftovers show up too
std::size_t constexpr probe_size = 4 * nano::stack_wipe_size;

// Room for the frame of the function under test itself, which the wipe leaves alone
std::size_t constexpr own_frame_allowance = 1024;

// Fills the stack below the caller with repeated copies of the pattern
template <std::size_t Size = probe_size>
BOOST_NOINLINE void fill_stack (std::span<uint8_t const> pattern)
{
	volatile uint8_t area[Size];
	for (std::size_t i = 0; i < Size; ++i)
	{
		area[i] = pattern[i % pattern.size ()];
	}
}

// Copies the stack below the caller, where the functions it called before kept their frames; the last byte is the nearest to the caller
BOOST_NOINLINE std::vector<uint8_t> stack_snapshot ()
{
	// Never written, so it holds whatever those frames left
	volatile uint8_t area[probe_size];
	std::vector<uint8_t> result (probe_size);
	for (std::size_t i = 0; i < probe_size; ++i)
	{
		result[i] = area[i];
	}
	return result;
}

// How far below the caller the deepest copy of the needle sits, or 0 when the stack holds none
std::size_t copy_depth (std::vector<uint8_t> const & stack, std::span<uint8_t const> needle)
{
	auto const deepest = std::search (stack.begin (), stack.end (), needle.begin (), needle.end ());
	return deepest == stack.end () ? 0 : stack.end () - deepest;
}

// How far below the caller the deepest non-zero byte sits, within the nearer half of the span secure_wipe_stack clears, clear of the frames it calls
std::size_t dirty_depth (std::vector<uint8_t> const & stack)
{
	auto const window = std::span{ stack }.last (nano::stack_wipe_size / 2);
	auto const deepest = std::find_if (window.begin (), window.end (), [] (uint8_t byte) { return byte != 0; });
	return window.end () - deepest;
}

// Checks that stack_snapshot sees what a callee leaves behind, then zeroes the probed stack; false where frames live elsewhere, as under ASan
// Inlined so that its fills cover the same stack as the snapshot the test body takes afterwards
BOOST_FORCEINLINE bool stack_residue_observable ()
{
	std::array<uint8_t, 4> const marker{ 0xde, 0xad, 0xbe, 0xef };
	fill_stack (marker);
	auto const observable = copy_depth (stack_snapshot (), marker) != 0;
	fill_stack (std::array<uint8_t, 1>{});
	return observable;
}

// The expanded key ed25519 derives from a private key: the clamped scalar, then the nonce prefix
std::array<uint8_t, 64> expanded_key (nano::raw_key const & prv)
{
	std::array<uint8_t, 64> result;
	blake2b_state state;
	blake2b_init (&state, result.size ());
	blake2b_update (&state, prv.bytes.data (), prv.bytes.size ());
	blake2b_final (&state, result.data (), result.size ());
	result[0] &= 248;
	result[31] &= 127;
	result[31] |= 64;
	return result;
}
}

// secure_wipe_stack clears what functions called before it left in the stack below its caller
TEST (secure_memory, wipe_stack)
{
	std::array<uint8_t, 32> marker;
	nano::random_pool::generate_block (marker.data (), marker.size ());
	if (!stack_residue_observable ())
	{
		GTEST_SKIP () << "Stack frames of callees are not observable in this build";
	}
	// Leave the marker in callee frames well within the span the wipe covers, then wipe
	fill_stack<nano::stack_wipe_size / 2> (marker);
	nano::secure_wipe_stack ();
	auto const stack = stack_snapshot ();
	ASSERT_EQ (0, copy_depth (stack, marker));
	ASSERT_LT (dirty_depth (stack), own_frame_allowance);
}

// deterministic_key leaves neither the seed nor the derived key in the stack it used
TEST (secure_memory, deterministic_key)
{
	nano::raw_key seed;
	nano::random_pool::generate_block (seed.bytes.data (), seed.bytes.size ());
	if (!stack_residue_observable ())
	{
		GTEST_SKIP () << "Stack frames of callees are not observable in this build";
	}
	auto const key = nano::deterministic_key (seed, 1);
	// Copy the stack before any other call reuses it
	auto const stack = stack_snapshot ();
	EXPECT_EQ (0, copy_depth (stack, seed.bytes));
	EXPECT_EQ (0, copy_depth (stack, key.bytes));
	ASSERT_LT (dirty_depth (stack), own_frame_allowance);
}

// pub_key leaves neither the private key nor its expansion in the stack it used
TEST (secure_memory, pub_key)
{
	nano::raw_key prv;
	nano::random_pool::generate_block (prv.bytes.data (), prv.bytes.size ());
	if (!stack_residue_observable ())
	{
		GTEST_SKIP () << "Stack frames of callees are not observable in this build";
	}
	nano::pub_key (prv);
	// Copy the stack before any other call reuses it
	auto const stack = stack_snapshot ();
	auto const expanded = expanded_key (prv);
	EXPECT_EQ (0, copy_depth (stack, prv.bytes));
	EXPECT_EQ (0, copy_depth (stack, std::span{ expanded }.first (32)));
	EXPECT_EQ (0, copy_depth (stack, std::span{ expanded }.last (32)));
	ASSERT_LT (dirty_depth (stack), own_frame_allowance);
}

// sign_message leaves neither the private key nor its expansion in the stack it used
TEST (secure_memory, sign_message)
{
	nano::keypair key;
	nano::uint256_union const message{ 1 };
	if (!stack_residue_observable ())
	{
		GTEST_SKIP () << "Stack frames of callees are not observable in this build";
	}
	nano::sign_message (key.prv, key.pub, message);
	// Copy the stack before any other call reuses it
	auto const stack = stack_snapshot ();
	auto const expanded = expanded_key (key.prv);
	EXPECT_EQ (0, copy_depth (stack, key.prv.bytes));
	EXPECT_EQ (0, copy_depth (stack, std::span{ expanded }.first (32)));
	EXPECT_EQ (0, copy_depth (stack, std::span{ expanded }.last (32)));
	ASSERT_LT (dirty_depth (stack), own_frame_allowance);
}

// Creating a random keypair leaves neither its private key nor the expansion in the stack it used
TEST (secure_memory, keypair)
{
	if (!stack_residue_observable ())
	{
		GTEST_SKIP () << "Stack frames of callees are not observable in this build";
	}
	nano::keypair key;
	// Copy the stack before any other call reuses it
	auto const stack = stack_snapshot ();
	auto const expanded = expanded_key (key.prv);
	EXPECT_EQ (0, copy_depth (stack, key.prv.bytes));
	EXPECT_EQ (0, copy_depth (stack, std::span{ expanded }.first (32)));
	EXPECT_EQ (0, copy_depth (stack, std::span{ expanded }.last (32)));
	ASSERT_LT (dirty_depth (stack), own_frame_allowance);
}

// The password KDF leaves neither the password, the derived key nor its working data in the stack it used
TEST (secure_memory, kdf)
{
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	std::string const password{ "correct horse battery staple" };
	std::span<uint8_t const> const password_bytes{ reinterpret_cast<uint8_t const *> (password.data ()), password.size () };
	nano::uint256_union const salt{ 1 };
	if (!stack_residue_observable ())
	{
		GTEST_SKIP () << "Stack frames of callees are not observable in this build";
	}
	nano::raw_key key;
	kdf.phs (key, password, salt);
	// Copy the stack before any other call reuses it
	auto const stack = stack_snapshot ();
	EXPECT_EQ (0, copy_depth (stack, password_bytes));
	EXPECT_EQ (0, copy_depth (stack, key.bytes));
	ASSERT_LT (dirty_depth (stack), own_frame_allowance);
}
