#include <nano/lib/blocks.hpp>
#include <nano/node/bootstrap/verify.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <set>

namespace nano::bootstrap
{
verify_result verify (nano::messages::asc_pull_ack::blocks_payload const & response, blocks_query const & query)
{
	auto const & blocks = response.blocks;

	if (blocks.empty ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () == 1 && blocks.front ()->hash () == query.start.as_block_hash ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () > query.count)
	{
		return verify_result::invalid;
	}

	auto const & first = blocks.front ();
	switch (query.type)
	{
		case query_type::blocks_by_hash:
		{
			if (first->hash () != query.start.as_block_hash ())
			{
				return verify_result::invalid;
			}
		}
		break;
		case query_type::blocks_by_account:
		{
			// Open & state blocks always contain account field
			if (first->account_field ().value_or (0) != query.start.as_account ())
			{
				return verify_result::invalid;
			}
		}
		break;
		default:
			return verify_result::invalid;
	}

	// Verify blocks make a valid chain
	nano::block_hash previous_hash = blocks.front ()->hash ();
	for (std::size_t n = 1; n < blocks.size (); ++n)
	{
		auto & block = blocks[n];
		if (block->previous () != previous_hash)
		{
			return verify_result::invalid; // Blocks do not make a chain
		}
		previous_hash = block->hash ();
	}

	return verify_result::ok;
}

verify_result verify (nano::messages::asc_pull_ack::frontiers_payload const & response, frontiers_query const & query)
{
	auto const & frontiers = response.frontiers;

	if (frontiers.empty ())
	{
		return verify_result::nothing_new;
	}

	// Ensure frontiers accounts are in ascending order
	nano::account previous{ 0 };
	for (auto const & [account, _] : frontiers)
	{
		if (account.number () <= previous.number ())
		{
			return verify_result::invalid;
		}
		previous = account;
	}

	// Ensure the frontiers are larger or equal to the requested frontier
	if (frontiers.front ().first.number () < query.start.number ())
	{
		return verify_result::invalid;
	}

	return verify_result::ok;
}

verify_result verify (nano::messages::asc_pull_ack::topo_index_payload const & response, topo_index_query const & query)
{
	auto const & entries = response.entries;

	if (entries.empty ())
	{
		return verify_result::nothing_new;
	}
	if (entries.size () > query.count)
	{
		return verify_result::invalid;
	}

	// The server returns a contiguous ascending page starting at the first key >= start
	if (entries.front () < query.start)
	{
		return verify_result::invalid;
	}

	// Topo keys are unique and must be strictly ascending. Heights are densely packed (a block at height h
	// depends on one at h-1), so a contiguous page never skips a height: adjacent entries step up by at most one.
	for (size_t n = 1; n < entries.size (); ++n)
	{
		if (!(entries[n - 1] < entries[n]))
		{
			return verify_result::invalid;
		}
		if (entries[n].topo_height - entries[n - 1].topo_height > 1)
		{
			return verify_result::invalid;
		}
	}

	return verify_result::ok;
}

verify_result verify (nano::messages::asc_pull_ack::blocks_payload const & response, blocks_random_query const & query)
{
	auto const & blocks = response.blocks;

	if (blocks.empty ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () > query.hashes.size ())
	{
		return verify_result::invalid;
	}

	// Every returned block must have been requested; no duplicates (random fetch has no chain ordering)
	std::set<nano::block_hash> requested{ query.hashes.begin (), query.hashes.end () };
	std::set<nano::block_hash> seen;
	for (auto const & block : blocks)
	{
		auto const hash = block->hash ();
		if (requested.find (hash) == requested.end ())
		{
			return verify_result::invalid;
		}
		if (!seen.insert (hash).second)
		{
			return verify_result::invalid;
		}
	}

	return verify_result::ok;
}
}
