#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/timer.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>
#include <nano/store/final_vote.hpp>
#include <nano/store/pending.hpp>
#include <nano/store/pruned.hpp>
#include <nano/store/rep_weight.hpp>

nano::store::component::component (nano::store::block & block_store_a, nano::store::account & account_store_a, nano::store::pending & pending_store_a, nano::store::online_weight & online_weight_store_a, nano::store::pruned & pruned_store_a, nano::store::peer & peer_store_a, nano::store::confirmation_height & confirmation_height_store_a, nano::store::final_vote & final_vote_store_a, nano::store::version & version_store_a, nano::store::rep_weight & rep_weight_a) :
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
void nano::store::component::initialize (store::write_transaction const & transaction, nano::ledger_constants & constants)
{
	release_assert (constants.genesis->has_sideband ());
	release_assert (account.begin (transaction) == account.end (transaction));
	release_assert (block.begin (transaction) == block.end (transaction));
	release_assert (pending.begin (transaction) == pending.end (transaction));
	release_assert (confirmation_height.begin (transaction) == confirmation_height.end (transaction));
	release_assert (final_vote.begin (transaction) == final_vote.end (transaction));
	release_assert (rep_weight.begin (transaction) == rep_weight.end (transaction));
	release_assert (pruned.begin (transaction) == pruned.end (transaction));

	block.put (transaction, constants.genesis->hash (), *constants.genesis);
	confirmation_height.put (transaction, constants.genesis->account (), nano::confirmation_height_info{ 1, constants.genesis->hash () });
	account.put (transaction, constants.genesis->account (), { constants.genesis->hash (), constants.genesis->account (), constants.genesis->hash (), std::numeric_limits<nano::uint128_t>::max (), nano::seconds_since_epoch (), 1, nano::epoch::epoch_0 });
	rep_weight.put (transaction, constants.genesis->account (), std::numeric_limits<nano::uint128_t>::max ());
}

/*
 *
 */

std::string_view nano::store::to_string (open_mode mode)
{
	return nano::enum_util::name (mode);
}