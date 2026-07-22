#include <nano/lib/blocks.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/test_channel.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/node/vote_relay_index.hpp>
#include <nano/node/wallet.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
/**
 * Collects vote relay acks sent to a test channel
 */
class ack_collector final
{
public:
	void connect (std::shared_ptr<nano::transport::test_channel> const & channel)
	{
		channel->observe<nano::messages::vote_relay_ack> ([this] (nano::messages::vote_relay_ack const & ack) {
			nano::lock_guard<nano::mutex> lock{ mutex };
			acks.push_back (ack);
		});
	}

	// All votes received across acks for the given request id
	std::vector<std::shared_ptr<nano::vote>> votes (uint64_t id) const
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		std::vector<std::shared_ptr<nano::vote>> result;
		for (auto const & ack : acks)
		{
			if (ack.id == id)
			{
				result.insert (result.end (), ack.votes.begin (), ack.votes.end ());
			}
		}
		return result;
	}

	// Whether a terminating empty ack was received for the given request id
	bool terminated (uint64_t id) const
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return std::any_of (acks.begin (), acks.end (), [id] (auto const & ack) {
			return ack.id == id && ack.votes.empty ();
		});
	}

private:
	mutable nano::mutex mutex;
	std::vector<nano::messages::vote_relay_ack> acks;
};
}

/*
 * Insert should return queries grouped per representative
 */
TEST (vote_relay_index, insert_queries)
{
	nano::vote_relay_index index;
	ASSERT_TRUE (index.empty ());
	ASSERT_FALSE (index.next_deadline ().has_value ());

	nano::account rep1{ 1 };
	nano::account rep2{ 2 };
	nano::block_hash hash1{ 100 };
	nano::block_hash hash2{ 200 };
	auto deadline = std::chrono::steady_clock::now () + 5s;

	auto queries = index.insert (nullptr, 1, { { hash1, nano::root{ 100 }, { rep1 } }, { hash2, nano::root{ 200 }, { rep1, rep2 } } }, false, deadline);
	ASSERT_EQ (2, queries.size ());
	ASSERT_EQ (rep1, queries[0].rep);
	ASSERT_EQ (2, queries[0].roots_hashes.size ());
	ASSERT_EQ (rep2, queries[1].rep);
	ASSERT_EQ (1, queries[1].roots_hashes.size ());
	ASSERT_EQ (hash2, queries[1].roots_hashes[0].first);

	ASSERT_EQ (1, index.size ());
	ASSERT_EQ (2, index.pending_size ());
	ASSERT_TRUE (index.next_deadline ().has_value ());
	ASSERT_EQ (deadline, *index.next_deadline ());
}

/*
 * (hash, rep) pairs already in flight should not be queried again, an arriving vote satisfies all waiting requests
 */
TEST (vote_relay_index, dedup)
{
	nano::vote_relay_index index;

	nano::keypair rep;
	nano::block_hash hash{ 100 };
	auto deadline = std::chrono::steady_clock::now () + 5s;

	auto queries1 = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, false, deadline);
	ASSERT_EQ (1, queries1.size ());

	// Same (hash, rep) pair is already in flight
	auto queries2 = index.insert (nullptr, 2, { { hash, nano::root{ 100 }, { rep.pub } } }, false, deadline);
	ASSERT_TRUE (queries2.empty ());
	ASSERT_EQ (2, index.size ());

	// A single vote satisfies both requests
	auto vote = nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ hash });
	auto replies = index.vote (vote);
	ASSERT_EQ (2, replies.size ());
	for (auto const & reply : replies)
	{
		ASSERT_EQ (1, reply.votes.size ());
		ASSERT_EQ (vote, reply.votes[0]);
	}
	ASSERT_TRUE (index.empty ());
	ASSERT_EQ (0, index.pending_size ());
}

/*
 * Request should complete only once all wanted reps have answered, accumulating their votes
 */
TEST (vote_relay_index, partial_completion)
{
	nano::vote_relay_index index;

	nano::keypair rep1;
	nano::keypair rep2;
	nano::block_hash hash{ 100 };
	auto deadline = std::chrono::steady_clock::now () + 5s;

	auto queries = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep1.pub, rep2.pub } } }, false, deadline);
	ASSERT_EQ (2, queries.size ());

	auto vote1 = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (index.vote (vote1).empty ());
	ASSERT_EQ (1, index.size ());

	auto vote2 = nano::test::make_final_vote (rep2, std::vector<nano::block_hash>{ hash });
	auto replies = index.vote (vote2);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (2, replies[0].votes.size ());
	ASSERT_TRUE (index.empty ());
}

/*
 * A vote covering multiple wanted hashes should complete the request with the vote included once
 */
TEST (vote_relay_index, multi_hash_vote)
{
	nano::vote_relay_index index;

	nano::keypair rep;
	nano::block_hash hash1{ 100 };
	nano::block_hash hash2{ 200 };
	auto deadline = std::chrono::steady_clock::now () + 5s;

	auto queries = index.insert (nullptr, 1, { { hash1, nano::root{ 100 }, { rep.pub } }, { hash2, nano::root{ 200 }, { rep.pub } } }, false, deadline);
	ASSERT_EQ (1, queries.size ());
	ASSERT_EQ (2, queries[0].roots_hashes.size ());

	auto vote = nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ hash1, hash2 });
	auto replies = index.vote (vote);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (1, replies[0].votes.size ());
	ASSERT_TRUE (index.empty ());
}

/*
 * Non-final votes should only satisfy requests that include non-final votes
 */
TEST (vote_relay_index, finality)
{
	nano::vote_relay_index index;

	nano::keypair rep;
	nano::block_hash hash{ 100 };
	auto deadline = std::chrono::steady_clock::now () + 5s;

	index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, /* include_non_final */ false, deadline);

	// Non-final vote does not satisfy a final-only request
	auto vote_normal = nano::test::make_vote (rep, std::vector<nano::block_hash>{ hash }, nano::vote::timestamp_min, 0);
	ASSERT_FALSE (vote_normal->is_final ());
	ASSERT_TRUE (index.vote (vote_normal).empty ());
	ASSERT_EQ (1, index.size ());

	// Final vote does
	auto vote_final = nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ hash });
	ASSERT_EQ (1, index.vote (vote_final).size ());
	ASSERT_TRUE (index.empty ());

	// Non-final vote satisfies a request that includes non-final votes
	index.insert (nullptr, 2, { { hash, nano::root{ 100 }, { rep.pub } } }, /* include_non_final */ true, deadline);
	auto replies = index.vote (vote_normal);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (vote_normal, replies[0].votes[0]);
}

/*
 * Requests past their deadline should be evicted with whatever votes were accumulated
 */
TEST (vote_relay_index, evict)
{
	nano::vote_relay_index index;

	nano::keypair rep1;
	nano::keypair rep2;
	nano::block_hash hash1{ 100 };
	nano::block_hash hash2{ 200 };
	auto now = std::chrono::steady_clock::now ();

	index.insert (nullptr, 1, { { hash1, nano::root{ 100 }, { rep1.pub } }, { hash2, nano::root{ 200 }, { rep2.pub } } }, false, now + 1s);

	// Partial answer before the deadline
	auto vote = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash1 });
	ASSERT_TRUE (index.vote (vote).empty ());

	// Not expired yet
	ASSERT_TRUE (index.evict (now).empty ());
	ASSERT_EQ (1, index.size ());

	auto evicted = index.evict (now + 2s);
	ASSERT_EQ (1, evicted.size ());
	ASSERT_EQ (1, evicted[0].id);
	ASSERT_EQ (1, evicted[0].votes.size ());
	ASSERT_EQ (vote, evicted[0].votes[0]);

	ASSERT_TRUE (index.empty ());
	ASSERT_EQ (0, index.pending_size ());
	ASSERT_FALSE (index.next_deadline ().has_value ());
}

/*
 * Reinserting the same (channel, id) should replace the previous request
 */
TEST (vote_relay_index, replace)
{
	nano::vote_relay_index index;

	nano::keypair rep;
	nano::block_hash hash{ 100 };
	auto deadline = std::chrono::steady_clock::now () + 5s;

	auto queries1 = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, false, deadline);
	ASSERT_EQ (1, queries1.size ());

	// Replacing removes the previous request together with its in-flight queries
	auto queries2 = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, false, deadline);
	ASSERT_EQ (1, queries2.size ());
	ASSERT_EQ (1, index.size ());
	ASSERT_EQ (1, index.pending_size ());
}

/*
 * Requests should be dropped when the relay is disabled
 */
TEST (vote_relay, disabled)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::fake_channel (node);
	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 1, { { nano::block_hash{ 1 }, nano::root{ 1 } } }, { nano::account{ 1 } } };
	ASSERT_FALSE (node.vote_relay.request (req, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::drop));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::request));
}

/*
 * Requests without a rep filter are reserved for the vote storage role and should be rejected
 */
TEST (vote_relay, unsupported_any_rep)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	auto & node = *system.add_node (config);

	auto channel = nano::test::fake_channel (node);
	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 1, { { nano::block_hash{ 1 }, nano::root{ 1 } } } };
	ASSERT_FALSE (node.vote_relay.request (req, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::unsupported));
}

/*
 * Votes already in the vote cache should be served immediately, followed by the terminating empty ack
 */
TEST (vote_relay, serve_from_cache)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	auto & node = *system.add_node (config);

	nano::block_hash hash{ 100 };
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ hash });
	node.vote_cache.insert (vote);
	ASSERT_TRUE (node.vote_cache.contains (hash));

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 42, { { hash, nano::root{ 100 } } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));

	ASSERT_TIMELY (5s, acks.terminated (42));
	auto votes = acks.votes (42);
	ASSERT_EQ (1, votes.size ());
	ASSERT_EQ (nano::dev::genesis_key.pub, votes[0]->account);
	ASSERT_TRUE (votes[0]->is_final ());

	// No rep queries were necessary
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::cache));
}

/*
 * Non-final cached votes should only be served when the request includes non-final votes
 */
TEST (vote_relay, non_final_filtering)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	auto & node = *system.add_node (config);

	nano::block_hash hash{ 100 };
	auto vote = nano::test::make_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ hash }, nano::vote::timestamp_min, 0);
	ASSERT_FALSE (vote->is_final ());
	node.vote_cache.insert (vote);

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	// Request including non-final votes is served from the cache
	nano::messages::vote_relay_req req1{ nano::dev::network_params.network, 1, { { hash, nano::root{ 100 } } }, { nano::dev::genesis_key.pub }, /* include_non_final */ true };
	ASSERT_TRUE (node.vote_relay.request (req1, channel));

	ASSERT_TIMELY (5s, acks.terminated (1));
	ASSERT_EQ (1, acks.votes (1).size ());

	// Final-only request cannot be served and the rep is not reachable, terminates empty
	nano::messages::vote_relay_req req2{ nano::dev::network_params.network, 2, { { hash, nano::root{ 100 } } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (node.vote_relay.request (req2, channel));

	ASSERT_TIMELY (5s, acks.terminated (2));
	ASSERT_TRUE (acks.votes (2).empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::rep_unknown));
}

/*
 * Votes missing from the cache should be queried from the representative and relayed once they arrive
 */
TEST (vote_relay, query_rep)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.vote_relay->enable = true;
	config.backlog_scan->enable = false;
	config.hinted_scheduler->enable = false;
	config.optimistic_scheduler->enable = false;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	auto blocks = nano::test::setup_chain (system, node, 1);

	// Local representative is reachable via the loopback channel
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, node.create_loopback_channel ());

	ASSERT_FALSE (node.vote_cache.contains (blocks[0]->hash ()));

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 7, { { blocks[0]->hash (), blocks[0]->root () } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));

	ASSERT_TIMELY (5s, acks.terminated (7));
	auto votes = acks.votes (7);
	ASSERT_EQ (1, votes.size ());
	ASSERT_EQ (nano::dev::genesis_key.pub, votes[0]->account);
	ASSERT_TRUE (votes[0]->is_final ());
	ASSERT_TRUE (std::find (votes[0]->hashes.begin (), votes[0]->hashes.end (), blocks[0]->hash ()) != votes[0]->hashes.end ());

	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::done));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::timeout));
}

/*
 * Requests that cannot be answered should terminate with an empty ack after the timeout
 */
TEST (vote_relay, timeout)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.vote_relay->enable = true;
	config.vote_relay->request_timeout = 500ms;
	config.backlog_scan->enable = false;
	config.hinted_scheduler->enable = false;
	config.optimistic_scheduler->enable = false;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Unconfirmed block, the voting policy will not produce a vote for it
	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false /* don't confirm */);

	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, node.create_loopback_channel ());

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 7, { { blocks[0]->hash (), blocks[0]->root () } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));

	ASSERT_TIMELY (5s, acks.terminated (7));
	ASSERT_TRUE (acks.votes (7).empty ());

	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::timeout));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::done));
}

/*
 * Concurrent requests for the same (hash, rep) pair should result in a single upstream query
 */
TEST (vote_relay, query_dedup)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.vote_relay->enable = true;
	config.vote_relay->request_timeout = 500ms;
	config.backlog_scan->enable = false;
	config.hinted_scheduler->enable = false;
	config.optimistic_scheduler->enable = false;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Unconfirmed block, so no vote ever arrives and the first request keeps its query in flight
	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false /* don't confirm */);

	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, node.create_loopback_channel ());

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req1{ nano::dev::network_params.network, 1, { { blocks[0]->hash (), blocks[0]->root () } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (node.vote_relay.request (req1, channel));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query), 1);

	// Second request for the same (hash, rep) pair rides on the first query
	nano::messages::vote_relay_req req2{ nano::dev::network_params.network, 2, { { blocks[0]->hash (), blocks[0]->root () } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (node.vote_relay.request (req2, channel));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::duplicate), 1);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query));

	// Both requests eventually time out
	ASSERT_TIMELY (5s, acks.terminated (1));
	ASSERT_TIMELY (5s, acks.terminated (2));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::timeout));
}

/*
 * End-to-end: inbound vote_relay_req is dispatched to the relay and served
 */
TEST (vote_relay, integration_inbound)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	auto & node = *system.add_node (config);

	nano::block_hash hash{ 100 };
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ hash });
	node.vote_cache.insert (vote);

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 13, { { hash, nano::root{ 100 } } }, { nano::dev::genesis_key.pub } };
	node.inbound (req, channel);

	ASSERT_TIMELY (5s, acks.terminated (13));
	ASSERT_EQ (1, acks.votes (13).size ());
}

/*
 * The vote relay capability should be advertised to peers via handshake
 */
TEST (vote_relay, capability)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	auto & relay_node = *system.add_node (config);
	auto & other_node = *system.add_node ();

	ASSERT_TIMELY (5s, other_node.network.find_node_id (relay_node.get_node_id ()) != nullptr);
	auto channel = other_node.network.find_node_id (relay_node.get_node_id ());
	ASSERT_TRUE (channel->get_flags ().test (nano::node_capabilities::vote_relay));

	ASSERT_TIMELY (5s, relay_node.network.find_node_id (other_node.get_node_id ()) != nullptr);
	auto channel2 = relay_node.network.find_node_id (other_node.get_node_id ());
	ASSERT_FALSE (channel2->get_flags ().test (nano::node_capabilities::vote_relay));
}

/*
 * End-to-end with two nodes: the relay queries the representative over the network and relays its votes
 */
TEST (vote_relay, two_nodes)
{
	nano::test::system system;

	// Voting representative
	nano::node_config config0 = system.default_config ();
	config0.enable_voting = true;
	auto & rep_node = *system.add_node (config0);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	rep_node.wallets.refresh_reps ();

	// Relay
	nano::node_config config1 = system.default_config ();
	config1.vote_relay->enable = true;
	auto & relay_node = *system.add_node (config1);

	// Confirmed block known to the representative
	auto blocks = nano::test::setup_chain (system, rep_node, 1);

	// Relay discovers the representative over the network
	ASSERT_TIMELY_EQ (10s, relay_node.rep_crawler.representative_count (), 1);

	auto channel = nano::test::test_channel (relay_node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 99, { { blocks[0]->hash (), blocks[0]->root () } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (relay_node.vote_relay.request (req, channel));

	ASSERT_TIMELY (10s, acks.terminated (99));
	auto votes = acks.votes (99);
	ASSERT_EQ (1, votes.size ());
	ASSERT_EQ (nano::dev::genesis_key.pub, votes[0]->account);
	ASSERT_TRUE (votes[0]->is_final ());
	ASSERT_TRUE (std::find (votes[0]->hashes.begin (), votes[0]->hashes.end (), blocks[0]->hash ()) != votes[0]->hashes.end ());
}
