#include <nano/lib/stream.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/pending_info.hpp>

#include <ostream>

nano::pending_info::pending_info (nano::account const & source_a, nano::amount const & amount_a, nano::epoch epoch_a) :
	source (source_a),
	amount (amount_a),
	epoch (epoch_a)
{
}

bool nano::pending_info::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, source.bytes);
		nano::read (stream_a, amount.bytes);
		nano::read (stream_a, epoch);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

size_t nano::pending_info::db_size () const
{
	return sizeof (source) + sizeof (amount) + sizeof (epoch);
}

nano::pending_key::pending_key (nano::account const & account_a, nano::block_hash const & hash_a) :
	account (account_a),
	hash (hash_a)
{
}

bool nano::pending_key::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		nano::read (stream_a, account.bytes);
		nano::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

nano::account const & nano::pending_key::key () const
{
	return account;
}

namespace nano
{
std::ostream & operator<< (std::ostream & os, nano::pending_info const & info)
{
	int const epoch = nano::normalized_epoch (info.epoch);
	os << "Source: " << info.source << ", Amount: " << info.amount.to_string_dec () << " Epoch: " << epoch;
	return os;
}

std::ostream & operator<< (std::ostream & os, nano::pending_key const & key)
{
	os << "Account: " << key.account << ", Hash: " << key.hash;
	return os;
}
}
