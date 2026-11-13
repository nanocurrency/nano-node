#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/traffic_type.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace
{
// An entry whose buffer size identifies it, so tests can follow the order entries come out in
nano::transport::tcp_channel_queue::entry_t make_entry (size_t id)
{
	return { nano::shared_const_buffer{ std::vector<uint8_t> (id) }, nullptr };
}

// The traffic types of a batch, in the order it holds them
std::vector<nano::transport::traffic_type> types_of (nano::transport::tcp_channel_queue::batch_t const & batch)
{
	std::vector<nano::transport::traffic_type> result;
	for (auto const & [type, entry] : batch)
	{
		result.push_back (type);
	}
	return result;
}
}

/*
 * A new queue is empty for every traffic type and hands out empty batches
 */
TEST (tcp_channel_queue, empty)
{
	nano::transport::tcp_channel_queue queue;

	ASSERT_TRUE (queue.empty ());
	ASSERT_EQ (0, queue.size ());
	ASSERT_EQ (0, queue.size (nano::transport::traffic_type::generic));
	ASSERT_FALSE (queue.max (nano::transport::traffic_type::generic));
	ASSERT_FALSE (queue.full (nano::transport::traffic_type::generic));
	ASSERT_TRUE (queue.next_batch (8).empty ());
}

/*
 * Each traffic type has its own limits: `max` reports it at its soft limit and `full` at its hard limit, while other types keep their room
 */
TEST (tcp_channel_queue, limits_per_type)
{
	nano::transport::tcp_channel_queue queue;

	// One entry short of the soft limit
	for (size_t i = 0; i + 1 < nano::transport::tcp_channel_queue::max_size; ++i)
	{
		queue.push (nano::transport::traffic_type::generic, make_entry (1));
	}
	ASSERT_FALSE (queue.max (nano::transport::traffic_type::generic));

	// At the soft limit, but not full
	queue.push (nano::transport::traffic_type::generic, make_entry (1));
	ASSERT_TRUE (queue.max (nano::transport::traffic_type::generic));
	ASSERT_FALSE (queue.full (nano::transport::traffic_type::generic));

	// Filled to the hard limit
	while (queue.size (nano::transport::traffic_type::generic) < nano::transport::tcp_channel_queue::full_size)
	{
		queue.push (nano::transport::traffic_type::generic, make_entry (1));
	}
	ASSERT_TRUE (queue.full (nano::transport::traffic_type::generic));

	// Another type is unaffected and the totals add up
	ASSERT_FALSE (queue.max (nano::transport::traffic_type::vote));
	queue.push (nano::transport::traffic_type::vote, make_entry (1));
	ASSERT_EQ (1, queue.size (nano::transport::traffic_type::vote));
	ASSERT_EQ (nano::transport::tcp_channel_queue::full_size + 1, queue.size ());
}

/*
 * Entries of one traffic type come out in the order they were pushed, and a batch takes no more than asked for, leaving the rest queued
 */
TEST (tcp_channel_queue, batch_order_and_limit)
{
	nano::transport::tcp_channel_queue queue;
	for (size_t id = 1; id <= 6; ++id)
	{
		queue.push (nano::transport::traffic_type::generic, make_entry (id));
	}

	// The first batch holds the first three, in order
	auto const first = queue.next_batch (3);
	ASSERT_EQ (3, first.size ());
	for (size_t i = 0; i < first.size (); ++i)
	{
		ASSERT_EQ (i + 1, first[i].second.first.size ());
	}
	ASSERT_EQ (3, queue.size ());

	// The rest follows in the next batch
	auto const second = queue.next_batch (8);
	ASSERT_EQ (3, second.size ());
	ASSERT_EQ (4, second.front ().second.first.size ());
	ASSERT_TRUE (queue.empty ());
}

/*
 * Traffic types share the channel in turns of four entries each
 */
TEST (tcp_channel_queue, round_robin)
{
	using enum nano::transport::traffic_type;
	nano::transport::tcp_channel_queue queue;
	for (size_t i = 0; i < 6; ++i)
	{
		queue.push (generic, make_entry (1));
		queue.push (vote, make_entry (1));
	}

	auto const batch = queue.next_batch (12);

	std::vector<nano::transport::traffic_type> const expected{ generic, generic, generic, generic, vote, vote, vote, vote, generic, generic, vote, vote };
	ASSERT_EQ (expected, types_of (batch));
}

/*
 * Block broadcasts and vote rebroadcasts get one entry per turn instead of four, so they cannot crowd out other traffic,
 * and once the other traffic is drained they take every turn
 */
TEST (tcp_channel_queue, lower_priority_types)
{
	using enum nano::transport::traffic_type;
	nano::transport::tcp_channel_queue queue;
	for (size_t i = 0; i < 8; ++i)
	{
		queue.push (generic, make_entry (1));
		queue.push (block_broadcast, make_entry (1));
	}

	auto const batch = queue.next_batch (16);

	std::vector<nano::transport::traffic_type> const expected{
		generic, generic, generic, generic, block_broadcast,
		generic, generic, generic, generic, block_broadcast,
		block_broadcast, block_broadcast, block_broadcast, block_broadcast, block_broadcast, block_broadcast
	};
	ASSERT_EQ (expected, types_of (batch));
}
