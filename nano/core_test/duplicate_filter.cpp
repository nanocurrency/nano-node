#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/duplicate_filter.hpp>
#include <nano/messages/messages.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

namespace
{
// The part of a serialized publish that follows its header, which is what the filter sees on the network
std::vector<uint8_t> publish_payload (std::shared_ptr<nano::block> const & block)
{
	nano::messages::publish const message{ nano::dev::network_params.network, block };
	auto const bytes = message.to_bytes ();
	return { bytes->begin () + nano::messages::message_header::size, bytes->end () };
}

// A send from genesis, a different block for each amount
std::shared_ptr<nano::block> make_send (nano::uint128_t const & amount)
{
	nano::state_block_builder builder;
	return builder
	.account (nano::dev::genesis_key.pub)
	.previous (nano::dev::genesis->hash ())
	.representative (nano::dev::genesis_key.pub)
	.balance (nano::dev::constants.genesis_amount - amount)
	.link (nano::dev::genesis_key.pub)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (0)
	.build ();
}
}

/*
 * A digest goes in as new the first time and is a duplicate after that, until it is erased
 */
TEST (duplicate_filter, insert)
{
	nano::duplicate_filter filter{ /* size */ 4 };

	// A digest the filter has not seen is absent and goes in as new
	ASSERT_FALSE (filter.contains (34));
	ASSERT_TRUE (filter.insert (34));

	// Once in, it is present and inserting it again reports a duplicate
	ASSERT_TRUE (filter.contains (34));
	ASSERT_FALSE (filter.insert (34));

	// Erasing it lets it in as new again
	filter.erase (34);
	ASSERT_FALSE (filter.contains (34));
	ASSERT_TRUE (filter.insert (34));
}

/*
 * With a single slot, a repeated publish is a duplicate until a different block takes the slot, after which the first one goes in as new again
 */
TEST (duplicate_filter, single_slot)
{
	nano::duplicate_filter filter{ /* size */ 1 };
	auto const genesis = publish_payload (nano::dev::genesis);
	auto const send = publish_payload (make_send (/* amount */ 1000));

	// Repeats of the genesis publish are duplicates while it holds the slot
	ASSERT_TRUE (filter.insert (genesis));
	for (int i = 0; i < 10; ++i)
	{
		ASSERT_FALSE (filter.insert (genesis));
	}

	// The send takes the slot and is a duplicate in turn
	ASSERT_TRUE (filter.insert (send));
	for (int i = 0; i < 10; ++i)
	{
		ASSERT_FALSE (filter.insert (send));
	}

	// Alternating, each takes the slot from the other, so neither is ever a duplicate
	for (int i = 0; i < 100; ++i)
	{
		ASSERT_TRUE (filter.insert (genesis));
		ASSERT_TRUE (filter.insert (send));
	}
}

/*
 * Distinct publishes are never duplicates, even with many more of them than slots, and each is present right after it goes in
 */
TEST (duplicate_filter, distinct)
{
	nano::duplicate_filter filter{ /* size */ 4 };
	for (int i = 0; i < 100; ++i)
	{
		auto const payload = publish_payload (make_send (i));
		ASSERT_TRUE (filter.insert (payload));
		ASSERT_TRUE (filter.contains (payload));
	}
}

/*
 * Erasing removes only the digest given: other bytes that map to the same slot leave the entry in place
 */
TEST (duplicate_filter, erase)
{
	nano::duplicate_filter filter{ /* size */ 1 };
	std::vector<uint8_t> const bytes1{ 1, 2, 3 };
	std::vector<uint8_t> const bytes2{ 1 };

	// Erasing the entry lets the same bytes in as new
	ASSERT_TRUE (filter.insert (bytes1));
	ASSERT_FALSE (filter.insert (bytes1));
	filter.erase (bytes1);
	ASSERT_TRUE (filter.insert (bytes1));

	// Erasing other bytes, which share the only slot, keeps the entry
	filter.erase (bytes2);
	ASSERT_FALSE (filter.insert (bytes1));
	ASSERT_TRUE (filter.insert (bytes2));
}

/*
 * The digest from hash () is the one the byte overloads use, so a caller can keep it and erase the message by digest later
 * Each filter draws its own key, so the same bytes hash differently in another filter
 */
TEST (duplicate_filter, hash)
{
	nano::duplicate_filter filter{ /* size */ 1 };
	std::vector<uint8_t> const bytes{ 1, 2, 3 };

	// Bytes that went in by their digest are a duplicate when inserted as bytes
	auto const digest = filter.hash (bytes);
	ASSERT_NE (0, digest);
	ASSERT_TRUE (filter.insert (digest));
	ASSERT_FALSE (filter.insert (bytes));

	// Erasing by digest lets the bytes in as new
	filter.erase (digest);
	ASSERT_TRUE (filter.insert (bytes));

	// Another filter's key gives the same bytes another digest
	nano::duplicate_filter const other{ /* size */ 1 };
	ASSERT_NE (other.hash (bytes), digest);
}

/*
 * An entry stops counting as present once more epochs than the age cutoff have passed since it went in, and its digest then goes in as new
 */
TEST (duplicate_filter, expire)
{
	nano::duplicate_filter filter{ /* size */ 4, /* age cutoff */ 2 };

	// Entries go in at epochs 0 and 1
	ASSERT_TRUE (filter.insert (1));
	filter.advance ();
	ASSERT_TRUE (filter.insert (2));
	ASSERT_TRUE (filter.contains (1));
	ASSERT_TRUE (filter.contains (2));

	// At epoch 3 the entry from epoch 0 is past the cutoff and the one from epoch 1 is not
	filter.advance (/* epochs */ 2);
	ASSERT_FALSE (filter.contains (1));
	ASSERT_TRUE (filter.contains (2));

	// At epoch 4 the second entry expires too, and its digest goes in as new
	filter.advance ();
	ASSERT_FALSE (filter.contains (2));
	ASSERT_TRUE (filter.insert (2));
	ASSERT_TRUE (filter.contains (2));
}

/*
 * Clearing removes every entry and leaves the filter taking new ones as before
 */
TEST (duplicate_filter, clear)
{
	nano::duplicate_filter filter{ /* size */ 4 };

	// Entries present before clearing are gone after
	ASSERT_TRUE (filter.insert (1));
	ASSERT_TRUE (filter.insert (2));
	filter.clear ();
	ASSERT_FALSE (filter.contains (1));
	ASSERT_FALSE (filter.contains (2));

	// The filter takes entries as before
	ASSERT_TRUE (filter.insert (1));
	ASSERT_FALSE (filter.insert (1));
}

/*
 * The domain is part of the input: the same bytes in two domains are two entries, each a duplicate only of itself and erased on its own
 */
TEST (duplicate_filter, domain)
{
	nano::duplicate_filter filter{ /* size */ 4 };
	std::vector<uint8_t> const bytes{ 1, 2, 3 };

	// The same bytes get a digest per domain, and leaving the domain out means domain zero
	ASSERT_NE (filter.hash (bytes, /* domain */ 1), filter.hash (bytes, /* domain */ 2));
	ASSERT_EQ (filter.hash (bytes), filter.hash (bytes, /* domain */ 0));

	// Going in under one domain leaves the bytes new to the other
	ASSERT_TRUE (filter.insert (bytes, /* domain */ 1));
	ASSERT_FALSE (filter.insert (bytes, /* domain */ 1));
	ASSERT_FALSE (filter.contains (bytes, /* domain */ 2));
	ASSERT_TRUE (filter.insert (bytes, /* domain */ 2));

	// Erasing under one domain keeps the entry of the other
	filter.erase (bytes, /* domain */ 1);
	ASSERT_FALSE (filter.contains (bytes, /* domain */ 1));
	ASSERT_TRUE (filter.contains (bytes, /* domain */ 2));
}
