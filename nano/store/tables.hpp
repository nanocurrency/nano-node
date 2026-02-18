#pragma once

#include <functional>

namespace nano::store
{
// Keep this in alphabetical order
enum class table
{
	accounts,
	block_data,
	block_index,
	blocks, // (v25 and earlier, kept for migration)
	confirmation_height,
	default_unused, // RocksDB only
	final_votes,
	meta,
	online_weight,
	peers,
	pending,
	pruned,
	successor,
	vote,
	rep_weights,
	unchecked, // dropped in v22
	frontiers, // dropped in v24
};
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
