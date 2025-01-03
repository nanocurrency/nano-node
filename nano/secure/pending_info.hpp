#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/secure/fwd.hpp>

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
	size_t db_size () const;
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_info const &) const;
	nano::account source{}; // the account sending the funds
	nano::amount amount{ 0 }; // amount receivable in this transaction
	nano::epoch epoch{ nano::epoch::epoch_0 }; // epoch of sending block, this info is stored here to make it possible to prune the send block

	friend std::ostream & operator<< (std::ostream & os, const nano::pending_info & info)
	{
		const int epoch = nano::normalized_epoch (info.epoch);
		os << "Source: " << info.source << ", Amount: " << info.amount.to_string_dec () << " Epoch: " << epoch;
		return os;
	}
};

// This class represents the data written into the pending (receivable) database table key
// the receiving account and hash of the send block identify a pending db table entry
class pending_key final
{
public:
	pending_key () = default;
	pending_key (nano::account const &, nano::block_hash const &);
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_key const &) const;
	bool operator< (nano::pending_key const &) const;
	nano::account const & key () const;
	nano::account account{}; // receiving account
	nano::block_hash hash{ 0 }; // hash of the send block

	friend std::ostream & operator<< (std::ostream & os, const nano::pending_key & key)
	{
		os << "Account: " << key.account << ", Hash: " << key.hash;
		return os;
	}
};
}

namespace std
{
template <>
struct hash<::nano::pending_key>
{
	size_t operator() (::nano::pending_key const & value) const
	{
		return hash<::nano::uint512_union>{}({ ::nano::uint256_union{ value.account.number () }, value.hash });
	}
};
}
