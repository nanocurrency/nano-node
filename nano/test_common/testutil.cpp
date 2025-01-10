#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/manual.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/transport/fake.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/store/block.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <numeric>

using namespace std::chrono_literals;

void celerix::test::wait_peer_connections (celerix::test::system & system_a)
{
	auto wait_peer_count = [&system_a] (bool in_memory) {
		auto num_nodes = system_a.nodes.size ();
		system_a.deadline_set (20s);
		size_t peer_count = 0;
		while (peer_count != num_nodes * (num_nodes - 1))
		{
			ASSERT_NO_ERROR (system_a.poll ());
			peer_count = std::accumulate (system_a.nodes.cbegin (), system_a.nodes.cend (), std::size_t{ 0 }, [in_memory] (auto total, auto const & node) {
				if (in_memory)
				{
					return total += node->network.size ();
				}
				else
				{
					auto transaction = node->store.tx_begin_read ();
					return total += node->store.peer.count (transaction);
				}
			});
		}
	};

	// Do a pre-pass with in-memory containers to reduce IO if still in the process of connecting to peers
	wait_peer_count (true);
	wait_peer_count (false);
}

celerix::hash_or_account celerix::test::random_hash_or_account ()
{
	celerix::hash_or_account random_hash;
	celerix::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	return random_hash;
}

celerix::block_hash celerix::test::random_hash ()
{
	return celerix::test::random_hash_or_account ().as_block_hash ();
}

celerix::account celerix::test::random_account ()
{
	return celerix::test::random_hash_or_account ().as_account ();
}

bool celerix::test::process (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	auto const transaction = node.ledger.tx_begin_write ();
	for (auto & block : blocks)
	{
		auto result = node.process (transaction, block);
		if (result != celerix::block_status::progress && result != celerix::block_status::old)
		{
			return false;
		}
	}
	return true;
}

bool celerix::test::process_live (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	for (auto & block : blocks)
	{
		node.process_active (block);
	}
	return true;
}

bool celerix::test::confirmed (celerix::node & node, std::vector<celerix::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		if (!node.block_confirmed (hash))
		{
			return false;
		}
	}
	return true;
}

bool celerix::test::confirmed (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	return confirmed (node, blocks_to_hashes (blocks));
}

bool celerix::test::exists (celerix::node & node, std::vector<celerix::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		if (!node.block (hash))
		{
			return false;
		}
	}
	return true;
}

bool celerix::test::exists (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	return exists (node, blocks_to_hashes (blocks));
}

void celerix::test::confirm (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> const blocks)
{
	confirm (node.ledger, blocks);
}

void celerix::test::confirm (celerix::ledger & ledger, std::vector<std::shared_ptr<celerix::block>> const blocks)
{
	for (auto const block : blocks)
	{
		confirm (ledger, block);
	}
}

void celerix::test::confirm (celerix::ledger & ledger, std::shared_ptr<celerix::block> const block)
{
	confirm (ledger, block->hash ());
}

void celerix::test::confirm (celerix::ledger & ledger, celerix::block_hash const & hash)
{
	auto transaction = ledger.tx_begin_write ();
	ledger.confirm (transaction, hash);
}

bool celerix::test::block_or_pruned_all_exists (celerix::node & node, std::vector<celerix::block_hash> hashes)
{
	auto transaction = node.ledger.tx_begin_read ();
	return std::all_of (hashes.begin (), hashes.end (),
	[&] (const auto & hash) {
		return node.ledger.any.block_exists_or_pruned (transaction, hash);
	});
}

bool celerix::test::block_or_pruned_all_exists (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	return block_or_pruned_all_exists (node, blocks_to_hashes (blocks));
}

bool celerix::test::block_or_pruned_none_exists (celerix::node & node, std::vector<celerix::block_hash> hashes)
{
	auto transaction = node.ledger.tx_begin_read ();
	return std::none_of (hashes.begin (), hashes.end (),
	[&] (const auto & hash) {
		return node.ledger.any.block_exists_or_pruned (transaction, hash);
	});
}

bool celerix::test::block_or_pruned_none_exists (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	return block_or_pruned_none_exists (node, blocks_to_hashes (blocks));
}

bool celerix::test::activate (celerix::node & node, std::vector<celerix::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		auto disk_block = node.block (hash);
		if (disk_block == nullptr)
		{
			// Block does not exist in the ledger yet
			return false;
		}
		node.scheduler.manual.push (disk_block);
	}
	return true;
}

bool celerix::test::activate (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	return activate (node, blocks_to_hashes (blocks));
}

bool celerix::test::active (celerix::node & node, std::vector<celerix::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		if (!node.vote_router.active (hash))
		{
			return false;
		}
	}
	return true;
}

bool celerix::test::active (celerix::node & node, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	return active (node, blocks_to_hashes (blocks));
}

std::shared_ptr<celerix::vote> celerix::test::make_vote (celerix::keypair key, std::vector<celerix::block_hash> hashes, uint64_t timestamp, uint8_t duration)
{
	return std::make_shared<celerix::vote> (key.pub, key.prv, timestamp, duration, hashes);
}

std::shared_ptr<celerix::vote> celerix::test::make_vote (celerix::keypair key, std::vector<std::shared_ptr<celerix::block>> blocks, uint64_t timestamp, uint8_t duration)
{
	std::vector<celerix::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return make_vote (key, hashes, timestamp, duration);
}

std::shared_ptr<celerix::vote> celerix::test::make_final_vote (celerix::keypair key, std::vector<celerix::block_hash> hashes)
{
	return make_vote (key, hashes, celerix::vote::timestamp_max, celerix::vote::duration_max);
}

std::shared_ptr<celerix::vote> celerix::test::make_final_vote (celerix::keypair key, std::vector<std::shared_ptr<celerix::block>> blocks)
{
	return make_vote (key, blocks, celerix::vote::timestamp_max, celerix::vote::duration_max);
}

std::vector<celerix::block_hash> celerix::test::blocks_to_hashes (std::vector<std::shared_ptr<celerix::block>> blocks)
{
	std::vector<celerix::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return hashes;
}

std::vector<std::shared_ptr<celerix::block>> celerix::test::clone (std::vector<std::shared_ptr<celerix::block>> blocks)
{
	std::vector<std::shared_ptr<celerix::block>> clones;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (clones), [] (auto & block) { return block->clone (); });
	return clones;
}

std::shared_ptr<celerix::transport::fake::channel> celerix::test::fake_channel (celerix::node & node, celerix::account node_id)
{
	auto channel = std::make_shared<celerix::transport::fake::channel> (node);
	if (!node_id.is_zero ())
	{
		channel->set_node_id (node_id);
	}
	return channel;
}

std::shared_ptr<celerix::election> celerix::test::start_election (celerix::test::system & system_a, celerix::node & node_a, const celerix::block_hash & hash_a)
{
	system_a.deadline_set (5s);

	// wait until and ensure that the block is in the ledger
	auto block_l = node_a.block (hash_a);
	while (!block_l)
	{
		if (system_a.poll ())
		{
			return nullptr;
		}
		block_l = node_a.block (hash_a);
	}

	node_a.scheduler.manual.push (block_l);

	// wait for the election to appear
	std::shared_ptr<celerix::election> election = node_a.active.election (block_l->qualified_root ());
	while (!election)
	{
		if (system_a.poll ())
		{
			return nullptr;
		}
		election = node_a.active.election (block_l->qualified_root ());
	}

	election->transition_active ();
	return election;
}

bool celerix::test::start_elections (celerix::test::system & system_a, celerix::node & node_a, std::vector<celerix::block_hash> const & hashes_a, bool const forced_a)
{
	for (auto const & hash_l : hashes_a)
	{
		auto election = celerix::test::start_election (system_a, node_a, hash_l);
		if (!election)
		{
			return false;
		}
		if (forced_a)
		{
			election->force_confirm ();
		}
	}
	return true;
}

bool celerix::test::start_elections (celerix::test::system & system_a, celerix::node & node_a, std::vector<std::shared_ptr<celerix::block>> const & blocks_a, bool const forced_a)
{
	return celerix::test::start_elections (system_a, node_a, blocks_to_hashes (blocks_a), forced_a);
}

celerix::account_info celerix::test::account_info (celerix::node const & node, celerix::account const & acc)
{
	auto const tx = node.ledger.tx_begin_read ();
	auto opt = node.ledger.any.account_get (tx, acc);
	if (opt.has_value ())
	{
		return opt.value ();
	}
	return {};
}

void celerix::test::print_all_receivable_entries (const celerix::store::component & store)
{
	std::cout << "Printing all receivable entries:\n";
	auto const tx = store.tx_begin_read ();
	auto const end = store.pending.end (tx);
	for (auto i = store.pending.begin (tx); i != end; ++i)
	{
		std::cout << "Key:  " << i->first << std::endl;
		std::cout << "Info: " << i->second << std::endl;
	}
}

void celerix::test::print_all_account_info (const celerix::ledger & ledger)
{
	std::cout << "Printing all account info:\n";
	auto const tx = ledger.tx_begin_read ();
	auto const end = ledger.store.account.end (tx);
	for (auto i = ledger.store.account.begin (tx); i != end; ++i)
	{
		celerix::account acc = i->first;
		celerix::account_info acc_info = i->second;
		celerix::confirmation_height_info height_info;
		std::cout << "Account: " << acc.to_account () << std::endl;
		std::cout << "  Unconfirmed Balance: " << acc_info.balance.to_string_dec () << std::endl;
		std::cout << "  Confirmed Balance:   " << ledger.confirmed.account_balance (tx, acc).value_or (0) << std::endl;
		std::cout << "  Block Count:         " << acc_info.block_count << std::endl;
		if (!ledger.store.confirmation_height.get (tx, acc, height_info))
		{
			std::cout << "  Conf. Height:        " << height_info.height << std::endl;
			std::cout << "  Conf. Frontier:      " << height_info.frontier.to_string () << std::endl;
		}
	}
}

void celerix::test::print_all_blocks (const celerix::store::component & store)
{
	auto tx = store.tx_begin_read ();
	auto i = store.block.begin (tx);
	auto end = store.block.end (tx);
	std::cout << "Listing all blocks" << std::endl;
	for (; i != end; ++i)
	{
		celerix::block_hash hash = i->first;
		celerix::store::block_w_sideband sideband = i->second;
		std::shared_ptr<celerix::block> b = sideband.block;
		std::cout << "Hash: " << hash.to_string () << std::endl;
		const auto acc = sideband.sideband.account;
		std::cout << "Acc: " << acc.to_string () << "(" << acc.to_account () << ")" << std::endl;
		std::cout << "Height: " << sideband.sideband.height << std::endl;
		std::cout << b->to_json ();
	}
}

std::vector<std::shared_ptr<celerix::block>> celerix::test::all_blocks (celerix::node & node)
{
	auto transaction = node.store.tx_begin_read ();
	std::vector<std::shared_ptr<celerix::block>> result;
	for (auto it = node.store.block.begin (transaction), end = node.store.block.end (transaction); it != end; ++it)
	{
		result.push_back (it->second.block);
	}
	return result;
}

celerix::uint128_t celerix::test::minimum_principal_weight ()
{
	return celerix::dev::genesis->balance ().number () / celerix::dev::network_params.network.principal_weight_factor;
}
