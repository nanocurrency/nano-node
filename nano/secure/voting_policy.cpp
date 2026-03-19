#include <nano/lib/blocks.hpp>
#include <nano/lib/vote.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_cemented.hpp>
#include <nano/secure/voting_policy.hpp>
#include <nano/store/ledger/final_vote.hpp>

/*
 * vote_permit
 */

nano::vote_permit::vote_permit (nano::qualified_root qualified_root, nano::block_hash hash, nano::vote_type type) :
	qualified_root_m{ qualified_root },
	hash_m{ hash },
	type_m{ type }
{
}

nano::qualified_root const & nano::vote_permit::qualified_root () const
{
	return qualified_root_m;
}

nano::root nano::vote_permit::root () const
{
	return qualified_root_m.root ();
}

nano::block_hash const & nano::vote_permit::hash () const
{
	return hash_m;
}

nano::vote_type nano::vote_permit::type () const
{
	return type_m;
}

/*
 * voting_policy
 */

nano::voting_policy::voting_policy (nano::ledger & ledger) :
	ledger{ ledger }
{
}

std::optional<nano::vote_permit> nano::voting_policy::vote_normal (nano::secure::transaction const & txn, nano::block const & block) const
{
	if (ledger.dependencies_cemented (txn, block))
	{
		// If a final vote was already committed for a different block at this root, refuse to vote
		if (auto final_hash = ledger.store.final_vote.get (txn, block.qualified_root ()))
		{
			if (*final_hash != block.hash ())
			{
				return std::nullopt;
			}
		}
		return nano::vote_permit{ block.qualified_root (), block.hash (), vote_type::normal };
	}
	return std::nullopt;
}

std::optional<nano::vote_permit> nano::voting_policy::vote_final (nano::secure::write_transaction const & txn, nano::block const & block) const
{
	if (ledger.dependencies_cemented (txn, block))
	{
		// This checks for existing final vote and only inserts if no record exists, or if the same hash is already recorded for this root
		if (ledger.store.final_vote.put (txn, block.qualified_root (), block.hash ()))
		{
			return nano::vote_permit{ block.qualified_root (), block.hash (), vote_type::final };
		}
	}
	return std::nullopt;
}

std::optional<nano::vote_permit> nano::voting_policy::reply_final (nano::secure::transaction const & txn, nano::block const & block) const
{
	// If a final vote was already committed for this root, allow voting for the recorded hash
	if (auto final_hash = ledger.store.final_vote.get (txn, block.qualified_root ()))
	{
		return nano::vote_permit{ block.qualified_root (), *final_hash, vote_type::final };
	}
	// If no final vote recorded but block is already cemented, allow voting for this hash
	if (ledger.cemented.block_exists (txn, block.hash ()))
	{
		return nano::vote_permit{ block.qualified_root (), block.hash (), vote_type::final };
	}
	return std::nullopt;
}

std::vector<std::shared_ptr<nano::vote>> nano::voting_policy::sign (nano::vote_type type, std::vector<nano::vote_permit> const & permits, vote_signer_t const & signer, nano::millis_t timestamp) const
{
	if (permits.empty ())
	{
		return {};
	}

	release_assert (std::all_of (permits.begin (), permits.end (), [type] (auto const & p) { return p.type () == type; }), "vote permit type mismatch");

	std::vector<nano::block_hash> hashes;
	hashes.reserve (permits.size ());
	for (auto const & permit : permits)
	{
		hashes.push_back (permit.hash ());
	}

	nano::millis_t const effective_timestamp = (type == vote_type::final) ? nano::vote::timestamp_max : timestamp;
	uint8_t const effective_duration = (type == vote_type::final) ? nano::vote::duration_max : /*8192ms*/ 0x9;

	std::vector<std::shared_ptr<nano::vote>> votes;
	signer ([&] (nano::public_key const & pub, nano::raw_key const & prv) {
		votes.emplace_back (std::make_shared<nano::vote> (pub, prv, effective_timestamp, effective_duration, hashes));
	});
	return votes;
}
