#include <nano/lib/blocks.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <vector>

/*
 * The packed timestamp keeps the duration exponent in its low four bits, which the timestamp masks off and the duration reads back
 */
TEST (vote, timestamp_and_duration_masking)
{
	// A timestamp with its low four bits set, packed with the highest duration exponent
	nano::keypair key;
	auto hash = std::vector<nano::block_hash>{ nano::dev::genesis->hash () };
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, 0x123f, 0xf, hash);

	// The timestamp loses those bits, the exponent 15 reads back and the duration is 2^(15 + 4) ms
	ASSERT_EQ (vote->timestamp (), 0x1230);
	ASSERT_EQ (vote->duration ().count (), 524288);
	ASSERT_EQ (vote->duration_bits (), 0xf);
}

/*
 * A vote can be signed over an empty hash set
 */
TEST (vote, empty_hashes)
{
	// No hash at all is within the vote's limits
	nano::keypair key;
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, 0, 0, std::vector<nano::block_hash>{} /* empty */);
}

/*
 * The full hash of a vote changes with every byte of its account and of its signature
 */
TEST (vote, full_hash_covers_account_and_signature)
{
	// A signed vote over a single hash
	nano::keypair key;
	nano::vote const original{ key.pub, key.prv, 0, 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () } };
	auto const full_hash = original.full_hash ();

	// A copy hashes alike, so any difference below comes from the changed byte alone
	ASSERT_EQ (full_hash, nano::vote{ original }.full_hash ());

	// Changing any single byte of the account changes the full hash
	for (size_t index = 0; index < original.account.bytes.size (); ++index)
	{
		nano::vote changed{ original };
		changed.account.bytes[index] ^= 1;
		ASSERT_NE (full_hash, changed.full_hash ()) << "account byte " << index;
	}

	// Changing any single byte of the signature changes the full hash
	for (size_t index = 0; index < original.signature.bytes.size (); ++index)
	{
		nano::vote changed{ original };
		changed.signature.bytes[index] ^= 1;
		ASSERT_NE (full_hash, changed.full_hash ()) << "signature byte " << index;
	}
}

/*
 * Hashes are split by count alone: a single hash makes a batch of its own, every batch but the last holds max_hashes, and the batches keep the hashes in order
 */
TEST (vote, batch_hashes_count)
{
	// No hashes give no batches, so no vote gets signed
	ASSERT_TRUE (nano::vote::batch_hashes ({}).empty ());

	// A single hash, far below the limit, makes one batch holding just that hash
	std::deque<nano::block_hash> const single{ nano::block_hash{ 1 } };
	auto const one = nano::vote::batch_hashes (single);
	ASSERT_EQ (1, one.size ());
	ASSERT_EQ (1, one[0].size ());
	ASSERT_EQ (single.front (), one[0].front ());

	// Two full votes worth of hashes and one more
	std::deque<nano::block_hash> hashes;
	for (uint64_t i = 0; i < 2 * nano::vote::max_hashes + 1; ++i)
	{
		hashes.emplace_back (i + 1);
	}

	auto const batches = nano::vote::batch_hashes (hashes);

	// Two full batches and the remainder
	ASSERT_EQ (3, batches.size ());
	ASSERT_EQ (nano::vote::max_hashes, batches[0].size ());
	ASSERT_EQ (nano::vote::max_hashes, batches[1].size ());
	ASSERT_EQ (1, batches[2].size ());

	// Joined back together, the batches are the input in its order
	std::deque<nano::block_hash> joined;
	for (auto const & batch : batches)
	{
		joined.insert (joined.end (), batch.begin (), batch.end ());
	}
	ASSERT_EQ (hashes, joined);
}

/*
 * Hashes that fill a vote to its last slot stay in one vote, only the hash after them opens the next, and two votes filled exactly leave no empty third
 */
TEST (vote, batch_hashes_exact_fit)
{
	// Exactly as many hashes as a vote carries
	std::deque<nano::block_hash> hashes;
	for (uint64_t i = 0; i < nano::vote::max_hashes; ++i)
	{
		hashes.emplace_back (i + 1);
	}

	// They make a single full batch
	auto const full = nano::vote::batch_hashes (hashes);
	ASSERT_EQ (1, full.size ());
	ASSERT_EQ (nano::vote::max_hashes, full[0].size ());

	// One more hash goes alone into a second batch, the full one stays as it was
	hashes.emplace_back (9999);
	auto const batches = nano::vote::batch_hashes (hashes);
	ASSERT_EQ (2, batches.size ());
	ASSERT_EQ (full[0], batches[0]);
	ASSERT_EQ (1, batches[1].size ());
	ASSERT_EQ (hashes.back (), batches[1].front ());

	// Filling the second vote to its last slot too gives two full batches and no empty third
	for (uint64_t i = hashes.size (); i < 2 * nano::vote::max_hashes; ++i)
	{
		hashes.emplace_back (i + 1);
	}
	auto const two = nano::vote::batch_hashes (hashes);
	ASSERT_EQ (2, two.size ());
	ASSERT_EQ (full[0], two[0]);
	ASSERT_EQ (nano::vote::max_hashes, two[1].size ());
	ASSERT_EQ (hashes.back (), two[1].back ());
}

/*
 * Every batch signs into a valid vote: a confirm_ack carrying it declares its hash count, the full one through the v2 count, and it reads back signed with the next hashes of the input
 */
TEST (vote, batch_hashes_serialization)
{
	// A full vote worth of hashes and a few more
	nano::keypair key;
	std::deque<nano::block_hash> hashes;
	for (uint64_t i = 0; i < nano::vote::max_hashes + 3; ++i)
	{
		hashes.emplace_back (i + 1);
	}

	auto const batches = nano::vote::batch_hashes (hashes);
	ASSERT_EQ (2, batches.size ());

	std::size_t next{ 0 };
	for (auto const & batch : batches)
	{
		nano::messages::confirm_ack ack{ nano::dev::network_params.network, nano::test::make_final_vote (key, batch) };
		std::vector<uint8_t> bytes;
		{
			nano::vectorstream stream (bytes);
			ack.serialize (stream);
		}

		// The header declares the batch's hash count, through the v2 count once it passes 15
		bool error = false;
		nano::bufferstream stream (bytes.data (), bytes.size ());
		nano::messages::message_header header (error, stream);
		ASSERT_FALSE (error);
		ASSERT_EQ (batch.size () > 15, header.confirm_is_v2 ());
		ASSERT_EQ (batch.size (), header.confirm_is_v2 () ? header.count_v2_get () : header.count_get ());

		// The vote reads back to the last byte with a valid signature, carrying the next hashes of the input
		nano::messages::confirm_ack ack2 (error, stream, header);
		ASSERT_FALSE (error);
		ASSERT_TRUE (nano::at_end (stream));
		ASSERT_FALSE (ack2.vote->validate ()); // validate returns false for a valid signature
		for (auto const & hash : ack2.vote->hashes)
		{
			ASSERT_EQ (hashes[next++], hash);
		}
	}
	ASSERT_EQ (hashes.size (), next);
}
