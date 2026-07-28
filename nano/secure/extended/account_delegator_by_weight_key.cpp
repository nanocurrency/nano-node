#include <nano/lib/stream.hpp>
#include <nano/secure/extended/account_delegator_by_weight_key.hpp>

nano::account_delegator_by_weight_key::account_delegator_by_weight_key (nano::account const & representative_a, nano::amount const & weight_a, nano::account const & delegator_a) :
	representative{ representative_a },
	weight{ weight_a },
	delegator{ delegator_a }
{
}

void nano::account_delegator_by_weight_key::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, representative.bytes);
	nano::write (stream_a, weight.bytes);
	nano::write (stream_a, delegator.bytes);
}

bool nano::account_delegator_by_weight_key::deserialize (nano::stream & stream_a)
{
	auto error{ false };
	try
	{
		nano::read (stream_a, representative.bytes);
		nano::read (stream_a, weight.bytes);
		nano::read (stream_a, delegator.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}
