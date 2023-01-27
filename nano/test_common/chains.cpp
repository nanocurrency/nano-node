#include <nano/test_common/chains.hpp>

using namespace std::chrono_literals;

nano::block_list_t nano::test::setup_chain (nano::test::system & system, nano::node & node, int count, nano::keypair target)
{
	auto latest = node.latest (target.pub);
	auto balance = node.balance (target.pub);

	std::vector<std::shared_ptr<nano::block>> blocks;
	for (int n = 0; n < count; ++n)
	{
		nano::keypair throwaway;
		nano::block_builder builder;

		balance -= 1;
		auto send = builder
					.send ()
					.previous (latest)
					.destination (throwaway.pub)
					.balance (balance)
					.sign (target.prv, target.pub)
					.work (*system.work.generate (latest))
					.build_shared ();

		latest = send->hash ();

		blocks.push_back (send);
	}

	EXPECT_TRUE (nano::test::process (node, blocks));

	// Confirm whole chain at once
	EXPECT_TIMELY (5s, nano::test::confirm (node, { blocks.back () }));
	EXPECT_TIMELY (5s, nano::test::confirmed (node, blocks));

	return blocks;
}

std::vector<std::pair<nano::account, nano::block_list_t>> nano::test::setup_chains (nano::test::system & system, nano::node & node, int chain_count, int block_count, nano::keypair source)
{
	auto latest = node.latest (source.pub);
	auto balance = node.balance (source.pub);

	std::vector<std::pair<nano::account, block_list_t>> chains;
	for (int n = 0; n < chain_count; ++n)
	{
		nano::keypair key;
		nano::block_builder builder;

		balance -= block_count * 2; // Send enough to later create `block_count` blocks
		auto send = builder
					.send ()
					.previous (latest)
					.destination (key.pub)
					.balance (balance)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();

		auto open = builder
					.open ()
					.source (send->hash ())
					.representative (key.pub)
					.account (key.pub)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build_shared ();

		latest = send->hash ();

		// Ensure blocks are in the ledger and confirmed
		EXPECT_TRUE (nano::test::process (node, { send, open }));
		EXPECT_TIMELY (5s, nano::test::confirm (node, { send, open }));
		EXPECT_TIMELY (5s, nano::test::confirmed (node, { send, open }));

		auto added_blocks = nano::test::setup_chain (system, node, block_count, key);

		auto blocks = block_list_t{ open };
		blocks.insert (blocks.end (), added_blocks.begin (), added_blocks.end ());

		chains.emplace_back (key.pub, blocks);
	}

	return chains;
}

nano::block_list_t nano::test::setup_independent_blocks (nano::test::system & system, nano::node & node, int count, nano::keypair source)
{
	std::vector<std::shared_ptr<nano::block>> blocks;

	auto latest = node.latest (source.pub);
	auto balance = node.balance (source.pub);

	for (int n = 0; n < count; ++n)
	{
		nano::keypair key;
		nano::block_builder builder;

		balance -= 1;
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();

		latest = send->hash ();

		auto open = builder
					.state ()
					.account (key.pub)
					.previous (0)
					.representative (key.pub)
					.balance (1)
					.link (send->hash ())
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build_shared ();

		EXPECT_TRUE (nano::test::process (node, { send, open }));
		EXPECT_TIMELY (5s, nano::test::exists (node, { send, open })); // Ensure blocks are in the ledger

		blocks.push_back (open);
	}

	// Confirm whole genesis chain at once
	EXPECT_TIMELY (5s, nano::test::confirm (node, { latest }));
	EXPECT_TIMELY (5s, nano::test::confirmed (node, { latest }));

	return blocks;
}

nano::keypair nano::test::setup_rep (nano::test::system & system, nano::node & node, nano::uint128_t const amount, nano::keypair source)
{
	auto latest = node.latest (source.pub);
	auto balance = node.balance (source.pub);

	nano::keypair key;
	nano::block_builder builder;

	auto send = builder
				.send ()
				.previous (latest)
				.destination (key.pub)
				.balance (balance - amount)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build_shared ();

	auto open = builder
				.open ()
				.source (send->hash ())
				.representative (key.pub)
				.account (key.pub)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();

	EXPECT_TRUE (nano::test::process (node, { send, open }));
	EXPECT_TIMELY (5s, nano::test::confirm (node, { send, open }));
	EXPECT_TIMELY (5s, nano::test::confirmed (node, { send, open }));

	return key;
}