#include <nano/crypto_lib/random_pool.hpp>
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
	for (auto & block : blocks)
	{
		auto result = node.process (*block);
		if (result.code != nano::process_result::progress)
		{
			return false;
		}
	}
	return true;
}

bool nano::test::confirm (nano::node & node, std::vector<nano::block_hash> hashes)
{
	// Finish processing all blocks
	node.block_processor.flush ();
	for (auto & hash : hashes)
	{
		if (node.block_confirmed (hash))
		{
			continue;
		}

		auto disk_block = node.block (hash);
		// A sideband is required to start an election
		release_assert (disk_block != nullptr);
		release_assert (disk_block->has_sideband ());
		// This only starts election
		auto election = node.block_confirm (disk_block);
		if (election == nullptr)
		{
			return false;
		}
		// Here we actually confirm the block
		election->force_confirm ();
	}
	return true;
}

bool nano::test::confirm (nano::node & node, std::vector<std::shared_ptr<nano::block>> blocks)
{
	std::vector<nano::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return confirm (node, hashes);
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
	std::vector<nano::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return confirmed (node, hashes);
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