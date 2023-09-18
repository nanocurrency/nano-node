#include <nano/lib/timer.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>
#include <nano/store/frontier.hpp>

// clang-format off
nano::store::component::component (
	nano::block_store & block_store_a,
	nano::frontier_store & frontier_store_a,
	nano::account_store & account_store_a,
	nano::pending_store & pending_store_a,
	nano::online_weight_store & online_weight_store_a,
	nano::pruned_store & pruned_store_a,
	nano::peer_store & peer_store_a,
	nano::confirmation_height_store & confirmation_height_store_a,
	nano::final_vote_store & final_vote_store_a,
	nano::version_store & version_store_a
) :
	block (block_store_a),
	frontier (frontier_store_a),
	account (account_store_a),
	pending (pending_store_a),
	online_weight (online_weight_store_a),
	pruned (pruned_store_a),
	peer (peer_store_a),
	confirmation_height (confirmation_height_store_a),
	final_vote (final_vote_store_a),
	version (version_store_a)
{
}
// clang-format on

/**
 * If using a different store version than the latest then you may need
 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
 */
void nano::store::component::initialize (nano::write_transaction const & transaction_a, nano::ledger_cache & ledger_cache_a, nano::ledger_constants & constants)
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
