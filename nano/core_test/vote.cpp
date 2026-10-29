#include <nano/lib/blocks.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
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
 * A vote can be signed over an empty hash set: it validates, takes only the fixed part of the wire format and reads back equal
 */
TEST (vote, empty_hashes)
{
	// No hash at all is within the vote's limits and signs into a valid vote
	nano::keypair key;
	nano::vote const vote{ key.pub, key.prv, 0, 0, std::vector<nano::block_hash>{} /* empty */ };
	ASSERT_FALSE (vote.validate ()); // validate returns false for a valid signature

	// Serialized, it is exactly the fixed part: account, signature and timestamp
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		vote.serialize (stream);
	}
	ASSERT_EQ (nano::vote::size (0), bytes.size ());

	// It reads back equal, with no hashes
	bool error = false;
	nano::bufferstream stream (bytes.data (), bytes.size ());
	nano::vote const read{ error, stream };
	ASSERT_FALSE (error);
	ASSERT_TRUE (read.hashes.empty ());
	ASSERT_EQ (vote, read);
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

/*
 * A vote with hashes serializes to its declared size and reads back equal to the last byte, still carrying a valid signature
 */
TEST (vote, serialization_round_trip)
{
	// A signed vote over three hashes with a timestamp and a duration
	nano::keypair key;
	std::vector<nano::block_hash> const hashes{ nano::block_hash{ 1 }, nano::block_hash{ 2 }, nano::block_hash{ 3 } };
	nano::vote const vote{ key.pub, key.prv, 0x1230, 2, hashes };

	// The wire size is the fixed part plus one hash each
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		vote.serialize (stream);
	}
	ASSERT_EQ (nano::vote::size (3), bytes.size ());

	// Reading consumes every byte and gives back the same vote, signature included
	bool error = false;
	nano::bufferstream stream (bytes.data (), bytes.size ());
	nano::vote const read{ error, stream };
	ASSERT_FALSE (error);
	ASSERT_TRUE (nano::at_end (stream));
	ASSERT_EQ (vote, read);
	ASSERT_EQ (hashes, read.hashes);
	ASSERT_EQ (0x1230, read.timestamp ());
	ASSERT_EQ (2, read.duration_bits ());
	ASSERT_FALSE (read.validate ()); // validate returns false for a valid signature
}

/*
 * A stream that ends inside the fixed part or inside a hash is an error, while one ending right after a hash reads as a complete vote with the hashes before the cut
 */
TEST (vote, deserialize_truncated)
{
	// The bytes of a valid vote over two hashes
	nano::keypair key;
	nano::vote const vote{ key.pub, key.prv, 0x1230, 0, std::vector<nano::block_hash>{ nano::block_hash{ 1 }, nano::block_hash{ 2 } } };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		vote.serialize (stream);
	}

	// Reads the first length bytes as a vote, giving its hash count or nothing on error
	auto read = [&] (std::size_t length) -> std::optional<std::size_t> {
		bool error = false;
		nano::bufferstream stream (bytes.data (), length);
		nano::vote const read{ error, stream };
		if (error)
		{
			return std::nullopt;
		}
		return read.hashes.size ();
	};

	// Cut inside the account, the signature or the timestamp, nothing reads
	ASSERT_FALSE (read (0));
	ASSERT_FALSE (read (sizeof (nano::account) - 1));
	ASSERT_FALSE (read (sizeof (nano::account) + sizeof (nano::signature) - 1));
	ASSERT_FALSE (read (nano::vote::size (0) - 1));

	// Cut inside a hash, the vote is an error too
	ASSERT_FALSE (read (nano::vote::size (1) - 1));
	ASSERT_FALSE (read (nano::vote::size (2) - 1));

	// Cut right after a hash, the vote is complete with the hashes before the cut
	ASSERT_EQ (std::size_t{ 0 }, read (nano::vote::size (0)));
	ASSERT_EQ (std::size_t{ 1 }, read (nano::vote::size (1)));
	ASSERT_EQ (std::size_t{ 2 }, read (nano::vote::size (2)));
}

/*
 * Reading stops at max_hashes: a stream holding more leaves the extra hashes unread instead of building an oversized vote
 */
TEST (vote, deserialize_hash_cap)
{
	// The fixed part of a vote followed by one hash more than a vote holds
	nano::keypair key;
	nano::vote const vote{ key.pub, key.prv, 0, 0, std::vector<nano::block_hash>{} };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		vote.serialize (stream);
		for (uint64_t i = 0; i < nano::vote::max_hashes + 1; ++i)
		{
			nano::write (stream, nano::block_hash{ i + 1 });
		}
	}

	// The vote reads the first max_hashes in order and leaves exactly one hash in the stream
	bool error = false;
	nano::bufferstream stream (bytes.data (), bytes.size ());
	nano::vote const read{ error, stream };
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::vote::max_hashes, read.hashes.size ());
	ASSERT_EQ (nano::block_hash{ 1 }, read.hashes.front ());
	ASSERT_EQ (nano::block_hash{ nano::vote::max_hashes }, read.hashes.back ());
	ASSERT_EQ (sizeof (nano::block_hash), static_cast<std::size_t> (stream.in_avail ()));
}

/*
 * The signed hash covers the hashes, their order and the packed timestamp, and nothing else: the account and the signature stay out of it
 */
TEST (vote, hash_covers_hashes_and_timestamp)
{
	// A vote over two hashes
	nano::keypair key;
	std::vector<nano::block_hash> const hashes{ nano::block_hash{ 1 }, nano::block_hash{ 2 } };
	nano::vote const original{ key.pub, key.prv, 0x1230, 1, hashes };
	auto const hash = original.hash ();

	// Another hash set, the same hashes in the other order, another timestamp and another duration all hash differently
	nano::vote const other_hashes{ key.pub, key.prv, 0x1230, 1, std::vector<nano::block_hash>{ nano::block_hash{ 1 }, nano::block_hash{ 3 } } };
	nano::vote const reordered{ key.pub, key.prv, 0x1230, 1, std::vector<nano::block_hash>{ nano::block_hash{ 2 }, nano::block_hash{ 1 } } };
	nano::vote const other_timestamp{ key.pub, key.prv, 0x1240, 1, hashes };
	nano::vote const other_duration{ key.pub, key.prv, 0x1230, 2, hashes };
	ASSERT_NE (hash, other_hashes.hash ());
	ASSERT_NE (hash, reordered.hash ());
	ASSERT_NE (hash, other_timestamp.hash ());
	ASSERT_NE (hash, other_duration.hash ());

	// The low bits of the timestamp argument are masked off before packing, so they do not reach the hash
	nano::vote const low_bits{ key.pub, key.prv, 0x123f, 1, hashes };
	ASSERT_EQ (hash, low_bits.hash ());

	// Another signer over the same content hashes alike, so the hash alone does not identify a vote
	nano::keypair other;
	nano::vote const other_signer{ other.pub, other.prv, 0x1230, 1, hashes };
	ASSERT_EQ (hash, other_signer.hash ());

	// Changing the account and the signature of a copy leaves the hash alone
	nano::vote changed{ original };
	changed.account.bytes[0] ^= 1;
	changed.signature.bytes[0] ^= 1;
	ASSERT_EQ (hash, changed.hash ());
}

/*
 * validate accepts a vote only as signed: a changed hash, account or timestamp, a damaged signature and a signature by another key all fail
 */
TEST (vote, validate_rejects_tampering)
{
	// A vote signed by its account
	nano::keypair key;
	std::vector<nano::block_hash> const hashes{ nano::block_hash{ 1 }, nano::block_hash{ 2 } };
	nano::vote const original{ key.pub, key.prv, 0x1230, 0, hashes };
	ASSERT_FALSE (original.validate ()); // validate returns false for a valid signature

	// A changed hash
	nano::vote tampered_hash{ original };
	tampered_hash.hashes[1].bytes[0] ^= 1;
	ASSERT_TRUE (tampered_hash.validate ());

	// Another account under the same signature
	nano::vote tampered_account{ original };
	tampered_account.account = nano::keypair{}.pub;
	ASSERT_TRUE (tampered_account.validate ());

	// A damaged signature
	nano::vote tampered_signature{ original };
	tampered_signature.signature.bytes[0] ^= 1;
	ASSERT_TRUE (tampered_signature.validate ());

	// The signature carried over to a vote with another timestamp
	nano::vote other_timestamp{ key.pub, key.prv, 0x1240, 0, hashes };
	other_timestamp.signature = original.signature;
	ASSERT_TRUE (other_timestamp.validate ());

	// A vote signed with a key that is not the account's
	nano::keypair other;
	nano::vote const wrong_signer{ key.pub, other.prv, 0x1230, 0, hashes };
	ASSERT_TRUE (wrong_signer.validate ());
}

/*
 * Two votes are equal only when every field agrees: hashes, account, signature and the packed timestamp
 */
TEST (vote, equality)
{
	// A vote and its copy
	nano::keypair key;
	std::vector<nano::block_hash> const hashes{ nano::block_hash{ 1 } };
	nano::vote const original{ key.pub, key.prv, 0x1230, 0, hashes };
	nano::vote const copy{ original };
	ASSERT_EQ (original, copy);
	ASSERT_FALSE (original != copy);

	// Each field on its own breaks equality
	nano::vote other_hashes{ original };
	other_hashes.hashes.emplace_back (2);
	ASSERT_NE (original, other_hashes);

	nano::vote other_account{ original };
	other_account.account.bytes[0] ^= 1;
	ASSERT_NE (original, other_account);

	nano::vote other_signature{ original };
	other_signature.signature.bytes[0] ^= 1;
	ASSERT_NE (original, other_signature);

	// Another duration alone changes the packed timestamp
	nano::vote other_duration{ key.pub, key.prv, 0x1230, 1, hashes };
	other_duration.signature = original.signature;
	ASSERT_NE (original, other_duration);

	// Signing draws a fresh nonce, so the same content signed again hashes alike but is not the same vote
	nano::vote const signed_again{ key.pub, key.prv, 0x1230, 0, hashes };
	ASSERT_EQ (original.hash (), signed_again.hash ());
	ASSERT_NE (original, signed_again);
}

/*
 * A final vote is the all-ones packed timestamp: timestamp () returns the marker whole, duration bits included, and is_final recognizes it
 */
TEST (vote, final_vote)
{
	// A final vote: the highest timestamp with the highest duration exponent
	nano::keypair key;
	std::vector<nano::block_hash> const hashes{ nano::block_hash{ 1 } };
	nano::vote const final{ key.pub, key.prv, nano::vote::timestamp_max, nano::vote::duration_max, hashes };
	ASSERT_TRUE (final.is_final ());
	ASSERT_EQ (nano::vote::timestamp_final, final.timestamp ());
	ASSERT_EQ (nano::vote::duration_max, final.duration_bits ());

	// The test helper makes the same final vote, up to its fresh signature
	auto const helper = nano::test::make_final_vote (key, hashes);
	ASSERT_TRUE (helper->is_final ());
	ASSERT_EQ (final.hash (), helper->hash ());

	// The highest duration on any other timestamp is not final, and that timestamp reads back masked as usual
	nano::vote const long_lived{ key.pub, key.prv, nano::vote::timestamp_max - nano::vote::timestamp_min, nano::vote::duration_max, hashes };
	ASSERT_FALSE (long_lived.is_final ());
	ASSERT_EQ (nano::vote::timestamp_max - nano::vote::timestamp_min, long_lived.timestamp ());

	// The marker check on raw packed timestamps
	ASSERT_TRUE (nano::vote::is_final_timestamp (nano::vote::timestamp_final));
	ASSERT_FALSE (nano::vote::is_final_timestamp (nano::vote::timestamp_max));
	ASSERT_FALSE (nano::vote::is_final_timestamp (nano::vote::timestamp_final - 1));
}

/*
 * Each duration exponent doubles the duration, from 16 ms at zero to 2^19 ms at the highest, and reads back as the bits it was packed with
 */
TEST (vote, duration_exponents)
{
	nano::keypair key;
	std::vector<nano::block_hash> const hashes{ nano::block_hash{ 1 } };
	for (uint8_t exponent = 0; exponent <= nano::vote::duration_max; ++exponent)
	{
		nano::vote const vote{ key.pub, key.prv, 0x1230, exponent, hashes };
		ASSERT_EQ (exponent, vote.duration_bits ());
		ASSERT_EQ (std::chrono::milliseconds{ 16 << exponent }, vote.duration ());
		ASSERT_EQ (0x1230, vote.timestamp ());
	}
}

/*
 * The JSON form carries the account, the signature, the masked timestamp (also as the legacy sequence), the duration exponent and the hashes in order
 */
TEST (vote, json)
{
	// A vote over two hashes with a duration, its timestamp argument carrying low bits
	nano::keypair key;
	std::vector<nano::block_hash> const hashes{ nano::block_hash{ 1 }, nano::block_hash{ 2 } };
	nano::vote const vote{ key.pub, key.prv, 0x123f, 3, hashes };

	// Parsed back, each field reads as the vote's own formatting of it
	boost::property_tree::ptree tree;
	std::stringstream stream{ vote.to_json () };
	boost::property_tree::read_json (stream, tree);
	ASSERT_EQ (key.pub.to_account (), tree.get<std::string> ("account"));
	ASSERT_EQ (vote.signature.to_string (), tree.get<std::string> ("signature"));
	ASSERT_EQ (std::to_string (0x1230), tree.get<std::string> ("timestamp"));
	ASSERT_EQ (tree.get<std::string> ("timestamp"), tree.get<std::string> ("sequence"));
	ASSERT_EQ ("3", tree.get<std::string> ("duration"));

	// The hashes come as an array in their order
	std::vector<std::string> blocks;
	for (auto const & entry : tree.get_child ("blocks"))
	{
		blocks.push_back (entry.second.get_value<std::string> ());
	}
	ASSERT_EQ ((std::vector<std::string>{ hashes[0].to_string (), hashes[1].to_string () }), blocks);
}

/*
 * The duration exponent has four bits, and the maximum timestamp is reserved for final votes: any other duration on it would make a non-final vote outranking every real timestamp.
 * Both checks hold in release builds, since a vote signed past them is ranked by the whole network, so each gets a death test.
 */
TEST (vote_DeathTest, duration_exponent_past_four_bits)
{
	nano::keypair key;
	ASSERT_DEATH_IF_SUPPORTED ((nano::vote{ key.pub, key.prv, 0x1230, nano::vote::duration_max + 1, { nano::block_hash{ 1 } } }), "does not fit in the four duration bits");
}

TEST (vote_DeathTest, max_timestamp_without_final_duration)
{
	nano::keypair key;
	ASSERT_DEATH_IF_SUPPORTED ((nano::vote{ key.pub, key.prv, nano::vote::timestamp_max, 0, { nano::block_hash{ 1 } } }), "reserved for final votes");
}

TEST (vote_DeathTest, final_marker_bits_without_final_duration)
{
	// The low bits of the timestamp argument are masked off, so the all-ones marker with another duration is the maximum timestamp too
	nano::keypair key;
	ASSERT_DEATH_IF_SUPPORTED ((nano::vote{ key.pub, key.prv, nano::vote::timestamp_final, 0, { nano::block_hash{ 1 } } }), "reserved for final votes");
}

/*
 * vote_uniquer tests
 */

/*
 * A null vote passes through the uniquer untouched
 */
TEST (vote_uniquer, null)
{
	nano::vote_uniquer uniquer;
	ASSERT_EQ (nullptr, uniquer.unique (nullptr));
}

/*
 * Two instances of one vote unique to the instance seen first
 */
TEST (vote_uniquer, same_vote)
{
	// A vote and a second instance equal to it
	nano::vote_uniquer uniquer;
	nano::keypair key;
	auto vote1 = nano::test::make_vote (key, std::vector<nano::block_hash>{ nano::block_hash{ 1 } }, 0, 0);
	auto vote2 = std::make_shared<nano::vote> (*vote1);

	// The first instance is recorded, the second resolves to it
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

/*
 * Votes over different hashes stay distinct instances
 */
TEST (vote_uniquer, different_votes)
{
	nano::vote_uniquer uniquer;
	nano::keypair key;
	auto vote1 = nano::test::make_vote (key, std::vector<nano::block_hash>{ nano::block_hash{ 1 } }, 0, 0);
	auto vote2 = nano::test::make_vote (key, std::vector<nano::block_hash>{ nano::block_hash{ 2 } }, 0, 0);
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
}

/*
 * Entries whose vote nobody holds any more are dropped on the first unique call after the cleanup cutoff
 */
TEST (vote_uniquer, cleanup)
{
	// Two distinct votes recorded, then one of them released by everyone
	nano::vote_uniquer uniquer;
	nano::keypair key;
	auto vote1 = nano::test::make_vote (key, std::vector<nano::block_hash>{ nano::block_hash{ 0 } }, 0, 0);
	auto vote2 = nano::test::make_vote (key, std::vector<nano::block_hash>{ nano::block_hash{ 0 } }, nano::vote::timestamp_min, 0);
	auto vote3 = uniquer.unique (vote1);
	auto vote4 = uniquer.unique (vote2);
	vote2.reset ();
	vote4.reset ();
	ASSERT_EQ (2, uniquer.size ());

	// After the cutoff, the next call sweeps the expired entry
	std::this_thread::sleep_for (nano::vote_uniquer::cleanup_cutoff);
	auto vote5 = uniquer.unique (vote1);
	ASSERT_EQ (1, uniquer.size ());
}
