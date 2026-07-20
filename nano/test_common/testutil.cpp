#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/manual.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_cemented.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/confirmation_height.hpp>
#include <nano/store/ledger/peer.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <numeric>

using namespace std::chrono_literals;

void nano::test::wait_peer_connections (nano::test::system & system_a)
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

bool nano::test::process (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	auto const transaction = node.ledger.tx_begin_write ();
	for (auto & block : blocks)
	{
		auto result = node.process (transaction, block);
		debug_assert (result == nano::block_status::progress || result == nano::block_status::old);
	}
	return true;
}

bool nano::test::process_live (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	for (auto & block : blocks)
	{
		node.process_active (block);
	}
	return true;
}

bool nano::test::confirmed (nano::node & node, std::vector<nano::block_hash> hashes)
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

bool nano::test::confirmed (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	return confirmed (node, blocks_to_hashes (blocks));
}

bool nano::test::exists (nano::node & node, std::vector<nano::block_hash> hashes)
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

bool nano::test::exists (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	return exists (node, blocks_to_hashes (blocks));
}

void nano::test::confirm (nano::node & node, std::vector<std::shared_ptr<nano::block>> const blocks)
{
	confirm (node.ledger, blocks);
}

void nano::test::confirm (nano::ledger & ledger, std::vector<std::shared_ptr<nano::block>> const blocks)
{
	for (auto const block : blocks)
	{
		confirm (ledger, block);
	}
}

void nano::test::confirm (nano::ledger & ledger, std::shared_ptr<nano::block> const block)
{
	confirm (ledger, block->hash ());
}

void nano::test::confirm (nano::ledger & ledger, nano::block_hash const & hash)
{
	auto transaction = ledger.tx_begin_write ();
	ledger.cement (transaction, hash);
}

bool nano::test::block_or_pruned_all_exists (nano::node & node, std::vector<nano::block_hash> hashes)
{
	auto transaction = node.ledger.tx_begin_read ();
	return std::all_of (hashes.begin (), hashes.end (),
	[&] (const auto & hash) {
		return node.ledger.any.block_exists_or_pruned (transaction, hash);
	});
}

bool nano::test::block_or_pruned_all_exists (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	return block_or_pruned_all_exists (node, blocks_to_hashes (blocks));
}

bool nano::test::block_or_pruned_none_exists (nano::node & node, std::vector<nano::block_hash> hashes)
{
	auto transaction = node.ledger.tx_begin_read ();
	return std::none_of (hashes.begin (), hashes.end (),
	[&] (const auto & hash) {
		return node.ledger.any.block_exists_or_pruned (transaction, hash);
	});
}

bool nano::test::block_or_pruned_none_exists (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	return block_or_pruned_none_exists (node, blocks_to_hashes (blocks));
}

bool nano::test::activate (nano::node & node, std::vector<nano::block_hash> hashes)
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

bool nano::test::activate (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	return activate (node, blocks_to_hashes (blocks));
}

bool nano::test::active (nano::node & node, std::vector<nano::block_hash> hashes)
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

bool nano::test::active (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	return active (node, blocks_to_hashes (blocks));
}

std::shared_ptr<nano::vote> nano::test::make_vote (nano::keypair key, std::vector<nano::block_hash> hashes, uint64_t timestamp, uint8_t duration)
{
	return std::make_shared<nano::vote> (key.pub, key.prv, timestamp, duration, hashes);
}

std::shared_ptr<nano::vote> nano::test::make_vote (nano::keypair key, std::vector<std::shared_ptr<nano::block>> blocks, uint64_t timestamp, uint8_t duration)
{
	std::vector<nano::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return make_vote (key, hashes, timestamp, duration);
}

std::shared_ptr<nano::vote> nano::test::make_final_vote (nano::keypair key, std::vector<nano::block_hash> hashes)
{
	return make_vote (key, hashes, nano::vote::timestamp_max, nano::vote::duration_max);
}

std::shared_ptr<nano::vote> nano::test::make_final_vote (nano::keypair key, std::vector<std::shared_ptr<nano::block>> blocks)
{
	return make_vote (key, blocks, nano::vote::timestamp_max, nano::vote::duration_max);
}

std::vector<nano::block_hash> nano::test::blocks_to_hashes (std::vector<std::shared_ptr<nano::block>> blocks)
{
	std::vector<nano::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return hashes;
}

std::vector<std::shared_ptr<nano::block>> nano::test::clone (std::vector<std::shared_ptr<nano::block>> blocks)
{
	std::vector<std::shared_ptr<nano::block>> clones;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (clones), [] (auto & block) { return block->clone (); });
	return clones;
}

std::shared_ptr<nano::transport::channel> nano::test::fake_channel (nano::node & node, nano::account node_id)
{
	nano::transport::peer_info const peer{
		.node_id = node_id.is_zero () ? node.get_node_id () : node_id,
		.protocol_version = node.network_params.network.protocol_version,
	};
	return std::make_shared<nano::transport::fake::channel> (node, peer);
}

std::shared_ptr<nano::transport::test_channel> nano::test::test_channel (nano::node & node, nano::account node_id)
{
	nano::transport::peer_info const peer{
		.node_id = node_id,
		.protocol_version = node.network_params.network.protocol_version,
	};
	return std::make_shared<nano::transport::test_channel> (node, peer);
}

std::shared_ptr<nano::election> nano::test::start_election (nano::test::system & system, nano::node & node, const nano::block_hash & hash)
{
	system.deadline_set (5s);

	// Wait until and ensure that the block is in the ledger
	auto block_l = node.block (hash);
	debug_assert (block_l);

	auto fut = node.scheduler.manual.push (block_l);

	// Wait for the block to be scheduled
	auto status = fut.wait_for (5s);
	debug_assert (status == std::future_status::ready);

	auto election = fut.get ();
	return election;
}

bool nano::test::start_elections (nano::test::system & system, nano::node & node, std::vector<nano::block_hash> const & hashes, bool const forced)
{
	for (auto const & hash_l : hashes)
	{
		auto election = start_election (system, node, hash_l);
		if (!election)
		{
			return false;
		}
		if (forced)
		{
			election->force_confirm ();
		}
	}
	return true;
}

bool nano::test::start_elections (nano::test::system & system, nano::node & node, std::vector<std::shared_ptr<nano::block>> const & blocks, bool const forced)
{
	return start_elections (system, node, blocks_to_hashes (blocks), forced);
}

nano::account_info nano::test::account_info (nano::node const & node, nano::account const & acc)
{
	auto const tx = node.ledger.tx_begin_read ();
	auto opt = node.ledger.any.account_get (tx, acc);
	if (opt.has_value ())
	{
		return opt.value ();
	}
	return {};
}

void nano::test::print_all_receivable_entries (const nano::store::ledger_store & store)
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

void nano::test::print_all_account_info (const nano::ledger & ledger)
{
	std::cout << "Printing all account info:\n";
	auto const tx = ledger.tx_begin_read ();
	auto const end = ledger.store.account.end (tx);
	for (auto i = ledger.store.account.begin (tx); i != end; ++i)
	{
		nano::account acc = i->first;
		nano::account_info acc_info = i->second;
		nano::confirmation_height_info height_info;
		std::cout << "Account: " << acc.to_account () << std::endl;
		std::cout << "  Unconfirmed Balance: " << acc_info.balance.to_string_dec () << std::endl;
		std::cout << "  Confirmed Balance:   " << ledger.cemented.account_balance (tx, acc).value_or (0) << std::endl;
		std::cout << "  Block Count:         " << acc_info.block_count << std::endl;
		if (!ledger.store.confirmation_height.get (tx, acc, height_info))
		{
			std::cout << "  Conf. Height:        " << height_info.height << std::endl;
			std::cout << "  Conf. Frontier:      " << height_info.frontier.to_string () << std::endl;
		}
	}
}

void nano::test::print_all_blocks (const nano::store::ledger_store & store)
{
	auto tx = store.tx_begin_read ();
	auto i = store.block.begin (tx);
	auto end = store.block.end (tx);
	std::cout << "Listing all blocks" << std::endl;
	for (; i != end; ++i)
	{
		nano::block_hash hash = i->first;
		nano::store::block_w_sideband sideband = i->second;
		std::shared_ptr<nano::block> b = sideband.block;
		std::cout << "Hash: " << hash.to_string () << std::endl;
		const auto acc = sideband.sideband.account;
		std::cout << "Acc: " << acc.to_string () << "(" << acc.to_account () << ")" << std::endl;
		std::cout << "Height: " << sideband.sideband.height << std::endl;
		std::cout << b->to_json ();
	}
}

std::vector<std::shared_ptr<nano::block>> nano::test::all_blocks (nano::node & node)
{
	auto transaction = node.store.tx_begin_read ();
	std::vector<std::shared_ptr<nano::block>> result;
	for (auto it = node.store.block.begin (transaction), end = node.store.block.end (transaction); it != end; ++it)
	{
		result.push_back (it->second.block);
	}
	return result;
}

nano::uint128_t nano::test::minimum_principal_weight ()
{
	return nano::dev::genesis->balance ().number () / nano::dev::network_params.network.principal_weight_factor;
}
