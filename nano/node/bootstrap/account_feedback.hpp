#pragma once

#include <nano/node/block_source.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/secure/fwd.hpp>

namespace nano::bootstrap
{
// How a processed block reached the node, as far as the account sets are concerned
enum class block_origin
{
	live, // On its own, not as part of syncing its account
	bootstrap, // Pulled by bootstrap while syncing its account
	other,
};

// What a processed block means for the account sets
enum class account_feedback
{
	none,
	progressed, // The account made progress, so more of it is worth pulling
	blocked, // The account cannot progress until a missing source block arrives
	behind, // The network is ahead of the ledger on this account, so it is worth pulling
};

// Decides how a block reached the node, given its block source and the strategy that submitted it
block_origin classify_origin (nano::block_source, nano::bootstrap::strategy submitter);

// Decides what the result of a processed block means for the account sets, given where the block came from and which strategy submitted it
account_feedback classify_feedback (nano::block_status, nano::block_source, nano::bootstrap::strategy submitter);
}
