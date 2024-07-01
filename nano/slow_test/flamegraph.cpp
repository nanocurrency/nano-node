#include <nano/lib/blockbuilders.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
std::deque<nano::keypair> rep_set (size_t count)
{
	std::deque<nano::keypair> result;
	for (auto i = 0; i < count; ++i)
	{
		result.emplace_back (nano::keypair{});
	}
	return result;
}
}

TEST (flamegraph, large_direct_processing)
{
	auto reps = rep_set (4);
	auto circulating = 10 * nano::Gxrb_ratio;
	nano::test::system system;
	system.ledger_initialization_set (reps, circulating);
	auto & node = *system.add_node ();
	auto prepare = [&] () {
		nano::state_block_builder builder;
		std::deque<std::shared_ptr<nano::block>> blocks;
		std::deque<nano::keypair> keys;
		auto previous = *std::prev (std::prev (system.initialization_blocks.end ()));
		for (auto i = 0; i < 20000; ++i)
		{
			keys.emplace_back ();
			auto const & key = keys.back ();
			auto block = builder.make_block ()
						 .account (nano::dev::genesis_key.pub)
						 .representative (nano::dev::genesis_key.pub)
						 .previous (previous->hash ())
						 .link (key.pub)
						 .balance (previous->balance_field ().value ().number () - nano::xrb_ratio)
						 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						 .work (*system.work.generate (previous->hash ()))
						 .build ();
			blocks.push_back (block);
			previous = block;
		}
		return std::make_tuple (blocks, keys);
	};
	auto const & [blocks, keys] = prepare ();
	auto execute = [&] () {
		auto count = 0;
		for (auto block : blocks)
		{
			ASSERT_EQ (nano::block_status::progress, node.process (block));
		}
	};
	execute ();
}

TEST (flamegraph, large_confirmation)
{
	auto reps = rep_set (4);
	auto circulating = 10 * nano::Gxrb_ratio;
	nano::test::system system;
	system.ledger_initialization_set (reps, circulating);
	auto prepare = [&] () {
		nano::state_block_builder builder;
		std::deque<std::shared_ptr<nano::block>> blocks;
		std::deque<nano::keypair> keys;
		auto previous = *std::prev (std::prev (system.initialization_blocks.end ()));
		for (auto i = 0; i < 100; ++i)
		{
			keys.emplace_back ();
			auto const & key = keys.back ();
			auto block = builder.make_block ()
						 .account (nano::dev::genesis_key.pub)
						 .representative (nano::dev::genesis_key.pub)
						 .previous (previous->hash ())
						 .link (key.pub)
						 .balance (previous->balance_field ().value ().number () - nano::xrb_ratio)
						 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						 .work (*system.work.generate (previous->hash ()))
						 .build ();
			blocks.push_back (block);
			previous = block;
		}
		return std::make_tuple (blocks, keys);
	};
	auto const & [blocks, keys] = prepare ();
	system.initialization_blocks.insert (system.initialization_blocks.end (), blocks.begin (), blocks.end ());
	nano::node_config config;
	nano::node_flags flags;
	auto & node1 = *system.add_node (config, flags, nano::transport::transport_type::tcp, reps[0]);
	auto & node2 = *system.add_node (config, flags, nano::transport::transport_type::tcp, reps[1]);
	auto & node3 = *system.add_node (config, flags, nano::transport::transport_type::tcp, reps[2]);
	auto & node4 = *system.add_node (config, flags, nano::transport::transport_type::tcp, reps[3]);
	ASSERT_TIMELY (300s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (auto const & node) {
		return node->block_confirmed (system.initialization_blocks.back ()->hash ());
	}));
}
