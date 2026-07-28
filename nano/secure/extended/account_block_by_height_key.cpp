#include <nano/lib/stream.hpp>
#include <nano/secure/extended/account_block_by_height_key.hpp>

#include <boost/endian/conversion.hpp>

nano::account_block_by_height_key::account_block_by_height_key (nano::account const & account_a, uint64_t height_a) :
	account{ account_a },
	height{ height_a }
{
}

void nano::account_block_by_height_key::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, account.bytes);
	nano::write (stream_a, boost::endian::native_to_big (height));
}

bool nano::account_block_by_height_key::deserialize (nano::stream & stream_a)
{
	auto error{ false };
	try
	{
		nano::read (stream_a, account.bytes);
		nano::read (stream_a, height);
		boost::endian::big_to_native_inplace (height);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}
