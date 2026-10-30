#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/signature_verification.hpp>

#include <gtest/gtest.h>

namespace
{
// Signs the given message with the key
nano::signed_message make_signed (nano::keypair const & key, uint64_t message)
{
	nano::uint256_union const hash{ message };
	return { .signer = key.pub, .message = hash, .signature = nano::sign_message (key.prv, key.pub, hash) };
}
}

/*
 * A genuine signature verifies, a tampered one or a wrong signer does not
 */
TEST (signature_verification, verify)
{
	nano::keypair key;
	auto const genuine = make_signed (key, 1);
	ASSERT_TRUE (nano::signature_verification::verify (genuine).valid ());

	auto tampered = genuine;
	tampered.signature.bytes[0] ^= 1;
	ASSERT_FALSE (nano::signature_verification::verify (tampered).valid ());

	auto other_signer = genuine;
	other_signer.signer = nano::keypair ().pub;
	ASSERT_FALSE (nano::signature_verification::verify (other_signer).valid ());
}

/*
 * A verdict covers only the exact signer, message and signature it judged
 */
TEST (signature_verification, covers)
{
	nano::keypair key;
	auto const genuine = make_signed (key, 1);
	auto const verdict = nano::signature_verification::verify (genuine);
	ASSERT_TRUE (verdict.covers (genuine));
	ASSERT_TRUE (verdict.subject () == genuine);

	auto other_signer = genuine;
	other_signer.signer = nano::keypair ().pub;
	ASSERT_FALSE (verdict.covers (other_signer));

	auto other_message = genuine;
	other_message.message = nano::uint256_union{ 2 };
	ASSERT_FALSE (verdict.covers (other_message));

	auto other_signature = genuine;
	other_signature.signature.bytes[0] ^= 1;
	ASSERT_FALSE (verdict.covers (other_signature));
}

/*
 * A dummy carries whatever outcome it was given without verifying anything
 */
TEST (signature_verification, dummy)
{
	nano::keypair key;
	auto tampered = make_signed (key, 1);
	tampered.signature.bytes[0] ^= 1;
	auto const forged = nano::signature_verification::dummy (tampered, true);
	ASSERT_TRUE (forged.valid ());
	ASSERT_TRUE (forged.covers (tampered));

	ASSERT_FALSE (nano::signature_verification::dummy (make_signed (key, 2), false).valid ());
}
