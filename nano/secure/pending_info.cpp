#include <nano/secure/ledger.hpp>
#include <nano/secure/pending_info.hpp>

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

bool nano::pending_info::operator== (nano::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
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

bool nano::pending_key::operator== (nano::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

nano::account const & nano::pending_key::key () const
{
	return account;
}

bool nano::pending_key::operator< (nano::pending_key const & other_a) const
{
	return account == other_a.account ? hash < other_a.hash : account < other_a.account;
}

nano::receivable_iterator::receivable_iterator (nano::ledger const & ledger, nano::secure::transaction const & tx, std::optional<std::pair<nano::pending_key, nano::pending_info>> item) :
	ledger{ &ledger },
	tx{ &tx },
	item{ item }
{
	if (item.has_value ())
	{
		account = item.value ().first.account;
	}
}

bool nano::receivable_iterator::operator== (receivable_iterator const & other) const
{
	debug_assert (ledger == nullptr || other.ledger == nullptr || ledger == other.ledger);
	debug_assert (tx == nullptr || other.tx == nullptr || tx == other.tx);
	debug_assert (account.is_zero () || other.account.is_zero () || account == other.account);
	return item == other.item;
}

bool nano::receivable_iterator::operator!= (receivable_iterator const & other) const
{
	return !(*this == other);
}

nano::receivable_iterator & nano::receivable_iterator::operator++ ()
{
	item = ledger->receivable_lower_bound (*tx, item.value ().first.account, item.value ().first.hash.number () + 1);
	if (item && item.value ().first.account != account)
	{
		item = std::nullopt;
	}
	return *this;
}

std::pair<nano::pending_key, nano::pending_info> const & nano::receivable_iterator::operator* () const
{
	return item.value ();
}

std::pair<nano::pending_key, nano::pending_info> const * nano::receivable_iterator::operator->() const
{
	return &item.value ();
}
