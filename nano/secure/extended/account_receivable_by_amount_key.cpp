#include <nano/lib/stream.hpp>
#include <nano/secure/extended/account_receivable_by_amount_key.hpp>

nano::account_receivable_by_amount_key::account_receivable_by_amount_key (nano::account const & account_a, nano::amount const & amount_a, nano::block_hash const & send_block_hash_a) :
	account{ account_a },
	amount{ amount_a },
	send_block_hash{ send_block_hash_a }
{
}

void nano::account_receivable_by_amount_key::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, account.bytes);
	nano::write (stream_a, amount.bytes);
	nano::write (stream_a, send_block_hash.bytes);
}

bool nano::account_receivable_by_amount_key::deserialize (nano::stream & stream_a)
{
	auto error{ false };
	try
	{
		nano::read (stream_a, account.bytes);
		nano::read (stream_a, amount.bytes);
		nano::read (stream_a, send_block_hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}
