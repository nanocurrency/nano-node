#pragma once

#include <nano/node/block_source.hpp>
#include <nano/secure/fwd.hpp>

namespace nano::bootstrap
{
// What a processed block means for the account sets
enum class account_feedback
{
	none,
	progressed, // The account made progress, so more of it is worth pulling
	blocked, // The account cannot progress until a missing source block arrives
	behind, // The network is ahead of the ledger on this account, so it is worth pulling
};

// Decides what the result of a processed block means for the account sets, given where the block came from
account_feedback classify_feedback (nano::block_status, nano::block_source);
}
