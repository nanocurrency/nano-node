#pragma once

#include <nano/lib/numbers.hpp>

namespace nano
{
/** A message together with the key said to have signed it and the signature, none of it verified */
struct signed_message
{
	nano::public_key signer;
	nano::uint256_union message;
	nano::signature signature;

	bool operator== (signed_message const &) const = default;
};

/**
 * Verdict on a signed message, obtained only by verifying it here or, in tests, by forging one.
 * Keeps the message it judged, so a consumer can make sure the verdict is about exactly the message it holds.
 */
class signature_verification final
{
public:
	/// Verifies the signature against the signer, the one place that produces verdicts outside tests
	static signature_verification verify (nano::signed_message const &);
	/// Forges a verdict with the given outcome, for tests only
	static signature_verification dummy (nano::signed_message const &, bool valid);

	/// True when the verdict is about exactly this signer, message and signature
	bool covers (nano::signed_message const &) const;
	/// The signer, message and signature the verdict is about
	nano::signed_message const & subject () const;
	/// Whether the signature verified
	bool valid () const;

private:
	signature_verification (nano::signed_message const &, bool valid);

	nano::signed_message message_m;
	bool valid_m;
};
}
