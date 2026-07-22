#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>

#include <compare>
#include <iosfwd>

namespace nano
{
/**
 * Information on an uncollected send
 * This class captures the data stored in a pending table entry
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (nano::account const &, nano::amount const &, nano::epoch);

	// Size of the serialized representation in the database
	size_t db_size () const;

	// Deserialize from stream, returns true on error
	bool deserialize (nano::stream &);

	bool operator== (nano::pending_info const &) const = default;

	friend std::ostream & operator<< (std::ostream &, nano::pending_info const &);

public:
	nano::account source{}; // Account sending the funds
	nano::amount amount{ 0 }; // Amount receivable in this transaction
	nano::epoch epoch{ nano::epoch::epoch_0 }; // Epoch of the send block, stored here to allow pruning the send block
};

/**
 * Represents the key of the pending (receivable) database table
 * The receiving account and hash of the send block identify a pending table entry
 */
class pending_key final
{
public:
	pending_key () = default;
	pending_key (nano::account const &, nano::block_hash const &);

	// The receiving account, which groups the entries when scanning the table
	nano::account const & key () const;

	// Deserialize from stream, returns true on error
	bool deserialize (nano::stream &);

	// Orders by account, then hash, matching the database byte order; also provides a defaulted operator==
	std::strong_ordering operator<=> (nano::pending_key const &) const = default;

	friend std::ostream & operator<< (std::ostream &, nano::pending_key const &);

public:
	nano::account account{}; // Receiving account
	nano::block_hash hash{ 0 }; // Hash of the send block
};
}

namespace std
{
template <>
struct hash<::nano::pending_key>
{
	size_t operator() (::nano::pending_key const & value) const
	{
		return hash<::nano::uint512_union>{}({ value.account, value.hash });
	}
};
}
