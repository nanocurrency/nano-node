#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/vote_rebroadcaster.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>
#include <nano/test_common/topology.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <iomanip>
#include <iostream>
#include <set>

using namespace std::chrono_literals;

namespace
{
struct relay_network_params final
{
	std::size_t relays{ 2 };
	std::size_t hidden_reps{ 3 }; // One more representative is public
	std::size_t hidden_nodes{ 2 }; // Ordinary nodes behind NAT, they publish the spam
	std::size_t spam_accounts{ 20 };
	std::size_t spam_blocks{ 25 }; // Chain length per spam account
	std::size_t fork_accounts{ 4 }; // Accounts publishing two competing blocks from different nodes instead of a chain
	std::chrono::milliseconds spam_interval{ 2ms }; // Pause between published blocks
	bool relays_rebroadcast{ true }; // Relays forward the votes they receive, otherwise relay requests are the only path
	std::chrono::seconds deadline{ 120s }; // Per phase
};

// Instrumented builds get a fraction of the load and more time
relay_network_params scale_down (relay_network_params params)
{
	if (nano::memory_intensive_instrumentation () || nano::slow_instrumentation ())
	{
		params.spam_accounts = 4;
		params.spam_blocks = 5;
		params.fork_accounts = 1;
		params.deadline = 300s;
	}
	return params;
}

struct named_node final
{
	std::string name;
	std::shared_ptr<nano::node> node;
};

struct spam_account final
{
	nano::keypair key;
	std::shared_ptr<nano::block> send; // Funding send from genesis
	std::shared_ptr<nano::block> open;
	nano::block_list_t chain; // Sends on top of the open, empty for fork accounts
	std::shared_ptr<nano::block> fork_a; // Competing sends at the same height, fork accounts only
	std::shared_ptr<nano::block> fork_b;

	bool forked () const
	{
		return fork_a != nullptr;
	}
};

/*
 * relay 1 ----- relay 2                public, serve relay requests, do not vote
 *    |   \     /   |
 *    |   public rep                    voting, reachable by everyone
 *    |    /    \   |
 *  hidden reps   hidden nodes          behind NAT: they reach the public nodes and never each other
 */
struct relay_network final
{
	relay_network_params params;
	nano::test::topology topo;
	std::deque<nano::keypair> reps; // The first one is the public rep
	std::vector<named_node> relays;
	named_node public_rep;
	std::vector<named_node> hidden_reps;
	std::vector<named_node> hidden_nodes;
	std::vector<named_node> all; // Every node with its label

	relay_network (nano::test::system & system, relay_network_params const & params_a) :
		params{ params_a },
		topo{ system }
	{
	}

	// Hidden reps and hidden nodes, the ones that depend on the relays
	std::vector<named_node> hidden () const
	{
		auto result = hidden_reps;
		result.insert (result.end (), hidden_nodes.begin (), hidden_nodes.end ());
		return result;
	}

	bool all_cemented (uint64_t count) const
	{
		return std::all_of (all.begin (), all.end (), [count] (auto const & entry) {
			return entry.node->ledger.cemented_count () == count;
		});
	}

	bool all_idle () const
	{
		return std::all_of (all.begin (), all.end (), [] (auto const & entry) {
			return entry.node->active.empty ();
		});
	}
};

uint64_t count (nano::node const & node, nano::stat::type type, nano::stat::detail detail, nano::stat::dir dir = nano::stat::dir::in)
{
	return node.stats.count (type, detail, dir);
}

// Counters per node, one table for the relay client side and one for the relay server side
void report (relay_network const & net)
{
	using type = nano::stat::type;
	using detail = nano::stat::detail;
	auto const w = std::setw (11);

	std::cout << std::left << std::setw (14) << "client" << std::right
			  << w << "cemented" << w << "channels" << w << "accepted" << w << "blocklist"
			  << w << "requests" << w << "done" << w << "timeout" << w << "failed" << w << "relay_full" << w << "unsolicit" << w << "overflow" << w << "votes" << w << "via_relay"
			  << "\n";
	for (auto const & [name, node] : net.all)
	{
		std::cout << std::left << std::setw (14) << name << std::right
				  << w << node->ledger.cemented_count ()
				  << w << node->network.size ()
				  << w << count (*node, type::tcp_channels, detail::channel_accepted)
				  << w << count (*node, type::tcp_channels_rejected, detail::blocklisted)
				  << w << count (*node, type::vote_relay_client, detail::request)
				  << w << count (*node, type::vote_relay_client, detail::done)
				  << w << count (*node, type::vote_relay_client, detail::timeout)
				  << w << count (*node, type::vote_relay_client, detail::request_failed)
				  << w << count (*node, type::vote_relay_client, detail::relay_full)
				  << w << count (*node, type::vote_relay_client, detail::unsolicited)
				  << w << count (*node, type::vote_relay_client, detail::queue_overflow)
				  << w << count (*node, type::vote_relay_client, detail::vote)
				  << w << count (*node, type::vote_processor_source, detail::relay)
				  << "\n";
	}

	std::cout << std::left << std::setw (14) << "server" << std::right
			  << w << "requests" << w << "overfill" << w << "cache" << w << "queries" << w << "unknown" << w << "duplicate" << w << "done" << w << "timeout" << w << "acks" << w << "votes"
			  << "\n";
	for (auto const & [name, node] : net.relays)
	{
		std::cout << std::left << std::setw (14) << name << std::right
				  << w << count (*node, type::vote_relay, detail::request)
				  << w << count (*node, type::vote_relay, detail::overfill)
				  << w << count (*node, type::vote_relay, detail::cache)
				  << w << count (*node, type::vote_relay, detail::query)
				  << w << count (*node, type::vote_relay, detail::rep_unknown)
				  << w << count (*node, type::vote_relay, detail::duplicate)
				  << w << count (*node, type::vote_relay, detail::done)
				  << w << count (*node, type::vote_relay, detail::timeout)
				  << w << count (*node, type::vote_relay, detail::reply)
				  << w << count (*node, type::vote_relay, detail::vote)
				  << "\n";
	}
	std::cout << std::flush;
}

// Start every node and wire the links a NAT network would end up with
void build (nano::test::system & system, relay_network & net)
{
	auto const & params = net.params;

	// Every representative gets the same weight, genesis keeps a reserve to fund the spam accounts
	for (std::size_t i = 0; i < 1 + params.hidden_reps; ++i)
	{
		net.reps.emplace_back ();
	}
	nano::amount const reserve{ 1000000 };
	system.ledger_initialization_set (net.reps, reserve);
	system.set_cemented_initialization_blocks (system.initialization_blocks);
	system.set_initialization_blocks ({});

	// Quorum needs a majority of the reps from the first election on, not just whichever rep was seen first
	nano::amount const rep_weight{ nano::dev::constants.genesis_amount - reserve.number () };
	auto config_for = [&] (bool relay) {
		auto config = system.default_config ();
		config.online_weight_minimum = rep_weight;
		if (relay)
		{
			config.vote_relay->enable = true;
			config.vote_rebroadcaster->enable = params.relays_rebroadcast;
		}
		return config;
	};

	for (std::size_t i = 0; i < params.relays; ++i)
	{
		net.relays.push_back ({ "relay_" + std::to_string (i), net.topo.add_public (config_for (true)) });
	}
	net.public_rep = { "public_rep", net.topo.add_public (config_for (false), {}, net.reps[0]) };
	for (std::size_t i = 0; i < params.hidden_reps; ++i)
	{
		net.hidden_reps.push_back ({ "hidden_rep_" + std::to_string (i), net.topo.add_hidden (config_for (false), {}, net.reps[1 + i]) });
	}
	for (std::size_t i = 0; i < params.hidden_nodes; ++i)
	{
		net.hidden_nodes.push_back ({ "hidden_node_" + std::to_string (i), net.topo.add_hidden (config_for (false)) });
	}
	net.all = net.relays;
	net.all.push_back (net.public_rep);
	for (auto const & entry : net.hidden ())
	{
		net.all.push_back (entry);
	}

	// Public nodes peer with each other, everything hidden connects outward to all of them
	for (std::size_t i = 0; i < net.relays.size (); ++i)
	{
		for (std::size_t j = i + 1; j < net.relays.size (); ++j)
		{
			ASSERT_NO_ERROR (net.topo.connect (*net.relays[i].node, *net.relays[j].node));
		}
		ASSERT_NO_ERROR (net.topo.connect (*net.public_rep.node, *net.relays[i].node));
	}
	for (auto const & entry : net.hidden ())
	{
		for (auto const & relay : net.relays)
		{
			ASSERT_NO_ERROR (net.topo.connect (*entry.node, *relay.node));
		}
		ASSERT_NO_ERROR (net.topo.connect (*entry.node, *net.public_rep.node));
	}

	// The relays learn every representative, the hidden nodes see the relays
	ASSERT_TIMELY (30s, std::all_of (net.relays.begin (), net.relays.end (), [&] (auto const & relay) {
		return relay.node->rep_crawler.principal_representatives ().size () == net.reps.size ();
	}));
	for (auto const & entry : net.hidden ())
	{
		ASSERT_TIMELY (30s, entry.node->vote_relay_client.relays (8).size () == params.relays);
	}
}

// Fund the spam accounts from genesis, the funding blocks enter the network from the first hidden node
std::vector<spam_account> fund (nano::test::system & system, relay_network & net)
{
	auto const & params = net.params;
	auto & publisher = *net.hidden_nodes.front ().node;

	auto previous = publisher.latest (nano::dev::genesis_key.pub);
	auto balance = publisher.balance (nano::dev::genesis_key.pub);

	std::vector<spam_account> accounts (params.spam_accounts);
	nano::state_block_builder builder;
	for (std::size_t i = 0; i < accounts.size (); ++i)
	{
		auto & account = accounts[i];
		// A chain of sends of one raw each, a forked account only needs enough for its single height
		nano::uint128_t const amount = i < params.fork_accounts ? 2 : params.spam_blocks + 1;
		balance -= amount;
		account.send = builder.make_block ()
					   .account (nano::dev::genesis_key.pub)
					   .previous (previous)
					   .representative (nano::dev::genesis_key.pub)
					   .balance (balance)
					   .link (account.key.pub)
					   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					   .work (*system.work.generate (previous))
					   .build ();
		previous = account.send->hash ();
		account.open = builder.make_block ()
					   .account (account.key.pub)
					   .previous (0)
					   .representative (account.key.pub)
					   .balance (amount)
					   .link (account.send->hash ())
					   .sign (account.key.prv, account.key.pub)
					   .work (*system.work.generate (account.key.pub))
					   .build ();

		EXPECT_EQ (nano::block_status::progress, publisher.process_local (account.send).value ());
		EXPECT_EQ (nano::block_status::progress, publisher.process_local (account.open).value ());
	}
	return accounts;
}

// Build the spam chains and the competing fork blocks with their work, nothing is published yet
void prepare_spam (nano::test::system & system, relay_network const & net, std::vector<spam_account> & accounts)
{
	auto const & params = net.params;
	nano::state_block_builder builder;
	for (std::size_t i = 0; i < accounts.size (); ++i)
	{
		auto & account = accounts[i];
		auto send = [&] (nano::block_hash const & previous, nano::uint128_t balance) {
			return builder.make_block ()
			.account (account.key.pub)
			.previous (previous)
			.representative (account.key.pub)
			.balance (balance)
			.link (nano::keypair ().pub)
			.sign (account.key.prv, account.key.pub)
			.work (*system.work.generate (previous))
			.build ();
		};
		if (i < params.fork_accounts)
		{
			account.fork_a = send (account.open->hash (), 1);
			account.fork_b = send (account.open->hash (), 1);
		}
		else
		{
			auto previous = account.open->hash ();
			nano::uint128_t balance = params.spam_blocks + 1;
			for (std::size_t n = 0; n < params.spam_blocks; ++n)
			{
				balance -= 1;
				account.chain.push_back (send (previous, balance));
				previous = account.chain.back ()->hash ();
			}
		}
	}
}

// Publish the chains interleaved so many elections run at once, each account from a fixed hidden node, and the forks from two different nodes at the same time
void publish_spam (nano::test::system & system, relay_network const & net, std::vector<spam_account> const & accounts)
{
	auto const & params = net.params;
	auto publisher = [&] (std::size_t index) -> nano::node & {
		return *net.hidden_nodes[index % net.hidden_nodes.size ()].node;
	};

	for (std::size_t height = 0; height < params.spam_blocks; ++height)
	{
		for (std::size_t i = 0; i < accounts.size (); ++i)
		{
			auto const & account = accounts[i];
			if (account.forked ())
			{
				continue;
			}
			publisher (i).process_local_async (account.chain[height]);
			system.delay_ms (params.spam_interval);
		}
	}

	for (auto const & account : accounts)
	{
		if (account.forked ())
		{
			publisher (0).process_local_async (account.fork_a);
			publisher (1).process_local_async (account.fork_b);
			system.delay_ms (params.spam_interval);
		}
	}
}

void run_scenario (nano::test::system & system, relay_network_params const & params)
{
	relay_network net{ system, params };
	ASSERT_NO_FATAL_FAILURE (build (system, net));

	uint64_t const initial_count = 1 + 2 * net.reps.size (); // Genesis plus a send and an open per rep
	ASSERT_TRUE (net.all_cemented (initial_count));

	// Phase 1: the funding blocks confirm everywhere, this already needs the hidden reps' votes to travel through the relays
	auto accounts = fund (system, net);
	uint64_t const funded_count = initial_count + 2 * accounts.size ();
	ASSERT_TIMELY (params.deadline, net.all_cemented (funded_count));

	// Phase 2: the spam burst with forks mixed in
	prepare_spam (system, net, accounts);
	publish_spam (system, net, accounts);
	uint64_t const spam_count = funded_count + (accounts.size () - params.fork_accounts) * params.spam_blocks + params.fork_accounts;
	ASSERT_TIMELY (params.deadline, net.all_cemented (spam_count));
	ASSERT_TIMELY (30s, net.all_idle ());

	report (net);

	// Every node settled on the same frontier for every account, including the forked ones
	for (auto const & account : accounts)
	{
		std::set<nano::block_hash> frontiers;
		for (auto const & entry : net.all)
		{
			frontiers.insert (entry.node->latest (account.key.pub));
		}
		ASSERT_EQ (1, frontiers.size ()) << "nodes disagree on the frontier of " << account.key.pub.to_account ();
		auto const frontier = *frontiers.begin ();
		if (account.forked ())
		{
			ASSERT_TRUE (frontier == account.fork_a->hash () || frontier == account.fork_b->hash ());
		}
		else
		{
			ASSERT_EQ (account.chain.back ()->hash (), frontier);
		}
	}

	// The hidden nodes never peered with each other or learned a hidden rep directly, and were refused when discovery made them try
	auto const hidden = net.hidden ();
	for (auto const & a : hidden)
	{
		for (auto const & b : hidden)
		{
			if (a.node != b.node)
			{
				ASSERT_FALSE (nano::test::topology::connected (*a.node, *b.node)) << a.name << " peered with " << b.name;
			}
		}
		for (auto const & rep : a.node->rep_crawler.principal_representatives ())
		{
			bool const own = std::any_of (net.reps.begin (), net.reps.end (), [&] (auto const & key) {
				return key.pub == rep.account && a.node->wallets.exists (key.pub);
			});
			ASSERT_TRUE (rep.account == net.reps[0].pub || own) << a.name << " reached a hidden rep directly";
		}
		EXPECT_GT (count (*a.node, nano::stat::type::tcp_channels_rejected, nano::stat::detail::blocklisted), 0) << a.name << " never tried to reach another hidden node";
	}

	// The relays did the work: every hidden node got votes through them and every relay served requests
	uint64_t relayed_votes = 0;
	for (auto const & entry : hidden)
	{
		auto const relayed = count (*entry.node, nano::stat::type::vote_processor_source, nano::stat::detail::relay);
		ASSERT_GT (relayed, 0) << entry.name << " received no votes through relays";
		relayed_votes += relayed;
		// Every ack matched an outstanding request, a relay that answers out of order or twice would show up here
		ASSERT_EQ (0, count (*entry.node, nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited)) << entry.name << " dropped acks";
	}
	for (auto const & relay : net.relays)
	{
		ASSERT_GT (count (*relay.node, nano::stat::type::vote_relay, nano::stat::detail::request), 0) << relay.name << " served no requests";
	}
	std::cout << "votes delivered through relays: " << relayed_votes << "\n";
}
}

/*
 * Reps and ordinary nodes behind NAT confirm a spam burst through the relays, with the relays forwarding votes as usual
 */
TEST (vote_relay, hidden_network_spam)
{
	nano::test::system system;
	run_scenario (system, scale_down ({}));
}

/*
 * The same network with relays that never forward votes, so relay requests are the only way for hidden nodes to see the hidden reps' votes
 */
TEST (vote_relay, hidden_network_spam_relay_only)
{
	nano::test::system system;
	run_scenario (system, scale_down ({ .relays_rebroadcast = false }));
}
