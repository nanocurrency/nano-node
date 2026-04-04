#include <nano/lib/block_sideband.hpp>
#include <nano/lib/block_type.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>

#include <bitset>

/*
 * block_details
 */

nano::block_details::block_details (nano::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a) :
	epoch (epoch_a), is_send (is_send_a), is_receive (is_receive_a), is_epoch (is_epoch_a)
{
}

bool nano::block_details::operator== (nano::block_details const & other_a) const
{
	return epoch == other_a.epoch && is_send == other_a.is_send && is_receive == other_a.is_receive && is_epoch == other_a.is_epoch;
}

uint8_t nano::block_details::packed () const
{
	std::bitset<8> result (static_cast<uint8_t> (epoch));
	result.set (7, is_send);
	result.set (6, is_receive);
	result.set (5, is_epoch);
	return static_cast<uint8_t> (result.to_ulong ());
}

void nano::block_details::unpack (uint8_t details_a)
{
	constexpr std::bitset<8> epoch_mask{ 0b00011111 };
	auto as_bitset = static_cast<std::bitset<8>> (details_a);
	is_send = as_bitset.test (7);
	is_receive = as_bitset.test (6);
	is_epoch = as_bitset.test (5);
	epoch = static_cast<nano::epoch> ((as_bitset & epoch_mask).to_ulong ());
}

void nano::block_details::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, packed ());
}

bool nano::block_details::deserialize (nano::stream & stream_a)
{
	bool result (false);
	try
	{
		uint8_t packed{ 0 };
		nano::read (stream_a, packed);
		unpack (packed);
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

void nano::block_details::operator() (nano::object_stream & obs) const
{
	obs.write ("epoch", epoch);
	obs.write ("is_send", is_send);
	obs.write ("is_receive", is_receive);
	obs.write ("is_epoch", is_epoch);
}

std::string nano::state_subtype (nano::block_details const details_a)
{
	debug_assert (details_a.is_epoch + details_a.is_receive + details_a.is_send <= 1);
	if (details_a.is_send)
	{
		return "send";
	}
	else if (details_a.is_receive)
	{
		return "receive";
	}
	else if (details_a.is_epoch)
	{
		return "epoch";
	}
	else
	{
		return "change";
	}
}

/*
 * block_sideband
 */

nano::block_sideband::block_sideband (nano::account const & account_a, nano::amount const & balance_a, uint64_t const height_a, nano::seconds_t const timestamp_a, nano::block_details const & details_a, nano::epoch const source_epoch_a) :
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (details_a),
	source_epoch (source_epoch_a)
{
}

nano::block_sideband::block_sideband (nano::account const & account_a, nano::amount const & balance_a, uint64_t const height_a, nano::seconds_t const timestamp_a, nano::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, nano::epoch const source_epoch_a) :
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (epoch_a, is_send, is_receive, is_epoch),
	source_epoch (source_epoch_a)
{
}

bool nano::block_sideband::includes_account (nano::block_type const type)
{
	return type != nano::block_type::state && type != nano::block_type::open;
}

bool nano::block_sideband::includes_height (nano::block_type const type)
{
	return type != nano::block_type::open;
}

bool nano::block_sideband::includes_balance (nano::block_type const type)
{
	return type == nano::block_type::receive || type == nano::block_type::change || type == nano::block_type::open;
}

bool nano::block_sideband::includes_details (nano::block_type const type)
{
	return type == nano::block_type::state;
}

size_t nano::block_sideband::size (nano::block_type type)
{
	size_t result (0);
	if (includes_account (type))
	{
		result += sizeof (account);
	}
	if (includes_height (type))
	{
		result += sizeof (height);
	}
	if (includes_balance (type))
	{
		result += sizeof (balance);
	}
	result += sizeof (timestamp);
	if (includes_details (type))
	{
		static_assert (sizeof (nano::epoch) == nano::block_details::size (), "block_details is larger than the epoch enum");
		result += nano::block_details::size () + sizeof (nano::epoch);
	}
	return result;
}

void nano::block_sideband::serialize (nano::stream & stream_a, nano::block_type type) const
{
	if (includes_account (type))
	{
		nano::write (stream_a, account.bytes);
	}
	if (includes_height (type))
	{
		nano::write (stream_a, boost::endian::native_to_big (height));
	}
	if (includes_balance (type))
	{
		nano::write (stream_a, balance.bytes);
	}
	nano::write (stream_a, boost::endian::native_to_big (timestamp));
	if (includes_details (type))
	{
		details.serialize (stream_a);
		nano::write (stream_a, static_cast<uint8_t> (source_epoch));
	}
}

bool nano::block_sideband::deserialize (nano::stream & stream_a, nano::block_type type)
{
	bool result (false);
	try
	{
		if (includes_account (type))
		{
			nano::read (stream_a, account.bytes);
		}
		else
		{
			account.clear ();
		}
		if (includes_height (type))
		{
			nano::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (includes_balance (type))
		{
			nano::read (stream_a, balance.bytes);
		}
		else
		{
			balance.clear ();
		}
		nano::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
		if (includes_details (type))
		{
			result = details.deserialize (stream_a);
			uint8_t source_epoch_uint8_t{ 0 };
			nano::read (stream_a, source_epoch_uint8_t);
			source_epoch = static_cast<nano::epoch> (source_epoch_uint8_t);
		}
		else
		{
			details = {};
			source_epoch = nano::epoch::epoch_0;
		}
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

void nano::block_sideband::operator() (nano::object_stream & obs) const
{
	obs.write ("account", account);
	obs.write ("balance", balance);
	obs.write ("height", height);
	obs.write ("timestamp", timestamp);
	obs.write ("source_epoch", source_epoch);
	obs.write ("details", details);
}

/*
 * block_sideband_v25
 */

size_t nano::block_sideband_v25::size (nano::block_type type)
{
	return nano::block_sideband::size (type) + sizeof (nano::block_hash);
}

void nano::block_sideband_v25::serialize (nano::stream & stream_a, nano::block_type type) const
{
	nano::write (stream_a, successor.bytes);
	if (nano::block_sideband::includes_account (type))
	{
		nano::write (stream_a, account.bytes);
	}
	if (nano::block_sideband::includes_height (type))
	{
		nano::write (stream_a, boost::endian::native_to_big (height));
	}
	if (nano::block_sideband::includes_balance (type))
	{
		nano::write (stream_a, balance.bytes);
	}
	nano::write (stream_a, boost::endian::native_to_big (timestamp));
	if (nano::block_sideband::includes_details (type))
	{
		details.serialize (stream_a);
		nano::write (stream_a, static_cast<uint8_t> (source_epoch));
	}
}

bool nano::block_sideband_v25::deserialize (nano::stream & stream_a, nano::block_type type)
{
	bool result (false);
	try
	{
		nano::read (stream_a, successor.bytes);
		if (nano::block_sideband::includes_account (type))
		{
			nano::read (stream_a, account.bytes);
		}
		else
		{
			account.clear ();
		}
		if (nano::block_sideband::includes_height (type))
		{
			nano::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (nano::block_sideband::includes_balance (type))
		{
			nano::read (stream_a, balance.bytes);
		}
		else
		{
			balance.clear ();
		}
		nano::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
		if (nano::block_sideband::includes_details (type))
		{
			result = details.deserialize (stream_a);
			uint8_t source_epoch_uint8_t{ 0 };
			nano::read (stream_a, source_epoch_uint8_t);
			source_epoch = static_cast<nano::epoch> (source_epoch_uint8_t);
		}
		else
		{
			details = {};
			source_epoch = nano::epoch::epoch_0;
		}
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}
