#include <nano/lib/blocks.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
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

#include <sstream>

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

/**
 * Channel that reports every traffic type as full, nothing can be sent through it
 */
class full_channel final : public nano::transport::channel
{
public:
	explicit full_channel (nano::node & node) :
		channel{ node }
	{
	}

	bool max (nano::transport::traffic_type) override
	{
		return true;
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
		return "full_channel";
	}

protected:
	bool send_buffer_impl (nano::shared_const_buffer const &, nano::transport::traffic_type, callback_t) override
	{
		return false;
	}
};

// Limits that let only completion answer within a test: a lifetime, idle timeout and flush interval of 5s with a flush threshold of 8
nano::vote_relay_index::request_limits const test_limits{ 5s, 5s, 5s, 8 };
}

/*
 * Insert should return queries grouped per representative
 */
TEST (vote_relay_index, insert_queries)
{
	nano::vote_relay_index index{ test_limits };
	ASSERT_TRUE (index.empty ());
	ASSERT_FALSE (index.next_due ().has_value ());

	nano::account rep1{ 1 };
	nano::account rep2{ 2 };
	nano::block_hash hash1{ 100 };
	nano::block_hash hash2{ 200 };
	auto now = std::chrono::steady_clock::now ();

	auto queries = index.insert (nullptr, 1, { { hash1, nano::root{ 100 }, { rep1 } }, { hash2, nano::root{ 200 }, { rep1, rep2 } } }, nano::vote_finality::final, now);
	ASSERT_EQ (2, queries.size ());
	ASSERT_EQ (rep1, queries[0].rep);
	ASSERT_EQ (2, queries[0].roots_hashes.size ());
	ASSERT_EQ (rep2, queries[1].rep);
	ASSERT_EQ (1, queries[1].roots_hashes.size ());
	ASSERT_EQ (hash2, queries[1].roots_hashes[0].first);

	ASSERT_EQ (1, index.size ());
	ASSERT_EQ (2, index.pending_size ());
	ASSERT_TRUE (index.next_due ().has_value ());
	ASSERT_EQ (now + 5s, *index.next_due ());
}

/*
 * (hash, rep) pairs already in flight should not be queried again, an arriving vote satisfies all waiting requests
 */
TEST (vote_relay_index, dedup)
{
	nano::vote_relay_index index{ test_limits };

	nano::keypair rep;
	nano::block_hash hash{ 100 };
	auto now = std::chrono::steady_clock::now ();

	auto queries1 = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, nano::vote_finality::final, now);
	ASSERT_EQ (1, queries1.size ());

	// Same (hash, rep) pair is already in flight
	auto queries2 = index.insert (nullptr, 2, { { hash, nano::root{ 100 }, { rep.pub } } }, nano::vote_finality::final, now);
	ASSERT_TRUE (queries2.empty ());
	ASSERT_EQ (2, index.size ());

	// A single vote satisfies both requests
	auto vote = nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ hash });
	auto replies = index.vote (vote, now);
	ASSERT_EQ (2, replies.size ());
	for (auto const & reply : replies)
	{
		ASSERT_EQ (nano::vote_relay_index::outcome::completed, reply.result);
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
	nano::vote_relay_index index{ test_limits };

	nano::keypair rep1;
	nano::keypair rep2;
	nano::block_hash hash{ 100 };
	auto now = std::chrono::steady_clock::now ();

	auto queries = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep1.pub, rep2.pub } } }, nano::vote_finality::final, now);
	ASSERT_EQ (2, queries.size ());

	auto vote1 = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (index.vote (vote1, now).empty ());
	ASSERT_EQ (1, index.size ());

	auto vote2 = nano::test::make_final_vote (rep2, std::vector<nano::block_hash>{ hash });
	auto replies = index.vote (vote2, now);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (2, replies[0].votes.size ());
	ASSERT_TRUE (index.empty ());
}

/*
 * A vote covering multiple wanted hashes should complete the request with the vote included once
 */
TEST (vote_relay_index, multi_hash_vote)
{
	nano::vote_relay_index index{ test_limits };

	nano::keypair rep;
	nano::block_hash hash1{ 100 };
	nano::block_hash hash2{ 200 };
	auto now = std::chrono::steady_clock::now ();

	auto queries = index.insert (nullptr, 1, { { hash1, nano::root{ 100 }, { rep.pub } }, { hash2, nano::root{ 200 }, { rep.pub } } }, nano::vote_finality::final, now);
	ASSERT_EQ (1, queries.size ());
	ASSERT_EQ (2, queries[0].roots_hashes.size ());

	auto vote = nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ hash1, hash2 });
	auto replies = index.vote (vote, now);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (1, replies[0].votes.size ());
	ASSERT_TRUE (index.empty ());
}

/*
 * Non-final votes should only satisfy requests that include non-final votes
 */
TEST (vote_relay_index, finality)
{
	nano::vote_relay_index index{ test_limits };

	nano::keypair rep;
	nano::block_hash hash{ 100 };
	auto now = std::chrono::steady_clock::now ();

	index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, nano::vote_finality::final, now);

	// Non-final vote does not satisfy a final-only request
	auto vote_normal = nano::test::make_vote (rep, std::vector<nano::block_hash>{ hash }, nano::vote::timestamp_min, 0);
	ASSERT_FALSE (vote_normal->is_final ());
	ASSERT_TRUE (index.vote (vote_normal, now).empty ());
	ASSERT_EQ (1, index.size ());

	// Final vote does
	auto vote_final = nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ hash });
	ASSERT_EQ (1, index.vote (vote_final, now).size ());
	ASSERT_TRUE (index.empty ());

	// Non-final vote satisfies a request that includes non-final votes
	index.insert (nullptr, 2, { { hash, nano::root{ 100 }, { rep.pub } } }, nano::vote_finality::any, now);
	auto replies = index.vote (vote_normal, now);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (vote_normal, replies[0].votes[0]);
}

/*
 * Requests past their lifetime should be finished with whatever votes are still unsent
 */
TEST (vote_relay_index, lifetime)
{
	// A lifetime of 1s under an idle timeout and flush interval of 5s, so only the lifetime can end the request
	nano::vote_relay_index index{ { 1s, 5s, 5s, 8 } };

	nano::keypair rep1;
	nano::keypair rep2;
	nano::block_hash hash1{ 100 };
	nano::block_hash hash2{ 200 };
	auto now = std::chrono::steady_clock::now ();

	index.insert (nullptr, 1, { { hash1, nano::root{ 100 }, { rep1.pub } }, { hash2, nano::root{ 200 }, { rep2.pub } } }, nano::vote_finality::final, now);

	// Partial answer before the lifetime ends
	auto vote = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash1 });
	ASSERT_TRUE (index.vote (vote, now).empty ());

	// Not expired yet
	ASSERT_TRUE (index.due (now).empty ());
	ASSERT_EQ (1, index.size ());

	// The lifetime ends the request, the unsent vote goes with it
	auto expired = index.due (now + 2s);
	ASSERT_EQ (1, expired.size ());
	ASSERT_EQ (1, expired[0].id);
	ASSERT_EQ (nano::vote_relay_index::outcome::expired, expired[0].result);
	ASSERT_EQ (1, expired[0].votes.size ());
	ASSERT_EQ (vote, expired[0].votes[0]);

	ASSERT_TRUE (index.empty ());
	ASSERT_EQ (0, index.pending_size ());
	ASSERT_FALSE (index.next_due ().has_value ());
}

/*
 * Reinserting the same (channel, id) should replace the previous request
 */
TEST (vote_relay_index, replace)
{
	nano::vote_relay_index index{ test_limits };

	nano::keypair rep;
	nano::block_hash hash{ 100 };
	auto now = std::chrono::steady_clock::now ();

	auto queries1 = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, nano::vote_finality::final, now);
	ASSERT_EQ (1, queries1.size ());

	// Replacing removes the previous request together with its in-flight queries
	auto queries2 = index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep.pub } } }, nano::vote_finality::final, now);
	ASSERT_EQ (1, queries2.size ());
	ASSERT_EQ (1, index.size ());
	ASSERT_EQ (1, index.pending_size ());
}

/*
 * Requests should be dropped without an answer when the relay is disabled
 */
TEST (vote_relay, disabled)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 1, { { nano::block_hash{ 1 }, nano::root{ 1 } } }, { nano::account{ 1 } } };
	ASSERT_FALSE (node.vote_relay.request (req, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::drop));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::request));
	ASSERT_FALSE (acks.terminated (1));
}

/*
 * Requests without a rep filter are reserved for the vote storage role and should be refused with the terminating ack
 */
TEST (vote_relay, unsupported_any_rep)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	auto & node = *system.add_node (config);

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 1, { { nano::block_hash{ 1 }, nano::root{ 1 } } } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));

	// The relay thread refuses it and tells the requester that nothing will come
	ASSERT_TIMELY (5s, acks.terminated (1));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::unsupported));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::reply_empty));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::process));
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
	node.vote_cache.insert (vote, 0 /* any weight */);
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

	// The request ended without waiting for votes, which is counted apart from the requests that waited
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::immediate));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::done));
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
	node.vote_cache.insert (vote, 0 /* any weight */);

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	// Request including non-final votes is served from the cache
	nano::messages::vote_relay_req req1{ nano::dev::network_params.network, 1, { { hash, nano::root{ 100 } } }, { nano::dev::genesis_key.pub }, nano::vote_finality::any };
	ASSERT_TRUE (node.vote_relay.request (req1, channel));

	ASSERT_TIMELY (5s, acks.terminated (1));
	ASSERT_EQ (1, acks.votes (1).size ());

	// Final-only request cannot be served and the rep is not reachable, terminates empty
	nano::messages::vote_relay_req req2{ nano::dev::network_params.network, 2, { { hash, nano::root{ 100 } } }, { nano::dev::genesis_key.pub } };
	ASSERT_TRUE (node.vote_relay.request (req2, channel));

	ASSERT_TIMELY (5s, acks.terminated (2));
	ASSERT_TRUE (acks.votes (2).empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::rep_unknown));

	// Neither request had anything to wait for, the cache answered the first and nobody could be asked for the second
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::immediate));
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
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::immediate));
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
	node.vote_cache.insert (vote, 0 /* any weight */);

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
	ASSERT_TRUE (channel->has_capability (nano::node_capabilities::vote_relay));

	ASSERT_TIMELY (5s, relay_node.network.find_node_id (other_node.get_node_id ()) != nullptr);
	auto channel2 = relay_node.network.find_node_id (other_node.get_node_id ());
	ASSERT_FALSE (channel2->has_capability (nano::node_capabilities::vote_relay));
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

/*
 * Requests asking for more representatives than the relay accepts should be refused with the terminating ack
 */
TEST (vote_relay, max_reps)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->max_reps = 2;
	auto & node = *system.add_node (config);

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 1, { { nano::block_hash{ 1 }, nano::root{ 1 } } }, { nano::account{ 1 }, nano::account{ 2 }, nano::account{ 3 } } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));

	// The relay thread refuses it before any processing
	ASSERT_TIMELY (5s, acks.terminated (1));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::oversize));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::process));
}

/*
 * Requests over the queue limit of their channel should be refused while the queued one stays queued
 * The relay thread is stopped so the queue cannot drain, which also holds back the refusal's terminator
 */
TEST (vote_relay, queue_full)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->channel_limit = 1;
	auto & node = *system.add_node (config);

	// With the relay thread stopped, queued requests stay queued
	node.vote_relay.stop ();

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req1{ nano::dev::network_params.network, 1, { { nano::block_hash{ 1 }, nano::root{ 1 } } }, { nano::account{ 1 } } };
	ASSERT_TRUE (node.vote_relay.request (req1, channel));
	nano::messages::vote_relay_req req2{ nano::dev::network_params.network, 2, { { nano::block_hash{ 2 }, nano::root{ 2 } } }, { nano::account{ 1 } } };
	ASSERT_FALSE (node.vote_relay.request (req2, channel));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::overfill));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::request));
	ASSERT_FALSE (acks.terminated (1));
	ASSERT_FALSE (acks.terminated (2));
}

/*
 * A zero batch size would leave the relay thread spinning on empty batches and zero max_reps would refuse every request, both are rejected
 */
TEST (vote_relay_config, validation)
{
	auto deserialize = [] (std::string const & text) {
		std::stringstream ss;
		ss << text;
		nano::tomlconfig toml;
		toml.read (ss);
		nano::vote_relay_config config;
		return config.deserialize (toml);
	};

	ASSERT_FALSE (deserialize ("batch_size = 64\nmax_reps = 64\nflush_threshold = 8\n"));
	ASSERT_TRUE (deserialize ("batch_size = 0\n"));
	ASSERT_TRUE (deserialize ("max_reps = 0\n"));
	ASSERT_TRUE (deserialize ("flush_threshold = 0\n"));
}

/*
 * One peer should not take up every slot: requests waiting for votes are capped per channel, other peers are still served
 */
TEST (vote_relay, channel_limit)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->channel_limit = 1;
	config.vote_relay->request_timeout = 10s;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);

	// A rep that never answers keeps every request waiting
	nano::account rep{ 1 };
	node.rep_crawler.force_add_rep (rep, nano::test::fake_channel (node));

	auto channel1 = nano::test::test_channel (node);
	ack_collector acks1;
	acks1.connect (channel1);
	auto channel2 = nano::test::test_channel (node);
	ack_collector acks2;
	acks2.connect (channel2);

	nano::messages::vote_relay_req req1{ nano::dev::network_params.network, 1, { { nano::block_hash{ 1 }, nano::root{ 1 } } }, { rep } };
	ASSERT_TRUE (node.vote_relay.request (req1, channel1));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query), 1);

	// The second request from the same peer is refused and terminated right away, the first one keeps waiting
	nano::messages::vote_relay_req req2{ nano::dev::network_params.network, 2, { { nano::block_hash{ 2 }, nano::root{ 2 } } }, { rep } };
	ASSERT_TRUE (node.vote_relay.request (req2, channel1));
	ASSERT_TIMELY (5s, acks1.terminated (2));
	ASSERT_TRUE (acks1.votes (2).empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::overfill));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query));
	ASSERT_FALSE (acks1.terminated (1));

	// Another peer is still served
	nano::messages::vote_relay_req req3{ nano::dev::network_params.network, 3, { { nano::block_hash{ 3 }, nano::root{ 3 } } }, { rep } };
	ASSERT_TRUE (node.vote_relay.request (req3, channel2));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query), 2);
	ASSERT_FALSE (acks2.terminated (3));
}

/*
 * A representative behind a full channel cannot be queried, it is dropped from the request like an unknown one instead of idling until the timeout
 */
TEST (vote_relay, rep_channel_full)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->request_timeout = 10s;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);

	nano::account rep{ 1 };
	node.rep_crawler.force_add_rep (rep, std::make_shared<full_channel> (node));

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 7, { { nano::block_hash{ 1 }, nano::root{ 1 } } }, { rep } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));

	// Nothing to wait for, the request ends right away
	ASSERT_TIMELY (5s, acks.terminated (7));
	ASSERT_TRUE (acks.votes (7).empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::channel_full, nano::stat::dir::out));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::timeout));
}

/*
 * Refusals are capped like requests, beyond the cap the terminator is dropped and the requester is left to its timeout
 */
TEST (vote_relay, refusal_cap)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->channel_limit = 1;
	config.vote_relay->max_requests = 1;
	auto & node = *system.add_node (config);

	// With the relay thread stopped the first request keeps the channel's only queue slot and the refusals stay queued, so the cap bites from the second refusal on
	node.vote_relay.stop ();

	auto channel = nano::test::test_channel (node);
	for (uint64_t id = 1; id <= 4; ++id)
	{
		nano::messages::vote_relay_req req{ nano::dev::network_params.network, id, { { nano::block_hash{ id }, nano::root{ id } } }, { nano::account{ 1 } } };
		ASSERT_EQ (id == 1, node.vote_relay.request (req, channel));
	}
	ASSERT_EQ (3, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::overfill));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::drop));
}

/*
 * Unsent votes should be handed out as a flush once they reach the threshold, the request staying open until its last representative answers
 */
TEST (vote_relay_index, flush_threshold)
{
	// A threshold of 2 under long timeouts, so the threshold is the only thing that hands votes out early
	nano::vote_relay_index index{ { 20s, 20s, 10s, 2 } };

	nano::keypair rep1;
	nano::keypair rep2;
	nano::keypair rep3;
	nano::block_hash hash{ 100 };
	auto now = std::chrono::steady_clock::now ();

	index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep1.pub, rep2.pub, rep3.pub } } }, nano::vote_finality::final, now);

	// The first vote waits for company and starts the flush interval
	auto vote1 = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (index.vote (vote1, now).empty ());
	ASSERT_EQ (now + 10s, *index.next_due ());

	// The second vote reaches the threshold, both go out and the request stays open
	auto vote2 = nano::test::make_final_vote (rep2, std::vector<nano::block_hash>{ hash });
	auto replies = index.vote (vote2, now + 1s);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (nano::vote_relay_index::outcome::flush, replies[0].result);
	ASSERT_EQ (2, replies[0].votes.size ());
	ASSERT_EQ (1, index.size ());

	// Nothing waits to go out any more, the next moment of interest is the lifetime
	ASSERT_EQ (now + 20s, *index.next_due ());
	ASSERT_TRUE (index.due (now + 15s).empty ());

	// The last representative completes the request with its vote alone
	auto vote3 = nano::test::make_final_vote (rep3, std::vector<nano::block_hash>{ hash });
	replies = index.vote (vote3, now + 2s);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (nano::vote_relay_index::outcome::completed, replies[0].result);
	ASSERT_EQ (1, replies[0].votes.size ());
	ASSERT_EQ (vote3, replies[0].votes[0]);
	ASSERT_TRUE (index.empty ());
}

/*
 * A lone vote should go out once the flush interval passed, and the index should name that moment as the next one due
 */
TEST (vote_relay_index, flush_interval)
{
	// A flush interval of 3s under long timeouts and a high threshold, so only the interval hands the vote out
	nano::vote_relay_index index{ { 20s, 20s, 3s, 8 } };

	nano::keypair rep1;
	nano::keypair rep2;
	nano::block_hash hash{ 100 };
	auto now = std::chrono::steady_clock::now ();

	index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep1.pub, rep2.pub } } }, nano::vote_finality::final, now);
	ASSERT_EQ (now + 20s, *index.next_due ());

	// The vote starts the flush interval, nothing goes out before it passed
	auto vote = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (index.vote (vote, now + 1s).empty ());
	ASSERT_EQ (now + 4s, *index.next_due ());
	ASSERT_TRUE (index.due (now + 3s).empty ());

	// The interval hands the vote out as a flush, the request stays open and waits for its lifetime again
	auto replies = index.due (now + 4s);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (nano::vote_relay_index::outcome::flush, replies[0].result);
	ASSERT_EQ (1, replies[0].votes.size ());
	ASSERT_EQ (vote, replies[0].votes[0]);
	ASSERT_EQ (1, index.size ());
	ASSERT_EQ (now + 20s, *index.next_due ());

	// With nothing unsent there is nothing due until another vote arrives
	ASSERT_TRUE (index.due (now + 5s).empty ());
}

/*
 * A request without a new vote for the idle timeout should be finished with whatever is still unsent, before its lifetime ends
 */
TEST (vote_relay_index, idle)
{
	// An idle timeout of 5s under a lifetime of 20s and a flush interval of 10s, so idleness ends the request with the vote still unsent
	nano::vote_relay_index index{ { 20s, 5s, 10s, 8 } };

	nano::keypair rep1;
	nano::keypair rep2;
	nano::block_hash hash{ 100 };
	auto now = std::chrono::steady_clock::now ();

	// An untouched request goes idle counting from its creation
	index.insert (nullptr, 1, { { hash, nano::root{ 100 }, { rep1.pub, rep2.pub } } }, nano::vote_finality::final, now);
	ASSERT_EQ (now + 5s, *index.next_due ());

	// A vote pushes the idle timeout back
	auto vote = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (index.vote (vote, now + 4s).empty ());
	ASSERT_EQ (now + 9s, *index.next_due ());
	ASSERT_TRUE (index.due (now + 8s).empty ());

	// Idleness ends the request, the vote that never reached a flush goes with the terminator
	auto replies = index.due (now + 9s);
	ASSERT_EQ (1, replies.size ());
	ASSERT_EQ (nano::vote_relay_index::outcome::idle, replies[0].result);
	ASSERT_EQ (1, replies[0].votes.size ());
	ASSERT_EQ (vote, replies[0].votes[0]);
	ASSERT_TRUE (index.empty ());
	ASSERT_EQ (0, index.pending_size ());
}

/*
 * Votes should be forwarded as they arrive: once the unsent votes reach the flush threshold they go out in an ack while the request stays open, and the terminator follows the last representative's answer
 */
TEST (vote_relay, flush_threshold)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->flush_threshold = 2;
	config.vote_relay->flush_interval = 10s;
	config.vote_relay->request_timeout = 10s;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);

	// Three representatives the relay can query, which only answer when the test feeds their votes in
	nano::keypair rep1;
	nano::keypair rep2;
	nano::keypair rep3;
	for (auto const & rep : { rep1.pub, rep2.pub, rep3.pub })
	{
		node.rep_crawler.force_add_rep (rep, nano::test::fake_channel (node));
	}

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::block_hash hash{ 100 };
	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 7, { { hash, nano::root{ 100 } } }, { rep1.pub, rep2.pub, rep3.pub } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query), 3);

	// Two answers reach the threshold and go out at once, the request keeps waiting for the third
	auto vote1 = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	auto vote2 = nano::test::make_final_vote (rep2, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (node.vote_processor.vote (vote1, channel, nano::vote_source::live));
	ASSERT_TRUE (node.vote_processor.vote (vote2, channel, nano::vote_source::live));
	ASSERT_TIMELY_EQ (5s, acks.votes (7).size (), 2);
	ASSERT_FALSE (acks.terminated (7));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::flush));

	// The last answer completes the request, its vote travels with the terminator
	auto vote3 = nano::test::make_final_vote (rep3, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (node.vote_processor.vote (vote3, channel, nano::vote_source::live));
	ASSERT_TIMELY (5s, acks.terminated (7));
	ASSERT_EQ (3, acks.votes (7).size ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::done));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::timeout));
}

/*
 * A vote that arrives while others are still missing should be forwarded once the flush interval passed, the request staying open for the rest
 */
TEST (vote_relay, flush_interval)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->flush_interval = 300ms;
	config.vote_relay->idle_timeout = 10s;
	config.vote_relay->request_timeout = 10s;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);

	nano::keypair rep1;
	nano::keypair rep2;
	node.rep_crawler.force_add_rep (rep1.pub, nano::test::fake_channel (node));
	node.rep_crawler.force_add_rep (rep2.pub, nano::test::fake_channel (node));

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::block_hash hash{ 100 };
	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 7, { { hash, nano::root{ 100 } } }, { rep1.pub, rep2.pub } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query), 2);

	// One answer below the threshold goes out on its own once the interval passed, the request stays open
	auto vote1 = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (node.vote_processor.vote (vote1, channel, nano::vote_source::live));
	ASSERT_TIMELY_EQ (5s, acks.votes (7).size (), 1);
	ASSERT_FALSE (acks.terminated (7));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::flush));

	// The other answer completes it
	auto vote2 = nano::test::make_final_vote (rep2, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (node.vote_processor.vote (vote2, channel, nano::vote_source::live));
	ASSERT_TIMELY (5s, acks.terminated (7));
	ASSERT_EQ (2, acks.votes (7).size ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::done));
}

/*
 * A request whose representatives stop answering should end on the idle timeout rather than wait out its lifetime, the unsent vote travelling with the terminator
 */
TEST (vote_relay, idle)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_relay->enable = true;
	config.vote_relay->idle_timeout = 500ms;
	config.vote_relay->flush_interval = 10s;
	config.vote_relay->request_timeout = 10s;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);

	nano::keypair rep1;
	nano::keypair rep2;
	node.rep_crawler.force_add_rep (rep1.pub, nano::test::fake_channel (node));
	node.rep_crawler.force_add_rep (rep2.pub, nano::test::fake_channel (node));

	auto channel = nano::test::test_channel (node);
	ack_collector acks;
	acks.connect (channel);

	nano::block_hash hash{ 100 };
	nano::messages::vote_relay_req req{ nano::dev::network_params.network, 7, { { hash, nano::root{ 100 } } }, { rep1.pub, rep2.pub } };
	ASSERT_TRUE (node.vote_relay.request (req, channel));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::query), 2);

	// One representative answers, the other never does, so the request ends idle well before its lifetime
	auto vote1 = nano::test::make_final_vote (rep1, std::vector<nano::block_hash>{ hash });
	ASSERT_TRUE (node.vote_processor.vote (vote1, channel, nano::vote_source::live));
	ASSERT_TIMELY (5s, acks.terminated (7));
	ASSERT_EQ (1, acks.votes (7).size ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::idle));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::flush));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::timeout));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::done));
}
