#include <nano/lib/stats.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/node/message_dispatcher.hpp>
#include <nano/node/message_processor.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

using namespace std::chrono_literals;

namespace
{
/*
 * Takes what the processor dispatches: keepalives carrying their channel in the port of their first peer and their sequence in the port of the second.
 * Counts the messages, and the ones dispatched after a later message of their channel.
 */
class recording_dispatcher final : public nano::message_dispatcher
{
public:
	explicit recording_dispatcher (size_t channels) :
		channels (channels)
	{
	}

	void dispatch (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const &) override
	{
		auto const & keepalive = static_cast<nano::messages::keepalive const &> (message);
		auto const sequence = keepalive.peers[1].port ();
		auto & highest = channels[keepalive.peers[0].port ()].highest;
		auto previous = highest.load ();
		while (previous < sequence && !highest.compare_exchange_weak (previous, sequence))
		{
		}
		if (previous > sequence)
		{
			++reordered;
		}
		++dispatched;
	}

	std::unique_ptr<nano::messages::keepalive> make (nano::node & node, uint16_t channel, uint16_t sequence) const
	{
		auto message = std::make_unique<nano::messages::keepalive> (node.network_params.network);
		message->peers[0] = nano::endpoint{ boost::asio::ip::address_v6::any (), channel };
		message->peers[1] = nano::endpoint{ boost::asio::ip::address_v6::any (), sequence };
		return message;
	}

	std::atomic<uint64_t> dispatched{ 0 };
	std::atomic<uint64_t> reordered{ 0 };

private:
	// Padded so that threads dispatching different channels do not share a cache line
	struct alignas (64) channel_state
	{
		std::atomic<uint16_t> highest{ 0 }; // Highest sequence dispatched so far
	};

	std::vector<channel_state> channels;
};
}

/*
 * The messages of one channel are dispatched in the order they were handed over, whichever threads the processor runs.
 * Every channel sends keepalives numbered from 1, interleaved with the other channels as the io threads interleave them, into a processor that has not started yet, so its threads start into full queues as they do in a storm.
 * Not one message may be dispatched after a later one of its channel.
 */
TEST (message_processor, channel_order)
{
	constexpr size_t channel_count = 64;
	constexpr size_t messages_per_channel = 1000;

	nano::test::system system;
	auto & node = *system.add_node ();

	// Enough threads to race, and room for every message so none is dropped
	nano::message_processor_config config;
	config.threads = 4;
	config.max_queue = messages_per_channel;
	recording_dispatcher dispatcher{ channel_count };
	nano::message_processor processor{ config, dispatcher, node.stats, node.logger };

	std::vector<std::shared_ptr<nano::transport::fake::channel>> channels;
	for (size_t i = 0; i < channel_count; ++i)
	{
		channels.push_back (std::make_shared<nano::transport::fake::channel> (node));
	}

	// Hand over one keepalive per channel in turn
	for (size_t sequence = 1; sequence <= messages_per_channel; ++sequence)
	{
		for (size_t channel = 0; channel < channel_count; ++channel)
		{
			ASSERT_TRUE (processor.enqueue (dispatcher.make (node, channel, sequence), channels[channel]));
		}
	}

	// Start into the full queues and wait until every keepalive was dispatched
	processor.start ();
	ASSERT_TIMELY_EQ (10s, dispatcher.dispatched.load (), channel_count * messages_per_channel);
	processor.stop ();

	ASSERT_EQ (0, dispatcher.reordered);
}

/*
 * A channel's queue holds `max_queue` messages and drops the ones beyond, counting them, while another channel of the same thread still gets its messages in.
 */
TEST (message_processor, channel_overfill)
{
	constexpr size_t max_queue = 8;

	nano::test::system system;
	auto & node = *system.add_node ();

	// One thread, so both channels share a shard and the cap can only be the channel's own
	nano::message_processor_config config;
	config.threads = 1;
	config.max_queue = max_queue;
	recording_dispatcher dispatcher{ 2 };
	nano::message_processor processor{ config, dispatcher, node.stats, node.logger };

	auto const full = std::make_shared<nano::transport::fake::channel> (node);
	auto const other = std::make_shared<nano::transport::fake::channel> (node);

	// The channel takes exactly max_queue messages while the processor is stopped
	for (size_t i = 0; i < max_queue; ++i)
	{
		ASSERT_TRUE (processor.enqueue (dispatcher.make (node, 0, i + 1), full));
	}
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message_processor, nano::stat::detail::overfill, nano::stat::dir::in));

	// One more is dropped and counted, overall and by message type
	ASSERT_FALSE (processor.enqueue (dispatcher.make (node, 0, max_queue + 1), full));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_processor, nano::stat::detail::overfill, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_processor_overfill, nano::stat::detail::keepalive, nano::stat::dir::in));

	// The other channel is not held back by the full one
	ASSERT_TRUE (processor.enqueue (dispatcher.make (node, 1, 1), other));

	// Only what was queued gets dispatched, the dropped message is gone
	processor.start ();
	ASSERT_TIMELY_EQ (5s, dispatcher.dispatched.load (), max_queue + 1);
	processor.stop ();
	ASSERT_EQ (0, dispatcher.reordered);
}
