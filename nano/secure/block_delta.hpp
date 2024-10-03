#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/pending_info.hpp>

#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace nano
{
class block;
}

namespace nano
{
class block_delta
{
public:
	// The block associated with this delta
	std::shared_ptr<nano::block> block;

	// The updated account information after applying this block
	nano::account_info head;

	// Pair representing changes in receivable (pending) funds
	// First optional: If present, contains the key of the receivable to be added or removed
	// Second optional: If present, contains the info of the receivable to be added
	// Both empty: No change in receivables
	// First present, second empty: Remove the receivable
	// Both present: Add the receivable
	std::pair<std::optional<nano::pending_key>, std::optional<nano::pending_info>> receivable;

	// Pair representing changes in voting weight
	// First optional: If present, contains the account whose weight is changing
	// Second optional: If present, contains the amount of weight change
	// Both empty: No change in voting weight
	// Both present: Update the voting weight for the specified account
	std::pair<std::optional<nano::account>, std::optional<nano::amount>> weight;
};
}
