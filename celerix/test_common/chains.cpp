#include <celerix/lib/blocks.hpp>
#include <celerix/test_common/chains.hpp>

using namespace std::chrono_literals;

celerix::block_list_t celerix::test::setup_chain (celerix::test::system & system, celerix::node & node, int count, celerix::keypair target, bool confirm)
{
	auto latest = node.latest (target.pub);
	auto balance = node.balance (target.pub);

	std::vector<std::shared_ptr<celerix::block>> blocks;
	for (int n = 0; n < count; ++n)
	{
		celerix::keypair throwaway;
		celerix::block_builder builder;

		balance -= 1;
		auto send = builder
					.state ()
					.account (target.pub)
					.previous (latest)
					.representative (target.pub)
					.balance (balance)
					.link (throwaway.pub)
					.sign (target.prv, target.pub)
					.work (*system.work.generate (latest))
					.build ();

		latest = send->hash ();

		blocks.push_back (send);
	}

	EXPECT_TRUE (celerix::test::process (node, blocks));

	if (confirm)
	{
		// Confirm whole chain at once
		celerix::test::confirm (node.ledger, blocks);
	}

	return blocks;
}

std::vector<std::pair<celerix::account, celerix::block_list_t>> celerix::test::setup_chains (celerix::test::system & system, celerix::node & node, int chain_count, int block_count, celerix::keypair source, bool confirm)
{
	auto latest = node.latest (source.pub);
	auto balance = node.balance (source.pub);

	std::vector<std::pair<celerix::account, block_list_t>> chains;
	for (int n = 0; n < chain_count; ++n)
	{
		celerix::keypair key;
		celerix::block_builder builder;

		balance -= block_count * 2; // Send enough to later create `block_count` blocks
		auto send = builder
					.state ()
					.account (source.pub)
					.previous (latest)
					.representative (source.pub)
					.balance (balance)
					.link (key.pub)
					.sign (source.prv, source.pub)
					.work (*system.work.generate (latest))
					.build ();

		auto open = builder
					.state ()
					.account (key.pub)
					.previous (0)
					.representative (key.pub)
					.balance (block_count * 2)
					.link (send->hash ())
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build ();

		latest = send->hash ();

		EXPECT_TRUE (celerix::test::process (node, { send, open }));

		if (confirm)
		{
			// Ensure blocks are in the ledger and confirmed
			celerix::test::confirm (node.ledger, open);
		}

		auto added_blocks = celerix::test::setup_chain (system, node, block_count, key, confirm);

		auto blocks = block_list_t{ open };
		blocks.insert (blocks.end (), added_blocks.begin (), added_blocks.end ());

		chains.emplace_back (key.pub, blocks);
	}

	return chains;
}

celerix::block_list_t celerix::test::setup_independent_blocks (celerix::test::system & system, celerix::node & node, int count, celerix::keypair source)
{
	std::vector<std::shared_ptr<celerix::block>> blocks;

	auto latest = node.latest (source.pub);
	auto balance = node.balance (source.pub);

	for (int n = 0; n < count; ++n)
	{
		celerix::keypair key;
		celerix::block_builder builder;

		balance -= 1;
		auto send = builder
					.state ()
					.account (source.pub)
					.previous (latest)
					.representative (source.pub)
					.balance (balance)
					.link (key.pub)
					.sign (source.prv, source.pub)
					.work (*system.work.generate (latest))
					.build ();

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
					.build ();

		EXPECT_TRUE (celerix::test::process (node, { send, open }));
		EXPECT_TIMELY (5s, celerix::test::exists (node, { send, open })); // Ensure blocks are in the ledger

		blocks.push_back (open);
	}

	// Confirm whole genesis chain at once
	celerix::test::confirm (node.ledger, latest);

	return blocks;
}

std::pair<std::shared_ptr<celerix::block>, std::shared_ptr<celerix::block>> celerix::test::setup_new_account (celerix::test::system & system, celerix::node & node, celerix::uint128_t const amount, celerix::keypair source, celerix::keypair dest, celerix::account dest_rep, bool force_confirm)
{
	auto latest = node.latest (source.pub);
	auto balance = node.balance (source.pub);

	auto send = celerix::block_builder ()
				.state ()
				.account (source.pub)
				.previous (latest)
				.representative (source.pub)
				.balance (balance - amount)
				.link (dest.pub)
				.sign (source.prv, source.pub)
				.work (*system.work.generate (latest))
				.build ();

	auto open = celerix::block_builder ()
				.state ()
				.account (dest.pub)
				.previous (0)
				.representative (dest_rep)
				.balance (amount)
				.link (send->hash ())
				.sign (dest.prv, dest.pub)
				.work (*system.work.generate (dest.pub))
				.build ();

	EXPECT_TRUE (celerix::test::process (node, { send, open }));
	if (force_confirm)
	{
		celerix::test::confirm (node.ledger, open);
	}
	return std::make_pair (send, open);
}

celerix::keypair celerix::test::setup_rep (celerix::test::system & system, celerix::node & node, celerix::uint128_t const amount, celerix::keypair source)
{
	celerix::keypair destkey;
	celerix::test::setup_new_account (system, node, amount, source, destkey, destkey.pub, true);
	return destkey;
}
