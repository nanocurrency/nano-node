#pragma once

#include <functional>
#include <string_view>

namespace nano::store
{
// Keep this in alphabetical order
enum class table
{
	account_block_by_height,
	account_delegator_by_weight,
	account_receivable_by_amount,
	accounts,
	blocks,
	confirmation_height,
	default_unused, // RocksDB only
	final_votes,
	meta,
	online_weight,
	peers,
	pending,
	pruned,
	receive_block_by_send_block,
	successor,
	topology,
	vote,
	rep_weights,
	unchecked, // dropped in v22
	frontiers, // dropped in v24
};

std::string_view to_string (table);
}

namespace std
{
template <>
struct hash<::nano::store::table>
{
	size_t operator() (::nano::store::table const & table) const
	{
		return static_cast<size_t> (table);
	}
}; // struct hash
}
