#include <nano/lib/block_type.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/network.hpp>
#include <nano/node/transport/message_filter.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace
{
// Network config for a filter with a table of `size` slots
nano::network_config make_config (std::size_t size, uint64_t age_cutoff = 0)
{
	nano::network_config config{ nano::dev::network_params.network };
	config.duplicate_filter_size = size;
	config.duplicate_filter_cutoff = age_cutoff;
	return config;
}

// The part of a serialized message that follows its header, which is what the filter hashes
std::vector<uint8_t> payload_of (nano::messages::message const & message)
{
	auto const bytes = message.to_bytes ();
	return { bytes->begin () + nano::messages::message_header::size, bytes->end () };
}

// A legacy receive block, whose serialized form is as long as a change block and as a vote with one hash
std::shared_ptr<nano::block> make_receive ()
{
	nano::keypair key;
	nano::block_builder builder;
	return builder
	.receive ()
	.previous (nano::test::random_hash ())
	.source (nano::test::random_hash ())
	.sign (key.prv, key.pub)
	.work (0)
	.build ();
}
}

/*
 * A message goes in as new the first time and is a duplicate after that, until its digest is erased
 */
TEST (message_filter, insert)
{
	auto const config = make_config (/* size */ 4);
	nano::transport::message_filter filter{ config };
	nano::messages::publish const publish{ nano::dev::network_params.network, nano::dev::genesis };
	auto const digest = filter.hash (publish.header, payload_of (publish));

	// New the first time, a duplicate from then on
	ASSERT_FALSE (filter.contains (digest));
	ASSERT_TRUE (filter.insert (digest));
	ASSERT_TRUE (filter.contains (digest));
	ASSERT_FALSE (filter.insert (digest));

	// Erasing the digest lets the message in as new
	filter.erase (digest);
	ASSERT_FALSE (filter.contains (digest));
	ASSERT_TRUE (filter.insert (digest));
}

/*
 * What a message is counts along with its payload: the bytes of a receive block also read as a change block and as a vote with one hash, and each of the three has a digest of its own.
 * Each goes through the filter once and is a duplicate only of itself.
 */
TEST (message_filter, same_payload)
{
	auto const & network = nano::dev::network_params.network;
	auto const config = make_config (/* size */ 256);
	nano::transport::message_filter filter{ config };

	// The payload of a publish carrying a receive block, as long as a change block and as a vote with one hash
	nano::messages::publish const publish{ network, make_receive () };
	auto const payload = payload_of (publish);
	ASSERT_EQ (payload.size (), nano::block::size (nano::block_type::change));
	ASSERT_EQ (payload.size (), nano::vote::size (/* count */ 1));

	// Headers under which the same payload is a change block and a vote
	auto change_header = publish.header;
	change_header.block_type_set (nano::block_type::change);
	nano::messages::confirm_ack const confirm_ack{ network, nano::test::make_vote (nano::keypair{}, { nano::test::random_hash () }) };

	// The three readings of the payload have three digests
	auto const receive_digest = filter.hash (publish.header, payload);
	auto const change_digest = filter.hash (change_header, payload);
	auto const vote_digest = filter.hash (confirm_ack.header, payload);
	ASSERT_NE (receive_digest, change_digest);
	ASSERT_NE (receive_digest, vote_digest);
	ASSERT_NE (change_digest, vote_digest);

	// Each is new to the filter whatever went in before it, and a duplicate once it is in
	// A repeat follows its first copy directly, since a later digest can take the slot of an earlier one
	ASSERT_TRUE (filter.insert (receive_digest));
	ASSERT_FALSE (filter.insert (receive_digest));
	ASSERT_TRUE (filter.insert (change_digest));
	ASSERT_FALSE (filter.insert (change_digest));
	ASSERT_TRUE (filter.insert (vote_digest));
	ASSERT_FALSE (filter.insert (vote_digest));
}

/*
 * Only what decides how the payload is read goes into a digest. Flags and versions in the header leave it as it is, so a rebroadcast of a vote is a duplicate of the vote
 */
TEST (message_filter, header_flags)
{
	auto const & network = nano::dev::network_params.network;
	auto const config = make_config (/* size */ 4);
	nano::transport::message_filter filter{ config };

	// A block published by its originator and relayed by another node
	nano::messages::publish const originated{ network, nano::dev::genesis, /* is originator */ true };
	nano::messages::publish const relayed{ network, nano::dev::genesis };
	ASSERT_NE (originated.header.extensions, relayed.header.extensions);
	ASSERT_EQ (filter.hash (originated.header, payload_of (originated)), filter.hash (relayed.header, payload_of (relayed)));

	// A vote and its rebroadcast
	auto const vote = nano::test::make_vote (nano::keypair{}, { nano::test::random_hash () });
	nano::messages::confirm_ack const voted{ network, vote };
	nano::messages::confirm_ack const rebroadcast{ network, vote, /* rebroadcasted */ true };
	ASSERT_NE (voted.header.extensions, rebroadcast.header.extensions);
	ASSERT_EQ (filter.hash (voted.header, payload_of (voted)), filter.hash (rebroadcast.header, payload_of (rebroadcast)));

	// The same vote from a peer on another protocol version
	auto versioned_header = voted.header;
	versioned_header.version_using = voted.header.version_using - 1;
	ASSERT_EQ (filter.hash (voted.header, payload_of (voted)), filter.hash (versioned_header, payload_of (voted)));

	// The block type a vote's header still has room for says nothing about the vote
	auto typed_header = voted.header;
	typed_header.block_type_set (nano::block_type::state);
	ASSERT_NE (voted.header.extensions, typed_header.extensions);
	ASSERT_EQ (filter.hash (voted.header, payload_of (voted)), filter.hash (typed_header, payload_of (voted)));
}

/*
 * A block has the digest of the publish that carries it, which is how elections let a block they gave up on be received again
 */
TEST (message_filter, hash_block)
{
	auto const config = make_config (/* size */ 4);
	nano::transport::message_filter filter{ config };
	nano::messages::publish const publish{ nano::dev::network_params.network, nano::dev::genesis };

	// The block hashes to the digest of its publish
	auto const digest = filter.hash (publish.header, payload_of (publish));
	ASSERT_EQ (digest, filter.hash (*nano::dev::genesis));

	// Erasing the block lets the publish in as new
	ASSERT_TRUE (filter.insert (digest));
	filter.erase (*nano::dev::genesis);
	ASSERT_FALSE (filter.contains (digest));
	ASSERT_TRUE (filter.insert (digest));
}

/*
 * An entry stops counting as present once more epochs than the age cutoff have passed since it went in, and the message then goes in as new
 */
TEST (message_filter, advance)
{
	auto const config = make_config (/* size */ 4, /* age cutoff */ 2);
	nano::transport::message_filter filter{ config };
	auto const digest = filter.hash (*nano::dev::genesis);
	ASSERT_TRUE (filter.insert (digest));

	// Within the cutoff the entry is still present
	filter.advance (/* epochs */ 2);
	ASSERT_TRUE (filter.contains (digest));

	// One epoch past the cutoff it is gone
	filter.advance ();
	ASSERT_FALSE (filter.contains (digest));
	ASSERT_TRUE (filter.insert (digest));
}

/*
 * Blocks and votes are the messages that go through the filter, and with deduplication turned off in the config none does
 */
TEST (message_filter, deduplicates)
{
	auto const config = make_config (/* size */ 4);
	nano::transport::message_filter const filter{ config };

	// Of all the message types only publish and confirm_ack are deduplicated
	for (auto const type : nano::enum_values<nano::messages::message_type> ())
	{
		SCOPED_TRACE (nano::messages::to_string (type));
		auto const expected = type == nano::messages::message_type::publish || type == nano::messages::message_type::confirm_ack;
		ASSERT_EQ (expected, filter.deduplicates (type));
	}

	// Turned off, the filter deduplicates neither
	auto disabled_config = make_config (/* size */ 4);
	disabled_config.duplicate_filter_enable = false;
	nano::transport::message_filter const disabled{ disabled_config };
	ASSERT_FALSE (disabled.deduplicates (nano::messages::message_type::publish));
	ASSERT_FALSE (disabled.deduplicates (nano::messages::message_type::confirm_ack));
}

/*
 * Clearing removes every entry, so a message that was in the filter goes in as new
 */
TEST (message_filter, clear)
{
	auto const config = make_config (/* size */ 4);
	nano::transport::message_filter filter{ config };
	auto const digest = filter.hash (*nano::dev::genesis);
	ASSERT_TRUE (filter.insert (digest));

	// Nothing is left after clearing, and the filter takes entries as before
	filter.clear ();
	ASSERT_FALSE (filter.contains (digest));
	ASSERT_TRUE (filter.insert (digest));
	ASSERT_FALSE (filter.insert (digest));
}

/*
 * The filter is shared by the threads that read messages. Of several threads that insert the same digest at once exactly one is told it is new, while another keeps moving the epoch on.
 * Every round has a digest of its own and a barrier releases the threads into it together, so each round has all of them at the same entry.
 */
TEST (message_filter, concurrent_insert)
{
	constexpr std::size_t thread_count = 4;
	constexpr std::size_t rounds = 100;

	// A cutoff no round reaches keeps the moving epoch from expiring an entry in the middle of a round
	auto const config = make_config (/* size */ 1024, /* age cutoff */ rounds);
	nano::transport::message_filter filter{ config };

	// How many threads were told the round's digest was new, and how often one did not find it right after inserting
	std::array<std::atomic<std::size_t>, rounds> inserted{};
	std::atomic<std::size_t> missing{ 0 };

	std::barrier barrier{ static_cast<std::ptrdiff_t> (thread_count) };
	std::vector<std::thread> threads;
	for (std::size_t id = 0; id < thread_count; ++id)
	{
		threads.emplace_back ([&, id] () {
			for (std::size_t round = 0; round < rounds; ++round)
			{
				barrier.arrive_and_wait ();

				// Digest zero is what an empty slot holds, so the rounds count from one
				nano::filter_digest const digest{ round + 1 };
				if (filter.insert (digest))
				{
					++inserted[round];
				}

				if (!filter.contains (digest))
				{
					++missing;
				}

				// One thread moves the epoch on while the others are still at the entry
				if (id == 0)
				{
					filter.advance ();
				}
			}
		});
	}

	for (auto & thread : threads)
	{
		thread.join ();
	}

	// Each digest went in exactly once and was there for every thread afterwards
	for (std::size_t round = 0; round < rounds; ++round)
	{
		ASSERT_EQ (1, inserted[round].load ());
	}
	ASSERT_EQ (0, missing.load ());
}
