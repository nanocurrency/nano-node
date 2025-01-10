#include <celerix/lib/blockbuilders.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
std::deque<celerix::keypair> rep_set (size_t count)
{
	std::deque<celerix::keypair> result;
	for (auto i = 0; i < count; ++i)
	{
		result.emplace_back (celerix::keypair{});
	}
	return result;
}
}

TEST (flamegraph, large_direct_processing)
{
	auto reps = rep_set (4);
	auto circulating = 10 * celerix::Kcelerix_ratio;
	celerix::test::system system;
	system.ledger_initialization_set (reps, circulating);
	auto & node = *system.add_node ();
	auto prepare = [&] () {
		celerix::state_block_builder builder;
		std::deque<std::shared_ptr<celerix::block>> blocks;
		std::deque<celerix::keypair> keys;
		auto previous = *std::prev (std::prev (system.initialization_blocks.end ()));
		for (auto i = 0; i < 20000; ++i)
		{
			keys.emplace_back ();
			auto const & key = keys.back ();
			auto block = builder.make_block ()
						 .account (celerix::dev::genesis_key.pub)
						 .representative (celerix::dev::genesis_key.pub)
						 .previous (previous->hash ())
						 .link (key.pub)
						 .balance (previous->balance_field ().value ().number () - 1000 * celerix::raw_ratio)
						 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
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
			ASSERT_EQ (celerix::block_status::progress, node.process (block));
		}
	};
	execute ();
}

TEST (flamegraph, large_confirmation)
{
	auto reps = rep_set (4);
	auto circulating = 10 * celerix::Kcelerix_ratio;
	celerix::test::system system;
	system.ledger_initialization_set (reps, circulating);
	auto prepare = [&] () {
		celerix::state_block_builder builder;
		std::deque<std::shared_ptr<celerix::block>> blocks;
		std::deque<celerix::keypair> keys;
		auto previous = *std::prev (std::prev (system.initialization_blocks.end ()));
		for (auto i = 0; i < 100; ++i)
		{
			keys.emplace_back ();
			auto const & key = keys.back ();
			auto block = builder.make_block ()
						 .account (celerix::dev::genesis_key.pub)
						 .representative (celerix::dev::genesis_key.pub)
						 .previous (previous->hash ())
						 .link (key.pub)
						 .balance (previous->balance_field ().value ().number () - 1000 * celerix::raw_ratio)
						 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						 .work (*system.work.generate (previous->hash ()))
						 .build ();
			blocks.push_back (block);
			previous = block;
		}
		return std::make_tuple (blocks, keys);
	};
	auto const & [blocks, keys] = prepare ();
	system.initialization_blocks.insert (system.initialization_blocks.end (), blocks.begin (), blocks.end ());
	celerix::node_config config;
	celerix::node_flags flags;
	auto & node1 = *system.add_node (config, flags, celerix::transport::transport_type::tcp, reps[0]);
	auto & node2 = *system.add_node (config, flags, celerix::transport::transport_type::tcp, reps[1]);
	auto & node3 = *system.add_node (config, flags, celerix::transport::transport_type::tcp, reps[2]);
	auto & node4 = *system.add_node (config, flags, celerix::transport::transport_type::tcp, reps[3]);
	ASSERT_TIMELY (300s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (auto const & node) {
		return node->block_confirmed (system.initialization_blocks.back ()->hash ());
	}));
}
