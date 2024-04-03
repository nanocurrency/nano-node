#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>

namespace nano
{
class ledger;
}

namespace nano::store
{
class transaction;
}

namespace nano
{
/**
 * Information on an uncollected send
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (nano::account const &, nano::amount const &, nano::epoch);
	size_t db_size () const;
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_info const &) const;
	nano::account source{};
	nano::amount amount{ 0 };
	nano::epoch epoch{ nano::epoch::epoch_0 };
};
class pending_key final
{
public:
	pending_key () = default;
	pending_key (nano::account const &, nano::block_hash const &);
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_key const &) const;
	bool operator< (nano::pending_key const &) const;
	nano::account const & key () const;
	nano::account account{};
	nano::block_hash hash{ 0 };
};
// This class iterates receivable enttries for an account
class receivable_iterator
{
public:
	receivable_iterator () = default;
	receivable_iterator (nano::ledger const & ledger, nano::store::transaction const & tx, std::optional<std::pair<nano::pending_key, nano::pending_info>> item);
	bool operator== (receivable_iterator const & other) const;
	bool operator!= (receivable_iterator const & other) const;
	// Advances to the next receivable entry for the same account
	receivable_iterator & operator++ ();
	std::pair<nano::pending_key, nano::pending_info> const & operator* () const;
	std::pair<nano::pending_key, nano::pending_info> const * operator->() const;

private:
	nano::ledger const * ledger{ nullptr };
	nano::store::transaction const * tx{ nullptr };
	nano::account account{ 0 };
	std::optional<std::pair<nano::pending_key, nano::pending_info>> item;
};
} // namespace nano

namespace std
{
template <>
struct hash<::nano::pending_key>
{
	size_t operator() (::nano::pending_key const & data_a) const
	{
		return hash<::nano::uint512_union>{}({ ::nano::uint256_union{ data_a.account.number () }, data_a.hash });
	}
};
}
