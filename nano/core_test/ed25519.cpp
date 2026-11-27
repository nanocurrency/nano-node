#include <nano/lib/blocks.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <array>
#include <set>

/*
 * Which signatures verify is a consensus rule: every node has to give the same answer for the same key, message and signature.
 * The verifier is more lenient than stricter ed25519 implementations in the ways pinned below, and one that answered differently,
 * in either direction, would disagree with the network about which blocks and votes are valid.
 * The tests go through nano::validate_message, which every signature check in the node ends in; it returns true for a signature that does not verify
 */

namespace
{
// Order of the ed25519 base point, little endian like the scalar half of a signature
std::array<uint8_t, 32> const group_order = { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };

// Adds the group order to the scalar half of a signature, which leaves the scalar the same modulo the order
nano::signature add_group_order (nano::signature signature)
{
	unsigned carry = 0;
	for (std::size_t index = 0; index < group_order.size (); ++index)
	{
		auto const sum = signature.bytes[32 + index] + group_order[index] + carry;
		signature.bytes[32 + index] = static_cast<uint8_t> (sum);
		carry = sum >> 8;
	}
	return signature;
}
}

/*
 * A signature verifies for the key and the message it was made for, and for no other key, message or signature bytes
 */
TEST (ed25519, signing)
{
	nano::keypair key;
	nano::keypair other;
	nano::uint256_union const message{ 1 };
	auto const signature = nano::sign_message (key.prv, key.pub, message);

	// The signature verifies for its own key and message
	ASSERT_FALSE (nano::validate_message (key.pub, message, signature));

	// Another key or another message does not verify
	ASSERT_TRUE (nano::validate_message (other.pub, message, signature));
	ASSERT_TRUE (nano::validate_message (key.pub, nano::uint256_union{ 2 }, signature));

	// A flipped bit in the point half or in the scalar half does not verify
	auto flipped_point = signature;
	flipped_point.bytes[0] ^= 0x1;
	ASSERT_TRUE (nano::validate_message (key.pub, message, flipped_point));
	auto flipped_scalar = signature;
	flipped_scalar.bytes[32] ^= 0x1;
	ASSERT_TRUE (nano::validate_message (key.pub, message, flipped_scalar));
}

/*
 * The scalar half of a signature is taken modulo the group order, so adding the order to it gives a second signature for the same key and message,
 * which anyone can make without the key. Block and vote hashes do not cover the signature, so the twin stands for the same block or vote in other bytes.
 * The dev genesis block stands in for any signed block: its signature is a fixed one, and so is the twin.
 * Only a scalar so close to the order that the sum reaches the verifier's limit has no twin, see scalar_cutoff.
 * Stricter implementations refuse a scalar at or above the order
 */
TEST (ed25519, scalar_above_group_order)
{
	auto const signer = nano::dev::genesis_key.pub;
	auto const message = nano::dev::genesis->hash ();
	auto const signature = nano::dev::genesis->block_signature ();
	ASSERT_FALSE (nano::validate_message (signer, message, signature));

	// The scalar plus the group order is another signature that verifies
	auto const twin = add_group_order (signature);
	ASSERT_NE (signature, twin);
	ASSERT_FALSE (nano::validate_message (signer, message, twin));
}

/*
 * The scalar half of a signature has to stay below 2^253, a limit that lies between the group order and twice the group order.
 * Under the identity key a scalar of choice signs every message together with the point it multiplies the base point to, which puts the limit within reach of a test:
 * 2^253 - 1 verifies, and 2^253 does not, although its point verifies with the same scalar reduced modulo the order.
 * That reduced scalar is below the order and has no twin, since the order added to it is the refused 2^253.
 * A verifier that stopped the scalar at the order, or only at twice the order, would answer one of these differently
 */
TEST (ed25519, scalar_cutoff)
{
	nano::public_key const identity{ "0100000000000000000000000000000000000000000000000000000000000000" };
	nano::uint256_union const message{ 1 };

	// Scalars as a signature carries them, little endian: 2^253 - 1, 2^253 and 2^253 less the group order
	nano::uint256_union const scalar_below{ "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1f" };
	nano::uint256_union const scalar_at{ "0000000000000000000000000000000000000000000000000000000000000020" };
	nano::uint256_union const scalar_reduced{ "132c0aa3e59ceda72963085d210621ebffffffffffffffffffffffffffffff0f" };

	// The base point times 2^253 - 1 and times 2^253
	nano::uint256_union const point_below{ "d627ed97d4b0c8f079c3a234c4faa67d634314ed5ce6a59b4a86566b49f1f838" };
	nano::uint256_union const point_at{ "2c6cf766a4626b39b3ba65d31cf811aabedc805987f57be5e3b33e39dabe8809" };

	// The largest scalar below the limit verifies
	nano::signature const below{ /* upper */ point_below, /* lower */ scalar_below };
	ASSERT_FALSE (nano::validate_message (identity, message, below));

	// The scalar at the limit does not verify
	nano::signature const at{ /* upper */ point_at, /* lower */ scalar_at };
	ASSERT_TRUE (nano::validate_message (identity, message, at));

	// Its point verifies with the scalar reduced, so the limit alone refused it, and the reduced scalar's twin is the refused signature
	nano::signature const reduced{ /* upper */ point_at, /* lower */ scalar_reduced };
	ASSERT_FALSE (nano::validate_message (identity, message, reduced));
	ASSERT_EQ (at, add_group_order (reduced));
}

/*
 * Public keys of small order are accepted, and under them a signature made without any private key verifies whenever the message digest,
 * reduced modulo the group order, is a multiple of the key's order.
 * Under the identity key that is every message. Under the all-zero key of the burn account, a point of order four, it is about one message in four,
 * so anyone can sign in the burn account's name by varying a message until it verifies, and only the explicit burn account checks protect that account.
 * Stricter implementations refuse such keys, one that clears the cofactor would accept every message under the zero key,
 * and one that multiplied the key by the unreduced digest would accept other messages than the ones listed here
 */
TEST (ed25519, small_order_key)
{
	auto const keyless = nano::test::keyless_signature ();
	nano::public_key const identity{ "0100000000000000000000000000000000000000000000000000000000000000" };
	nano::public_key const zero{ 0 };
	nano::keypair held;

	// The messages out of 0 to 63 whose reduced digest is a multiple of four
	std::set<unsigned> const zero_expected{ 3, 5, 6, 18, 24, 30, 43, 45, 48, 54, 58, 61, 63 };

	// The same signature against the run of messages under each key
	for (unsigned index = 0; index < 64; ++index)
	{
		nano::uint256_union const message{ index };

		// Every message verifies under the identity key
		ASSERT_FALSE (nano::validate_message (identity, message, keyless)) << "message " << index;

		// Exactly the expected messages verify under the zero key
		auto const zero_verified = !nano::validate_message (zero, message, keyless);
		ASSERT_EQ (zero_expected.contains (index), zero_verified) << "message " << index;

		// No message verifies under a key somebody holds
		ASSERT_TRUE (nano::validate_message (held.pub, message, keyless)) << "message " << index;
	}
}

/*
 * A public key does not have to be the canonical encoding of its point, while the point half of a signature does.
 * The identity point with its y coordinate written as p + 1 instead of 1 is accepted as a public key and behaves like the identity key,
 * yet a signature that carries the same bytes as its point half is compared against the canonical encoding and does not verify
 */
TEST (ed25519, non_canonical_encoding)
{
	auto const keyless = nano::test::keyless_signature ();
	nano::public_key const identity{ "0100000000000000000000000000000000000000000000000000000000000000" };
	nano::public_key const non_canonical{ "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f" };
	nano::uint256_union const message{ 1 };

	// As a public key the non-canonical identity takes the keyless signature like the canonical one
	ASSERT_FALSE (nano::validate_message (identity, message, keyless));
	ASSERT_FALSE (nano::validate_message (non_canonical, message, keyless));

	// As the point half of a signature it does not verify under either encoding of the key
	nano::signature const non_canonical_point{ /* upper */ non_canonical, /* lower */ nano::uint256_union{ 0 } };
	ASSERT_TRUE (nano::validate_message (identity, message, non_canonical_point));
	ASSERT_TRUE (nano::validate_message (non_canonical, message, non_canonical_point));
}
