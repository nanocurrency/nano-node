#include <nano/lib/blocks.hpp>
#include <nano/lib/timer.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>
#include <nano/store/frontier.hpp>

nano::store::component::component (nano::store::block & block_store_a, nano::store::frontier & frontier_store_a, nano::store::account & account_store_a, nano::store::pending & pending_store_a, nano::store::online_weight & online_weight_store_a, nano::store::pruned & pruned_store_a, nano::store::peer & peer_store_a, nano::store::confirmation_height & confirmation_height_store_a, nano::store::final_vote & final_vote_store_a, nano::store::version & version_store_a) :
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

/**
 * If using a different store version than the latest then you may need
 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
 */
void nano::store::component::initialize (store::write_transaction const & transaction_a, nano::ledger_cache & ledger_cache_a, nano::ledger_constants & constants)
{
	debug_assert (constants.genesis->has_sideband ());
	debug_assert (account.begin (transaction_a) == account.end ());
	auto hash_l (constants.genesis->hash ());
	block.put (transaction_a, hash_l, *constants.genesis);
	++ledger_cache_a.block_count;
	confirmation_height.put (transaction_a, constants.genesis->account ().value (), nano::confirmation_height_info{ 1, constants.genesis->hash () });
	++ledger_cache_a.cemented_count;
	ledger_cache_a.final_votes_confirmation_canary = (constants.final_votes_canary_account == constants.genesis->account () && 1 >= constants.final_votes_canary_height);
	account.put (transaction_a, constants.genesis->account ().value (), { hash_l, constants.genesis->account ().value (), constants.genesis->hash (), std::numeric_limits<nano::uint128_t>::max (), nano::seconds_since_epoch (), 1, nano::epoch::epoch_0 });
	++ledger_cache_a.account_count;
	ledger_cache_a.rep_weights.representation_put (constants.genesis->account ().value (), std::numeric_limits<nano::uint128_t>::max ());
	frontier.put (transaction_a, hash_l, constants.genesis->account ().value ());
}
