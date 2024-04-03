#pragma once

#include <functional>

namespace nano
{
// Keep this in alphabetical order
enum class tables
{
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
	vote,
	rep_weights,
};
} // namespace nano

namespace std
{
template <>
struct hash<::nano::tables>
{
	size_t operator() (::nano::tables const & table_a) const
	{
		return static_cast<size_t> (table_a);
	}
}; // struct hash
} // namespace std
