#include <celerix/lib/blocks.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/secure/ledger_cache.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/block.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/confirmation_height.hpp>
#include <celerix/store/rep_weight.hpp>

celerix::store::component::component (celerix::store::block & block_store_a, celerix::store::account & account_store_a, celerix::store::pending & pending_store_a, celerix::store::online_weight & online_weight_store_a, celerix::store::pruned & pruned_store_a, celerix::store::peer & peer_store_a, celerix::store::confirmation_height & confirmation_height_store_a, celerix::store::final_vote & final_vote_store_a, celerix::store::version & version_store_a, celerix::store::rep_weight & rep_weight_a) :
	block (block_store_a),
	account (account_store_a),
	pending (pending_store_a),
	online_weight (online_weight_store_a),
	pruned (pruned_store_a),
	peer (peer_store_a),
	confirmation_height (confirmation_height_store_a),
	final_vote (final_vote_store_a),
	version (version_store_a),
	rep_weight (rep_weight_a)
{
}

/**
 * If using a different store version than the latest then you may need
 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
 */
void celerix::store::component::initialize (store::write_transaction const & transaction_a, celerix::ledger_cache & ledger_cache_a, celerix::ledger_constants & constants)
{
	debug_assert (constants.genesis->has_sideband ());
	debug_assert (account.begin (transaction_a) == account.end (transaction_a));
	auto hash_l (constants.genesis->hash ());
	block.put (transaction_a, hash_l, *constants.genesis);
	++ledger_cache_a.block_count;
	confirmation_height.put (transaction_a, constants.genesis->account (), celerix::confirmation_height_info{ 1, constants.genesis->hash () });
	++ledger_cache_a.cemented_count;
	account.put (transaction_a, constants.genesis->account (), { hash_l, constants.genesis->account (), constants.genesis->hash (), std::numeric_limits<celerix::uint128_t>::max (), celerix::seconds_since_epoch (), 1, celerix::epoch::epoch_0 });
	++ledger_cache_a.account_count;
	rep_weight.put (transaction_a, constants.genesis->account (), std::numeric_limits<celerix::uint128_t>::max ());
	ledger_cache_a.rep_weights.representation_put (constants.genesis->account (), std::numeric_limits<celerix::uint128_t>::max ());
}
