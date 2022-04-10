#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/rep_weights.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/secure/buffer.hpp>
#include <nano/secure/store.hpp>

#include <crypto/cryptopp/words.h>

#include <thread>

namespace nano
{
/** This base class implements the store interface functions which have DB agnostic functionality. It also maps all the store classes. */
template <typename Val, typename Derived_Store>
class store_partial : public store
{
public:
	// clang-format off
	store_partial (
		nano::ledger_constants & constants,
		nano::block_store & block_store_a,
		nano::frontier_store & frontier_store_a,
		nano::account_store & account_store_a,
		nano::pending_store & pending_store_a,
		nano::unchecked_store & unchecked_store_a,
		nano::online_weight_store & online_weight_store_a,
		nano::pruned_store & pruned_store_a,
		nano::peer_store & peer_store_a,
		nano::confirmation_height_store & confirmation_height_store_a,
		nano::final_vote_store & final_vote_store_a,
		nano::version_store & version_store_a) :
		constants{ constants },
		store{
			block_store_a,
			frontier_store_a,
			account_store_a,
			pending_store_a,
			unchecked_store_a,
			online_weight_store_a,
			pruned_store_a,
			peer_store_a,
			confirmation_height_store_a,
			final_vote_store_a,
			version_store_a
		}
	{}
	// clang-format on

	/**
	 * If using a different store version than the latest then you may need
	 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
	 */
	void initialize (nano::write_transaction const & transaction_a, nano::ledger_cache & ledger_cache_a) override
	{
		debug_assert (constants.genesis->has_sideband ());
		debug_assert (account.begin (transaction_a) == account.end ());
		auto hash_l (constants.genesis->hash ());
		block.put (transaction_a, hash_l, *constants.genesis);
		++ledger_cache_a.block_count;
		confirmation_height.put (transaction_a, constants.genesis->account (), nano::confirmation_height_info{ 1, constants.genesis->hash () });
		++ledger_cache_a.cemented_count;
		ledger_cache_a.final_votes_confirmation_canary = (constants.final_votes_canary_account == constants.genesis->account () && 1 >= constants.final_votes_canary_height);
		account.put (transaction_a, constants.genesis->account (), { hash_l, constants.genesis->account (), constants.genesis->hash (), std::numeric_limits<nano::uint128_t>::max (), nano::seconds_since_epoch (), 1, nano::epoch::epoch_0 });
		++ledger_cache_a.account_count;
		ledger_cache_a.rep_weights.representation_put (constants.genesis->account (), std::numeric_limits<nano::uint128_t>::max ());
		frontier.put (transaction_a, hash_l, constants.genesis->account ());
	}

protected:
	nano::ledger_constants & constants;

	uint64_t count (nano::transaction const & transaction_a, std::initializer_list<tables> dbs_a) const
	{
		uint64_t total_count = 0;
		for (auto db : dbs_a)
		{
			total_count += count (transaction_a, db);
		}
		return total_count;
	}

	int put (nano::write_transaction const & transaction_a, tables table_a, nano::db_val<Val> const & key_a, nano::db_val<Val> const & value_a)
	{
		return static_cast<Derived_Store &> (*this).put (transaction_a, table_a, key_a, value_a);
	}

	virtual uint64_t count (nano::transaction const & transaction_a, tables table_a) const = 0;
	virtual int drop (nano::write_transaction const & transaction_a, tables table_a) = 0;
	virtual bool not_found (int status) const = 0;
	virtual bool success (int status) const = 0;
	virtual int status_code_not_found () const = 0;
	virtual std::string error_string (int status) const = 0;
};
}
