#pragma once

#include <functional>

namespace celerix
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
} // namespace celerix

namespace std
{
template <>
struct hash<::celerix::tables>
{
	size_t operator() (::celerix::tables const & table_a) const
	{
		return static_cast<size_t> (table_a);
	}
}; // struct hash
} // namespace std
