#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/test_channel.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <deque>

using namespace std::chrono_literals;

namespace
{
/**
 * Channel that can report every traffic type as full or drop every message, exercises backpressure and send failures
 */
class stub_channel final : public nano::transport::channel
{
public:
	stub_channel (nano::node & node, bool full_a, bool sending_a) :
		channel{ node },
		full{ full_a },
		sending{ sending_a }
	{
	}

	bool max (nano::transport::traffic_type) override
	{
		return full;
	}
	nano::endpoint get_remote_endpoint () const override
	{
		return {};
	}
	nano::endpoint get_local_endpoint () const override
	{
		return {};
	}
	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::fake;
	}
	void close () override
	{
	}
	std::string to_string () const override
	{
		return "stub_channel";
	}

protected:
	bool send_impl (nano::messages::message const &, nano::transport::traffic_type, callback_t) override
	{
		return sending;
	}

private:
	bool const full;
	bool const sending;
};

nano::vote_relay_client::roots_hashes_t const roots_hashes{ { nano::block_hash{ 100 }, nano::root{ 100 } } };
std::deque<nano::account> const reps{ nano::account{ 1 } };
}

/*
 * A request should be matched by its id and the channel it was sent to, votes accumulate until it is erased
 */
TEST (vote_relay_client_index, insert_find)
{
	nano::vote_relay_client_index index;
	ASSERT_TRUE (index.empty ());

	auto const deadline = std::chrono::steady_clock::now () + 5s;
	ASSERT_TRUE (index.insert ({ 1, nullptr, deadline, 3 }));
	ASSERT_FALSE (index.insert ({ 1, nullptr, deadline, 3 })); // Ids are unique
	ASSERT_EQ (1, index.size ());

	auto found = index.find (1, nullptr);
	ASSERT_TRUE (found.has_value ());
	ASSERT_EQ (1, found->id);
	ASSERT_EQ (3, found->hashes);
	ASSERT_EQ (0, found->votes);
	ASSERT_FALSE (index.find (2, nullptr).has_value ());

	index.received (1, 2);
	index.received (1, 3);
	ASSERT_EQ (5, index.find (1, nullptr)->votes);
	ASSERT_EQ (1, index.outstanding ());

	// Completion keeps the entry around until its new deadline, a repeated completion changes nothing
	ASSERT_TRUE (index.complete (1, deadline + 1s));
	ASSERT_FALSE (index.complete (1, deadline + 1s));
	ASSERT_FALSE (index.complete (2, deadline + 1s));
	ASSERT_EQ (0, index.outstanding ());
	ASSERT_EQ (1, index.size ());
	ASSERT_TRUE (index.find (1, nullptr)->done);
	ASSERT_TRUE (index.evict (deadline).empty ());
	ASSERT_EQ (1, index.evict (deadline + 1s).size ());
	ASSERT_TRUE (index.empty ());

	ASSERT_TRUE (index.insert ({ 2, nullptr, deadline }));
	ASSERT_TRUE (index.erase (2));
	ASSERT_FALSE (index.erase (2));
	ASSERT_EQ (0, index.outstanding ());
	ASSERT_TRUE (index.empty ());
}

/*
 * Requests past their deadline should be evicted in deadline order, later ones stay
 */
TEST (vote_relay_client_index, evict)
{
	nano::vote_relay_client_index index;

	auto const now = std::chrono::steady_clock::now ();
	ASSERT_TRUE (index.insert ({ 1, nullptr, now + 1s }));
	ASSERT_TRUE (index.insert ({ 2, nullptr, now + 3s }));
	ASSERT_TRUE (index.insert ({ 3, nullptr, now + 2s }));

	ASSERT_TRUE (index.evict (now).empty ());
	ASSERT_EQ (3, index.size ());

	auto evicted = index.evict (now + 2s);
	ASSERT_EQ (2, evicted.size ());
	ASSERT_EQ (1, evicted[0].id);
	ASSERT_EQ (3, evicted[1].id);
	ASSERT_EQ (1, index.size ());
	ASSERT_TRUE (index.find (2, nullptr).has_value ());

	index.clear ();
	ASSERT_TRUE (index.empty ());
}

/*
 * Outstanding requests should be counted per channel
 */
TEST (vote_relay_client_index, outstanding)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel1 = nano::test::fake_channel (node);
	auto channel2 = nano::test::fake_channel (node);

	nano::vote_relay_client_index index;
	auto const deadline = std::chrono::steady_clock::now () + 5s;
	ASSERT_TRUE (index.insert ({ 1, channel1, deadline }));
	ASSERT_TRUE (index.insert ({ 2, channel1, deadline }));
	ASSERT_TRUE (index.insert ({ 3, channel2, deadline }));

	ASSERT_EQ (2, index.outstanding (channel1));
	ASSERT_EQ (1, index.outstanding (channel2));
	ASSERT_EQ (0, index.outstanding (nullptr));
	ASSERT_EQ (3, index.outstanding ());

	// A completed request no longer counts against its channel
	ASSERT_TRUE (index.complete (2, deadline));
	ASSERT_EQ (1, index.outstanding (channel1));
	ASSERT_EQ (2, index.outstanding ());

	// A request is only matched from the channel it was sent to
	ASSERT_TRUE (index.find (1, channel1).has_value ());
	ASSERT_FALSE (index.find (1, channel2).has_value ());

	ASSERT_TRUE (index.erase (1));
	ASSERT_EQ (0, index.outstanding (channel1));
	ASSERT_EQ (1, index.outstanding ());
}

/*
 * A request should be sent to the relay with the given reps and hashes and tracked until its terminating ack
 */
TEST (vote_relay_client, request)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();

	nano::vote_relay_client::roots_hashes_t const roots_hashes2{ { nano::block_hash{ 100 }, nano::root{ 100 } }, { nano::block_hash{ 200 }, nano::root{ 200 } } };
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes2, /* include_non_final */ true));
	ASSERT_EQ (1, node.vote_relay_client.size ());
	ASSERT_EQ (1, node.vote_relay_client.outstanding (channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::request));

	ASSERT_EQ (std::future_status::ready, future.wait_for (5s));
	auto const message = future.get ();
	ASSERT_NE (0, message.id);
	ASSERT_TRUE (message.include_non_final ());
	ASSERT_EQ (roots_hashes2, message.roots_hashes);
	ASSERT_EQ (std::vector<nano::account>{ nano::account{ 1 } }, message.reps);

	// The terminating empty ack completes the request
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, message.id, {} };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());
	ASSERT_EQ (0, node.vote_relay_client.outstanding (channel));
	ASSERT_EQ (1, node.vote_relay_client.size ()); // Lingers for late acks
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::reply));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::done));
}

/*
 * Votes from an ack should be processed as relay-sourced and reach the election
 */
TEST (vote_relay_client, ack_votes)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false /* don't confirm */);
	auto election = nano::test::start_election (system, node, blocks[0]->hash ());
	ASSERT_NE (nullptr, election);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, { nano::dev::genesis_key.pub }, { { blocks[0]->hash (), blocks[0]->root () } }, false));
	auto const id = future.get ().id;

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ blocks[0]->hash () });
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, id, { vote } };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));

	ASSERT_TIMELY (5s, election->confirmed ());
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::election_vote, nano::stat::detail::relay), 1);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_processor_source, nano::stat::detail::relay));
	ASSERT_TRUE (election->votes ().contains (nano::dev::genesis_key.pub));

	// The request stays open until the terminator, votes are counted on it
	ASSERT_EQ (1, node.vote_relay_client.outstanding ());
	nano::messages::vote_relay_ack terminator{ nano::dev::network_params.network, id, {} };
	ASSERT_TRUE (node.vote_relay_client.process (terminator, channel));
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::done));
}

/*
 * Acks that match no outstanding request or come from the wrong channel should be dropped without processing votes
 */
TEST (vote_relay_client, unsolicited)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	auto other = nano::test::test_channel (node);

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ nano::block_hash{ 100 } });

	// Unknown id
	nano::messages::vote_relay_ack ack1{ nano::dev::network_params.network, 42, { vote } };
	ASSERT_FALSE (node.vote_relay_client.process (ack1, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));

	// Known id but from a channel other than the one asked
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, { nano::dev::genesis_key.pub }, roots_hashes, false));
	auto const id = future.get ().id;

	nano::messages::vote_relay_ack ack2{ nano::dev::network_params.network, id, { vote } };
	ASSERT_FALSE (node.vote_relay_client.process (ack2, other));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));
	ASSERT_EQ (1, node.vote_relay_client.size ()); // The request is still outstanding
}

/*
 * Requests past the timeout should be expired when the next request is made
 */
TEST (vote_relay_client, timeout)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay_client->request_timeout = 100ms;
	auto & node = *system.add_node (config);

	auto channel = nano::test::test_channel (node);
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_EQ (1, node.vote_relay_client.size ());

	WAIT (200ms);

	// Expiry is applied when the next request is made
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_EQ (1, node.vote_relay_client.size ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::timeout));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::request));
}

/*
 * The in-flight cap and a full relay channel should refuse new requests
 */
TEST (vote_relay_client, limits)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay_client->max_requests = 1;
	auto & node = *system.add_node (config);

	// A relay that drops the message leaves no request behind
	auto failing = std::make_shared<stub_channel> (node, false, false);
	ASSERT_FALSE (node.vote_relay_client.request (failing, reps, roots_hashes, false));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::request_failed));
	ASSERT_TRUE (node.vote_relay_client.empty ());

	auto channel = nano::test::test_channel (node);
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_FALSE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::overfill));

	auto full = std::make_shared<stub_channel> (node, true, true);
	ASSERT_FALSE (node.vote_relay_client.request (full, reps, roots_hashes, false));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::channel_full));

	ASSERT_EQ (1, node.vote_relay_client.size ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::request));
}

/*
 * A relay with too many outstanding requests should be skipped until it answers
 */
TEST (vote_relay_client, outstanding)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay_client->max_outstanding = 2;
	auto & node = *system.add_node (config);

	auto channel = nano::test::test_channel (node);
	std::vector<nano::vote_relay_client::id_t> ids;
	channel->observe<nano::messages::vote_relay_req> ([&ids] (nano::messages::vote_relay_req const & message) {
		ids.push_back (message.id);
	});

	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_FALSE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::relay_full));
	ASSERT_EQ (2, node.vote_relay_client.outstanding (channel));

	// Another relay is unaffected
	auto other = nano::test::test_channel (node);
	ASSERT_TRUE (node.vote_relay_client.request (other, reps, roots_hashes, false));

	// Answering a request frees a slot
	ASSERT_EQ (2, ids.size ());
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, ids[0], {} };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.vote_relay_client.outstanding (channel));
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
}

/*
 * Inbound acks should reach the client through message processing, unsolicited ones are dropped
 */
TEST (vote_relay_client, integration_inbound)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	auto const id = future.get ().id;

	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, id, {} };
	node.inbound (ack, channel);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::done), 1);
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());

	nano::messages::vote_relay_ack unknown{ nano::dev::network_params.network, 42, {} };
	node.inbound (unknown, channel);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::vote_relay_ack, nano::stat::dir::in), 1);
}

/*
 * Peers advertising the vote relay capability should be offered as relays, capped and without the ones that have too many outstanding requests
 */
TEST (vote_relay_client, relays)
{
	nano::test::system system;
	nano::node_config relay_config = system.default_config ();
	relay_config.vote_relay->enable = true;
	relay_config.vote_relay->request_timeout = 10s;
	auto & relay1 = *system.add_node (relay_config);
	auto & relay2 = *system.add_node (relay_config);

	nano::node_config config = system.default_config ();
	config.vote_relay_client->max_outstanding = 1;
	auto & node = *system.add_node (config);

	ASSERT_TIMELY_EQ (5s, node.vote_relay_client.relays (8).size (), 2);
	ASSERT_EQ (1, node.vote_relay_client.relays (1).size ());
	for (auto const & relay : node.vote_relay_client.relays (8))
	{
		ASSERT_TRUE (relay->get_node_id () == relay1.get_node_id () || relay->get_node_id () == relay2.get_node_id ());
	}

	// The relays see each other but not the requester
	ASSERT_TIMELY_EQ (5s, relay1.vote_relay_client.relays (8).size (), 1);
	ASSERT_EQ (relay2.get_node_id (), relay1.vote_relay_client.relays (8)[0]->get_node_id ());

	// Both relays know a rep that never answers, so a request to either stays unanswered
	nano::account rep{ 1 };
	relay1.rep_crawler.force_add_rep (rep, nano::test::fake_channel (relay1));
	relay2.rep_crawler.force_add_rep (rep, nano::test::fake_channel (relay2));

	// A relay at its outstanding cap is left out until it answers
	auto relay = node.vote_relay_client.relays (1)[0];
	ASSERT_TRUE (node.vote_relay_client.request (relay, { rep }, roots_hashes, false));
	auto remaining = node.vote_relay_client.relays (8);
	ASSERT_EQ (1, remaining.size ());
	ASSERT_NE (relay->get_node_id (), remaining[0]->get_node_id ());
}

/*
 * A stopped client neither sends nor accepts anything and forgets its outstanding requests
 */
TEST (vote_relay_client, stopped)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	auto const id = future.get ().id;

	node.vote_relay_client.stop ();
	ASSERT_TRUE (node.vote_relay_client.empty ());
	ASSERT_FALSE (node.vote_relay_client.request (channel, reps, roots_hashes, false));

	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, id, {} };
	ASSERT_FALSE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::request));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
}

/*
 * Votes without an account are dropped before processing, the ack itself still counts for the request
 */
TEST (vote_relay_client, zero_account_vote)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	auto const id = future.get ().id;

	auto vote = std::make_shared<nano::vote> ();
	ASSERT_TRUE (vote->account.is_zero ());
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, id, { vote } };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::reply));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::drop));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));
	ASSERT_EQ (1, node.vote_relay_client.size ());
}

/*
 * Messages on a channel are processed in parallel, so a vote ack can arrive after the terminator: it is still accepted while the request lingers, and dropped once the grace period passed
 */
TEST (vote_relay_client, late_ack)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay_client->linger_timeout = 200ms;
	auto & node = *system.add_node (config);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, { nano::dev::genesis_key.pub }, roots_hashes, false));
	auto const id = future.get ().id;

	nano::messages::vote_relay_ack terminator{ nano::dev::network_params.network, id, {} };
	ASSERT_TRUE (node.vote_relay_client.process (terminator, channel));
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::done));

	// A repeated terminator changes nothing
	ASSERT_TRUE (node.vote_relay_client.process (terminator, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::done));

	// Votes overtaken by the terminator are still processed
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ nano::block_hash{ 100 } });
	nano::messages::vote_relay_ack late{ nano::dev::network_params.network, id, { vote } };
	ASSERT_TRUE (node.vote_relay_client.process (late, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));

	// Once the grace period passed the request is forgotten without counting as a timeout
	WAIT (300ms);
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, false));
	ASSERT_EQ (1, node.vote_relay_client.size ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::timeout));
	ASSERT_FALSE (node.vote_relay_client.process (late, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
}
