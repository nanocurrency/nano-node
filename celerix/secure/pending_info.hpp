#pragma once

#include <celerix/lib/epoch.hpp>
#include <celerix/lib/fwd.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/secure/fwd.hpp>

namespace celerix
{
/**
 * Information on an uncollected send
 * This class captures the data stored in a pending table entry
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (celerix::account const &, celerix::amount const &, celerix::epoch);
	size_t db_size () const;
	bool deserialize (celerix::stream &);
	bool operator== (celerix::pending_info const &) const;
	celerix::account source{}; // the account sending the funds
	celerix::amount amount{ 0 }; // amount receivable in this transaction
	celerix::epoch epoch{ celerix::epoch::epoch_0 }; // epoch of sending block, this info is stored here to make it possible to prune the send block

	friend std::ostream & operator<< (std::ostream & os, const celerix::pending_info & info)
	{
		const int epoch = celerix::normalized_epoch (info.epoch);
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
	pending_key (celerix::account const &, celerix::block_hash const &);
	bool deserialize (celerix::stream &);
	bool operator== (celerix::pending_key const &) const;
	bool operator< (celerix::pending_key const &) const;
	celerix::account const & key () const;
	celerix::account account{}; // receiving account
	celerix::block_hash hash{ 0 }; // hash of the send block

	friend std::ostream & operator<< (std::ostream & os, const celerix::pending_key & key)
	{
		os << "Account: " << key.account << ", Hash: " << key.hash;
		return os;
	}
};
}

namespace std
{
template <>
struct hash<::celerix::pending_key>
{
	size_t operator() (::celerix::pending_key const & value) const
	{
		return hash<::celerix::uint512_union>{}({ ::celerix::uint256_union{ value.account.number () }, value.hash });
	}
};
}
