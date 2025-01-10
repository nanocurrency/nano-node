#include <celerix/lib/stream.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/pending_info.hpp>

celerix::pending_info::pending_info (celerix::account const & source_a, celerix::amount const & amount_a, celerix::epoch epoch_a) :
	source (source_a),
	amount (amount_a),
	epoch (epoch_a)
{
}

bool celerix::pending_info::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		celerix::read (stream_a, source.bytes);
		celerix::read (stream_a, amount.bytes);
		celerix::read (stream_a, epoch);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

size_t celerix::pending_info::db_size () const
{
	return sizeof (source) + sizeof (amount) + sizeof (epoch);
}

bool celerix::pending_info::operator== (celerix::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
}

celerix::pending_key::pending_key (celerix::account const & account_a, celerix::block_hash const & hash_a) :
	account (account_a),
	hash (hash_a)
{
}

bool celerix::pending_key::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		celerix::read (stream_a, account.bytes);
		celerix::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool celerix::pending_key::operator== (celerix::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

celerix::account const & celerix::pending_key::key () const
{
	return account;
}

bool celerix::pending_key::operator< (celerix::pending_key const & other_a) const
{
	return account == other_a.account ? hash < other_a.hash : account < other_a.account;
}
