#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/signature_verifier.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace
{
// Signs a distinct message per entry with a fresh key, flipping a bit in every tamper_every-th signature
std::vector<nano::signed_message> make_batch (std::size_t count, std::size_t tamper_every)
{
	std::vector<nano::signed_message> batch;
	for (std::size_t i = 0; i < count; ++i)
	{
		nano::keypair key;
		nano::uint256_union const message{ static_cast<uint64_t> (i + 1) };
		nano::signed_message entry{ .signer = key.pub, .message = message, .signature = nano::sign_message (key.prv, key.pub, message) };
		if (tamper_every > 0 && i % tamper_every == 0)
		{
			entry.signature.bytes[0] ^= 1;
		}
		batch.push_back (entry);
	}
	return batch;
}

// Every verdict must be about its own message and say what the signature deserves
void expect_verdicts (std::vector<nano::signed_message> const & batch, std::vector<nano::signature_verification> const & verdicts, std::size_t tamper_every)
{
	ASSERT_EQ (batch.size (), verdicts.size ());
	for (std::size_t i = 0; i < batch.size (); ++i)
	{
		ASSERT_TRUE (verdicts[i].covers (batch[i])) << "entry " << i;
		ASSERT_EQ (tamper_every == 0 || i % tamper_every != 0, verdicts[i].valid ()) << "entry " << i;
	}
}
}

/*
 * Without extra threads the calling thread verifies the whole batch, an empty batch yields no verdicts
 */
TEST (signature_verifier, calling_thread_only)
{
	nano::signature_verifier verifier{ 0 };
	nano::test::start_stop_guard guard{ verifier };

	auto const batch = make_batch (10, 3);
	expect_verdicts (batch, verifier.verify (batch), 3);

	ASSERT_TRUE (verifier.verify ({}).empty ());
}

/*
 * With extra threads an uneven batch is split between them and the calling thread, every message gets its verdict in order
 */
TEST (signature_verifier, parallel)
{
	nano::signature_verifier verifier{ 3 };
	nano::test::start_stop_guard guard{ verifier };

	auto const batch = make_batch (101, 7);
	expect_verdicts (batch, verifier.verify (batch), 7);

	// The pool is reused across batches
	auto const second = make_batch (33, 2);
	expect_verdicts (second, verifier.verify (second), 2);
}

/*
 * A batch too small to share stays on the calling thread and is still verified
 */
TEST (signature_verifier, small_batch)
{
	nano::signature_verifier verifier{ 3 };
	nano::test::start_stop_guard guard{ verifier };

	auto const batch = make_batch (nano::signature_verifier::min_share - 1, 2);
	expect_verdicts (batch, verifier.verify (batch), 2);
}

/*
 * A well-formed signature from another key fails against the named signer
 */
TEST (signature_verifier, wrong_signer)
{
	nano::signature_verifier verifier{ 0 };
	nano::test::start_stop_guard guard{ verifier };

	auto batch = make_batch (1, 0);
	batch[0].signer = nano::keypair ().pub;
	auto const verdicts = verifier.verify (batch);
	ASSERT_FALSE (verdicts[0].valid ());
	ASSERT_TRUE (verdicts[0].covers (batch[0]));
}
