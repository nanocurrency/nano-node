#include <nano/lib/stream.hpp>
#include <nano/secure/extended/account_receivable_by_amount_info.hpp>

nano::account_receivable_by_amount_info::account_receivable_by_amount_info (nano::account const & source_a, nano::epoch epoch_a) :
	source{ source_a },
	epoch{ epoch_a }
{
}

void nano::account_receivable_by_amount_info::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, source.bytes);
	nano::write (stream_a, epoch);
}

bool nano::account_receivable_by_amount_info::deserialize (nano::stream & stream_a)
{
	auto error{ false };
	try
	{
		nano::read (stream_a, source.bytes);
		nano::read (stream_a, epoch);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

size_t nano::account_receivable_by_amount_info::db_size () const
{
	return sizeof (source) + sizeof (epoch);
}
