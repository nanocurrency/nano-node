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

protected:
	nano::ledger_constants & constants;
};
}
