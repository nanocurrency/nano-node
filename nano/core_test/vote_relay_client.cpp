#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/common.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/test_channel.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <unordered_set>

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
	bool send_buffer_impl (nano::shared_const_buffer const &, nano::transport::traffic_type, callback_t) override
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
	ASSERT_EQ (1, index.outstanding ());

	auto found = index.find (1, nullptr);
	ASSERT_TRUE (found.has_value ());
	ASSERT_EQ (1, found->id);
	ASSERT_EQ (3, found->hashes);
	ASSERT_EQ (0, found->votes);
	ASSERT_FALSE (index.find (2, nullptr).has_value ());

	index.received (1, 2);
	index.received (1, 3);
	ASSERT_EQ (5, index.find (1, nullptr)->votes);

	ASSERT_TRUE (index.erase (1));
	ASSERT_FALSE (index.erase (1));
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
	ASSERT_EQ (3, index.outstanding ());

	auto evicted = index.evict (now + 2s);
	ASSERT_EQ (2, evicted.size ());
	ASSERT_EQ (1, evicted[0].id);
	ASSERT_EQ (3, evicted[1].id);
	ASSERT_EQ (1, index.outstanding ());
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

	// A request is only matched from the channel it was sent to
	ASSERT_TRUE (index.find (1, channel1).has_value ());
	ASSERT_FALSE (index.find (1, channel2).has_value ());

	ASSERT_TRUE (index.erase (1));
	ASSERT_EQ (1, index.outstanding (channel1));
}

/*
 * A vote matches a request when it comes from a requested representative and names at least one requested block
 */
TEST (vote_relay_client_index, request_filter)
{
	nano::keypair rep;
	nano::vote_relay_client_index::request_filter const filter{ { rep.pub }, { nano::block_hash{ 1 }, nano::block_hash{ 2 } } };

	ASSERT_TRUE (filter.matches (*nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ nano::block_hash{ 2 } })));
	// Other blocks alongside a requested one do not matter
	ASSERT_TRUE (filter.matches (*nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ nano::block_hash{ 3 }, nano::block_hash{ 1 } })));
	ASSERT_FALSE (filter.matches (*nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ nano::block_hash{ 3 } })));
	ASSERT_FALSE (filter.matches (*nano::test::make_final_vote (nano::keypair{}, std::vector<nano::block_hash>{ nano::block_hash{ 1 } })));
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
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes2, nano::vote_finality::any));
	ASSERT_EQ (1, node.vote_relay_client.outstanding ());
	ASSERT_EQ (1, node.vote_relay_client.outstanding (channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::request));

	ASSERT_EQ (std::future_status::ready, future.wait_for (5s));
	auto const message = future.get ();
	ASSERT_NE (0, message.id);
	ASSERT_EQ (nano::vote_finality::any, message.finality ());
	ASSERT_EQ (roots_hashes2, message.roots_hashes);
	ASSERT_EQ (std::vector<nano::account>{ nano::account{ 1 } }, message.reps);

	// The terminating empty ack completes the request
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, message.id, {} };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());
	ASSERT_EQ (0, node.vote_relay_client.outstanding (channel));
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
	ASSERT_TRUE (node.vote_relay_client.request (channel, { nano::dev::genesis_key.pub }, { { blocks[0]->hash (), blocks[0]->root () } }, nano::vote_finality::final));
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
	ASSERT_TRUE (node.vote_relay_client.request (channel, { nano::dev::genesis_key.pub }, roots_hashes, nano::vote_finality::final));
	auto const id = future.get ().id;

	nano::messages::vote_relay_ack ack2{ nano::dev::network_params.network, id, { vote } };
	ASSERT_FALSE (node.vote_relay_client.process (ack2, other));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));
	ASSERT_EQ (1, node.vote_relay_client.outstanding ()); // The request is still outstanding

	// Acks on a channel arrive in order, so once the terminator closed the request a later ack for it is unsolicited as well
	nano::messages::vote_relay_ack terminator{ nano::dev::network_params.network, id, {} };
	ASSERT_TRUE (node.vote_relay_client.process (terminator, channel));
	ASSERT_FALSE (node.vote_relay_client.process (ack2, channel));
	ASSERT_EQ (3, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());
}

/*
 * Votes a request did not ask for should be dropped, a relay cannot pass off votes it holds as reaching representatives it was not asked about
 */
TEST (vote_relay_client, mismatch)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, { nano::dev::genesis_key.pub }, roots_hashes, nano::vote_finality::final));
	auto const id = future.get ().id;

	auto const hash = roots_hashes[0].first;
	// Requested representative, unrequested block
	auto vote1 = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ nano::block_hash{ 200 } });
	// Unrequested representative, requested block
	auto vote2 = nano::test::make_final_vote (nano::keypair{}, std::vector<nano::block_hash>{ hash });
	// Requested representative and block, among other blocks
	auto vote3 = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ nano::block_hash{ 300 }, hash });

	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, id, { vote1, vote2, vote3 } };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::mismatch));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_processor_source, nano::stat::detail::relay), 1);

	// Only the requested representative is credited to the relay
	ASSERT_TIMELY_EQ (5s, node.vote_relay_client.relayed ().size (), 1);
	ASSERT_EQ (nano::dev::genesis_key.pub, node.vote_relay_client.relayed ().front ());
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
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_EQ (1, node.vote_relay_client.outstanding ());

	WAIT (200ms);

	// Expiry is applied when the next request is made
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_EQ (1, node.vote_relay_client.outstanding ());
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
	ASSERT_FALSE (node.vote_relay_client.request (failing, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::request_failed));
	ASSERT_TRUE (node.vote_relay_client.empty ());

	auto channel = nano::test::test_channel (node);
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_FALSE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::overfill));

	auto full = std::make_shared<stub_channel> (node, true, true);
	ASSERT_FALSE (node.vote_relay_client.request (full, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::channel_full));

	ASSERT_EQ (1, node.vote_relay_client.outstanding ());
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

	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_FALSE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::relay_full));
	ASSERT_EQ (2, node.vote_relay_client.outstanding (channel));

	// Another relay is unaffected
	auto other = nano::test::test_channel (node);
	ASSERT_TRUE (node.vote_relay_client.request (other, reps, roots_hashes, nano::vote_finality::final));

	// Answering a request frees a slot
	ASSERT_EQ (2, ids.size ());
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, ids[0], {} };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.vote_relay_client.outstanding (channel));
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
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
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
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

	// Relays are offered in random order, so a fanout of one still spreads the requests over both
	std::unordered_set<nano::account> picked;
	for (int i = 0; i < 64 && picked.size () < 2; ++i)
	{
		picked.insert (node.vote_relay_client.relays (1)[0]->get_node_id ());
	}
	ASSERT_EQ (2, picked.size ());
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
	ASSERT_TRUE (node.vote_relay_client.request (relay, { rep }, roots_hashes, nano::vote_finality::final));
	auto remaining = node.vote_relay_client.relays (8);
	ASSERT_EQ (1, remaining.size ());
	ASSERT_NE (relay->get_node_id (), remaining[0]->get_node_id ());
}

/*
 * A relay at its outstanding cap should be offered again once its unanswered requests time out, without another request being made
 */
TEST (vote_relay_client, relays_expired)
{
	nano::test::system system;
	nano::node_config relay_config = system.default_config ();
	relay_config.vote_relay->enable = true;
	relay_config.vote_relay->request_timeout = 10s;
	auto & relay_node = *system.add_node (relay_config);

	nano::node_config config = system.default_config ();
	config.vote_relay_client->max_outstanding = 1;
	config.vote_relay_client->request_timeout = 500ms;
	auto & node = *system.add_node (config);

	// The relay knows a rep that never answers, so a request to it stays unanswered
	nano::account rep{ 1 };
	relay_node.rep_crawler.force_add_rep (rep, nano::test::fake_channel (relay_node));

	ASSERT_TIMELY_EQ (5s, node.vote_relay_client.relays (8).size (), 1);
	auto relay = node.vote_relay_client.relays (1)[0];
	ASSERT_TRUE (node.vote_relay_client.request (relay, { rep }, roots_hashes, nano::vote_finality::final));
	ASSERT_TRUE (node.vote_relay_client.relays (8).empty ());

	// The request times out and the relay is offered again
	ASSERT_TIMELY (5s, !node.vote_relay_client.relays (8).empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::timeout));
	ASSERT_EQ (0, node.vote_relay_client.outstanding (relay));
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
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	auto const id = future.get ().id;

	node.vote_relay_client.stop ();
	ASSERT_TRUE (node.vote_relay_client.empty ());
	ASSERT_FALSE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));

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
	ASSERT_TRUE (node.vote_relay_client.request (channel, reps, roots_hashes, nano::vote_finality::final));
	auto const id = future.get ().id;

	auto vote = std::make_shared<nano::vote> ();
	ASSERT_TRUE (vote->account.is_zero ());
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, id, { vote } };
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::reply));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::drop));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));
	ASSERT_EQ (1, node.vote_relay_client.outstanding ());
}

/*
 * Relayed representatives are remembered with their latest relay and forgotten once no vote arrived since the cutoff
 */
TEST (vote_relay_client_index, relayed_reps)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto relay1 = nano::test::fake_channel (node);
	auto relay2 = nano::test::fake_channel (node);
	nano::account rep1{ 1 };
	nano::account rep2{ 2 };

	nano::vote_relay_client_index index;
	auto const now = std::chrono::steady_clock::now ();
	ASSERT_TRUE (index.relayed (now).empty ());

	index.observe (rep1, relay1, now);
	index.observe (rep2, relay1, now + 1s);
	ASSERT_EQ (2, index.relayed_size ());
	ASSERT_EQ ((std::deque<nano::account>{ rep1, rep2 }), index.relayed (now));
	// Only the representatives seen since the cutoff are listed
	ASSERT_EQ (std::deque<nano::account>{ rep2 }, index.relayed (now + 1s));

	// A later vote refreshes the representative in place, possibly through another relay
	index.observe (rep1, relay2, now + 2s);
	ASSERT_EQ (2, index.relayed_size ());
	ASSERT_EQ ((std::deque<nano::account>{ rep2, rep1 }), index.relayed (now));

	// Trimming drops everything older than the cutoff
	index.trim (now + 2s);
	ASSERT_EQ (1, index.relayed_size ());
	ASSERT_EQ (std::deque<nano::account>{ rep1 }, index.relayed (now));
}

/*
 * Votes delivered through a relay put their representative's weight in the relayed and reachable stake, without counting a peered representative twice
 */
TEST (vote_relay_client, reachable_stake)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (flags);

	auto stake = node.stake ();
	ASSERT_EQ (0, stake.peered);
	ASSERT_EQ (0, stake.relayed);
	ASSERT_EQ (0, stake.reachable);

	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false /* don't confirm */);
	auto election = nano::test::start_election (system, node, blocks[0]->hash ());
	ASSERT_NE (nullptr, election);

	// The send above spent from genesis, so read its weight after it
	auto const weight = node.ledger.weight (nano::dev::genesis_key.pub);
	ASSERT_GT (weight, 0);

	auto relay = nano::test::test_channel (node);
	auto future = relay->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (relay, { nano::dev::genesis_key.pub }, { { blocks[0]->hash (), blocks[0]->root () } }, nano::vote_finality::final));
	auto const id = future.get ().id;

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ blocks[0]->hash () });
	nano::messages::vote_relay_ack ack{ nano::dev::network_params.network, id, { vote } };
	ASSERT_TRUE (node.vote_relay_client.process (ack, relay));

	// The representative is reachable through the relay only
	ASSERT_TIMELY (5s, node.stake ().relayed == weight);
	ASSERT_EQ (std::deque<nano::account>{ nano::dev::genesis_key.pub }, node.vote_relay_client.relayed ());
	stake = node.stake ();
	ASSERT_EQ (0, stake.peered);
	ASSERT_EQ (weight, stake.reachable);
	ASSERT_TIMELY_EQ (5s, node.stake ().online, weight);

	// A direct channel to the same representative adds to the peered stake but not to the reachable one
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, nano::test::test_channel (node));
	stake = node.stake ();
	ASSERT_EQ (weight, stake.peered);
	ASSERT_EQ (weight, stake.relayed);
	ASSERT_EQ (weight, stake.reachable);

	// Without further relayed votes the representative drops out after the online window
	ASSERT_TIMELY (10s, node.vote_relay_client.relayed ().empty ());
	ASSERT_EQ (0, node.stake ().relayed);
	ASSERT_EQ (weight, node.stake ().reachable);
}

/*
 * A relay that refused a request should be kept out of the listings until its cooldown ran out, eviction then forgets it
 */
TEST (vote_relay_client_index, cooldown)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto relay = nano::test::fake_channel (node);
	auto other = nano::test::fake_channel (node);
	nano::vote_relay_client_index index;
	auto const now = std::chrono::steady_clock::now ();
	ASSERT_FALSE (index.cooling_down (relay, now));

	// Only the relay that refused is cooling down, and only until the given time
	index.cool_down (relay, now + 1s);
	ASSERT_TRUE (index.cooling_down (relay, now));
	ASSERT_FALSE (index.cooling_down (other, now));
	ASSERT_FALSE (index.cooling_down (relay, now + 1s));
	ASSERT_EQ (1, index.cooldown_size ());

	// Evicting past the cooldown drops it
	ASSERT_TRUE (index.evict (now).empty ());
	ASSERT_EQ (1, index.cooldown_size ());
	ASSERT_TRUE (index.evict (now + 1s).empty ());
	ASSERT_EQ (0, index.cooldown_size ());
}

/*
 * A refused terminator should close the request without counting it as answered, and keep that relay out of the listings for the cooldown
 */
TEST (vote_relay_client, refused)
{
	nano::test::system system;

	// A relay that refuses any request naming more than one representative
	nano::node_config relay_config = system.default_config ();
	relay_config.vote_relay->enable = true;
	relay_config.vote_relay->max_reps = 1;
	auto & relay_node = *system.add_node (relay_config);

	nano::node_config config = system.default_config ();
	config.vote_relay_client->refusal_cooldown = 500ms;
	auto & node = *system.add_node (config);
	ASSERT_TIMELY_EQ (5s, node.vote_relay_client.relays (8).size (), 1);
	auto relay = node.vote_relay_client.relays (1)[0];

	// The oversize request comes back refused, the request is gone and the relay is left alone
	ASSERT_TRUE (node.vote_relay_client.request (relay, { nano::account{ 1 }, nano::account{ 2 } }, roots_hashes, nano::vote_finality::final));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::refused), 1);
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::done));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
	ASSERT_EQ (1, relay_node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::oversize));
	ASSERT_TRUE (node.vote_relay_client.relays (8).empty ());

	// Once the cooldown ran out the relay is offered again
	ASSERT_TIMELY (5s, !node.vote_relay_client.relays (8).empty ());
}

/*
 * An ack flagged as completed should deliver its votes and close the request at once, so no separate empty terminator is needed
 */
TEST (vote_relay_client, completed_with_votes)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::vote_relay_req> ();
	ASSERT_TRUE (node.vote_relay_client.request (channel, { nano::dev::genesis_key.pub }, roots_hashes, nano::vote_finality::final));
	auto const id = future.get ().id;

	// The votes are queued for processing and the request is gone in one step
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ roots_hashes[0].first });
	auto ack = nano::messages::vote_relay_ack::completion (nano::dev::network_params.network, id, { vote });
	ASSERT_TRUE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::done));
	ASSERT_EQ (0, node.vote_relay_client.outstanding ());

	// Anything after it is unsolicited
	ASSERT_FALSE (node.vote_relay_client.process (ack, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited));
}
