#include <celerix/lib/block_sideband.hpp>
#include <celerix/lib/block_type.hpp>
#include <celerix/lib/object_stream.hpp>
#include <celerix/lib/stream.hpp>

#include <bitset>

/*
 * block_details
 */

celerix::block_details::block_details (celerix::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a) :
	epoch (epoch_a), is_send (is_send_a), is_receive (is_receive_a), is_epoch (is_epoch_a)
{
}

bool celerix::block_details::operator== (celerix::block_details const & other_a) const
{
	return epoch == other_a.epoch && is_send == other_a.is_send && is_receive == other_a.is_receive && is_epoch == other_a.is_epoch;
}

uint8_t celerix::block_details::packed () const
{
	std::bitset<8> result (static_cast<uint8_t> (epoch));
	result.set (7, is_send);
	result.set (6, is_receive);
	result.set (5, is_epoch);
	return static_cast<uint8_t> (result.to_ulong ());
}

void celerix::block_details::unpack (uint8_t details_a)
{
	constexpr std::bitset<8> epoch_mask{ 0b00011111 };
	auto as_bitset = static_cast<std::bitset<8>> (details_a);
	is_send = as_bitset.test (7);
	is_receive = as_bitset.test (6);
	is_epoch = as_bitset.test (5);
	epoch = static_cast<celerix::epoch> ((as_bitset & epoch_mask).to_ulong ());
}

void celerix::block_details::serialize (celerix::stream & stream_a) const
{
	celerix::write (stream_a, packed ());
}

bool celerix::block_details::deserialize (celerix::stream & stream_a)
{
	bool result (false);
	try
	{
		uint8_t packed{ 0 };
		celerix::read (stream_a, packed);
		unpack (packed);
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

void celerix::block_details::operator() (celerix::object_stream & obs) const
{
	obs.write ("epoch", epoch);
	obs.write ("is_send", is_send);
	obs.write ("is_receive", is_receive);
	obs.write ("is_epoch", is_epoch);
}

std::string celerix::state_subtype (celerix::block_details const details_a)
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

celerix::block_sideband::block_sideband (celerix::account const & account_a, celerix::block_hash const & successor_a, celerix::amount const & balance_a, uint64_t const height_a, celerix::seconds_t const timestamp_a, celerix::block_details const & details_a, celerix::epoch const source_epoch_a) :
	successor (successor_a),
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (details_a),
	source_epoch (source_epoch_a)
{
}

celerix::block_sideband::block_sideband (celerix::account const & account_a, celerix::block_hash const & successor_a, celerix::amount const & balance_a, uint64_t const height_a, celerix::seconds_t const timestamp_a, celerix::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, celerix::epoch const source_epoch_a) :
	successor (successor_a),
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (epoch_a, is_send, is_receive, is_epoch),
	source_epoch (source_epoch_a)
{
}

size_t celerix::block_sideband::size (celerix::block_type type_a)
{
	size_t result (0);
	result += sizeof (successor);
	if (type_a != celerix::block_type::state && type_a != celerix::block_type::open)
	{
		result += sizeof (account);
	}
	if (type_a != celerix::block_type::open)
	{
		result += sizeof (height);
	}
	if (type_a == celerix::block_type::receive || type_a == celerix::block_type::change || type_a == celerix::block_type::open)
	{
		result += sizeof (balance);
	}
	result += sizeof (timestamp);
	if (type_a == celerix::block_type::state)
	{
		static_assert (sizeof (celerix::epoch) == celerix::block_details::size (), "block_details is larger than the epoch enum");
		result += celerix::block_details::size () + sizeof (celerix::epoch);
	}
	return result;
}

void celerix::block_sideband::serialize (celerix::stream & stream_a, celerix::block_type type_a) const
{
	celerix::write (stream_a, successor.bytes);
	if (type_a != celerix::block_type::state && type_a != celerix::block_type::open)
	{
		celerix::write (stream_a, account.bytes);
	}
	if (type_a != celerix::block_type::open)
	{
		celerix::write (stream_a, boost::endian::native_to_big (height));
	}
	if (type_a == celerix::block_type::receive || type_a == celerix::block_type::change || type_a == celerix::block_type::open)
	{
		celerix::write (stream_a, balance.bytes);
	}
	celerix::write (stream_a, boost::endian::native_to_big (timestamp));
	if (type_a == celerix::block_type::state)
	{
		details.serialize (stream_a);
		celerix::write (stream_a, static_cast<uint8_t> (source_epoch));
	}
}

bool celerix::block_sideband::deserialize (celerix::stream & stream_a, celerix::block_type type_a)
{
	bool result (false);
	try
	{
		celerix::read (stream_a, successor.bytes);
		if (type_a != celerix::block_type::state && type_a != celerix::block_type::open)
		{
			celerix::read (stream_a, account.bytes);
		}
		if (type_a != celerix::block_type::open)
		{
			celerix::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (type_a == celerix::block_type::receive || type_a == celerix::block_type::change || type_a == celerix::block_type::open)
		{
			celerix::read (stream_a, balance.bytes);
		}
		celerix::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
		if (type_a == celerix::block_type::state)
		{
			result = details.deserialize (stream_a);
			uint8_t source_epoch_uint8_t{ 0 };
			celerix::read (stream_a, source_epoch_uint8_t);
			source_epoch = static_cast<celerix::epoch> (source_epoch_uint8_t);
		}
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

void celerix::block_sideband::operator() (celerix::object_stream & obs) const
{
	obs.write ("successor", successor);
	obs.write ("account", account);
	obs.write ("balance", balance);
	obs.write ("height", height);
	obs.write ("timestamp", timestamp);
	obs.write ("source_epoch", source_epoch);
	obs.write ("details", details);
}
