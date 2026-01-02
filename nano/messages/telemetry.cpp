#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/messages/message_visitor.hpp>
#include <nano/messages/telemetry.hpp>

#include <boost/endian/conversion.hpp>

namespace nano::messages
{
/*
 * telemetry_req
 */

telemetry_req::telemetry_req (nano::network_constants const & constants) :
	message (constants, message_type::telemetry_req)
{
}

telemetry_req::telemetry_req (message_header const & header_a) :
	message (header_a)
{
}

bool telemetry_req::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == message_type::telemetry_req);
	return false;
}

void telemetry_req::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
}

void telemetry_req::visit (message_visitor & visitor_a) const
{
	visitor_a.telemetry_req (*this);
}

void telemetry_req::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data
}

/*
 * telemetry_ack
 */

telemetry_ack::telemetry_ack (nano::network_constants const & constants) :
	message (constants, message_type::telemetry_ack)
{
}

telemetry_ack::telemetry_ack (bool & error_a, nano::stream & stream_a, message_header const & message_header) :
	message (message_header)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

telemetry_ack::telemetry_ack (nano::network_constants const & constants, telemetry_data const & telemetry_data_a) :
	message (constants, message_type::telemetry_ack),
	data (telemetry_data_a)
{
	debug_assert (telemetry_data::size + telemetry_data_a.unknown_data.size () <= message_header::telemetry_size_mask.to_ulong ()); // Maximum size the mask allows
	header.extensions &= ~message_header::telemetry_size_mask;
	header.extensions |= std::bitset<16> (static_cast<unsigned long long> (telemetry_data::size) + telemetry_data_a.unknown_data.size ());
}

void telemetry_ack::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	if (!is_empty_payload ())
	{
		data.serialize (stream_a);
	}
}

bool telemetry_ack::deserialize (nano::stream & stream_a)
{
	auto error (false);
	debug_assert (header.type == message_type::telemetry_ack);
	try
	{
		if (!is_empty_payload ())
		{
			data.deserialize (stream_a, nano::narrow_cast<uint16_t> (header.extensions.to_ulong ()));
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void telemetry_ack::visit (message_visitor & visitor_a) const
{
	visitor_a.telemetry_ack (*this);
}

uint16_t telemetry_ack::size () const
{
	return size (header);
}

uint16_t telemetry_ack::size (message_header const & message_header_a)
{
	return static_cast<uint16_t> ((message_header_a.extensions & message_header::telemetry_size_mask).to_ullong ());
}

bool telemetry_ack::is_empty_payload () const
{
	return size () == 0;
}

void telemetry_ack::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	if (!is_empty_payload ())
	{
		obs.write ("data", data);
	}
}

/*
 * telemetry_data
 */

void telemetry_data::deserialize (nano::stream & stream_a, uint16_t payload_length_a)
{
	read (stream_a, signature);
	read (stream_a, node_id);
	read (stream_a, block_count);
	boost::endian::big_to_native_inplace (block_count);
	read (stream_a, cemented_count);
	boost::endian::big_to_native_inplace (cemented_count);
	read (stream_a, unchecked_count);
	boost::endian::big_to_native_inplace (unchecked_count);
	read (stream_a, account_count);
	boost::endian::big_to_native_inplace (account_count);
	read (stream_a, bandwidth_cap);
	boost::endian::big_to_native_inplace (bandwidth_cap);
	read (stream_a, peer_count);
	boost::endian::big_to_native_inplace (peer_count);
	read (stream_a, protocol_version);
	read (stream_a, uptime);
	boost::endian::big_to_native_inplace (uptime);
	read (stream_a, genesis_block.bytes);
	read (stream_a, major_version);
	read (stream_a, minor_version);
	read (stream_a, patch_version);
	read (stream_a, pre_release_version);
	read (stream_a, maker);

	uint64_t timestamp_l;
	read (stream_a, timestamp_l);
	boost::endian::big_to_native_inplace (timestamp_l);
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	read (stream_a, active_difficulty);
	boost::endian::big_to_native_inplace (active_difficulty);
	if (payload_length_a > latest_size)
	{
		read (stream_a, unknown_data, payload_length_a - latest_size);
	}
}

void telemetry_data::serialize_without_signature (nano::stream & stream_a) const
{
	// All values should be serialized in big endian
	write (stream_a, node_id);
	write (stream_a, boost::endian::native_to_big (block_count));
	write (stream_a, boost::endian::native_to_big (cemented_count));
	write (stream_a, boost::endian::native_to_big (unchecked_count));
	write (stream_a, boost::endian::native_to_big (account_count));
	write (stream_a, boost::endian::native_to_big (bandwidth_cap));
	write (stream_a, boost::endian::native_to_big (peer_count));
	write (stream_a, protocol_version);
	write (stream_a, boost::endian::native_to_big (uptime));
	write (stream_a, genesis_block.bytes);
	write (stream_a, major_version);
	write (stream_a, minor_version);
	write (stream_a, patch_version);
	write (stream_a, pre_release_version);
	write (stream_a, maker);
	write (stream_a, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ()));
	write (stream_a, boost::endian::native_to_big (active_difficulty));
	write (stream_a, unknown_data);
}

void telemetry_data::serialize (nano::stream & stream_a) const
{
	write (stream_a, signature);
	serialize_without_signature (stream_a);
}

nano::error telemetry_data::serialize_json (nano::jsonconfig & json, bool ignore_identification_metrics_a) const
{
	json.put ("block_count", block_count);
	json.put ("cemented_count", cemented_count);
	json.put ("unchecked_count", unchecked_count);
	json.put ("account_count", account_count);
	json.put ("bandwidth_cap", bandwidth_cap);
	json.put ("peer_count", peer_count);
	json.put ("protocol_version", protocol_version);
	json.put ("uptime", uptime);
	json.put ("genesis_block", genesis_block.to_string ());
	json.put ("major_version", major_version);
	json.put ("minor_version", minor_version);
	json.put ("patch_version", patch_version);
	json.put ("pre_release_version", pre_release_version);
	json.put ("maker", maker);
	json.put ("timestamp", std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ());
	json.put ("active_difficulty", nano::to_string_hex (active_difficulty));
	// Keep these last for UI purposes
	if (!ignore_identification_metrics_a)
	{
		json.put ("node_id", node_id.to_node_id ());
		json.put ("signature", signature.to_string ());
	}
	return json.get_error ();
}

nano::error telemetry_data::deserialize_json (nano::jsonconfig & json, bool ignore_identification_metrics_a)
{
	if (!ignore_identification_metrics_a)
	{
		std::string signature_l;
		json.get ("signature", signature_l);
		if (!json.get_error ())
		{
			if (signature.decode_hex (signature_l))
			{
				json.get_error ().set ("Could not deserialize signature");
			}
		}

		std::string node_id_l;
		json.get ("node_id", node_id_l);
		if (!json.get_error ())
		{
			if (node_id.decode_node_id (node_id_l))
			{
				json.get_error ().set ("Could not deserialize node id");
			}
		}
	}

	json.get ("block_count", block_count);
	json.get ("cemented_count", cemented_count);
	json.get ("unchecked_count", unchecked_count);
	json.get ("account_count", account_count);
	json.get ("bandwidth_cap", bandwidth_cap);
	json.get ("peer_count", peer_count);
	json.get ("protocol_version", protocol_version);
	json.get ("uptime", uptime);
	std::string genesis_block_l;
	json.get ("genesis_block", genesis_block_l);
	if (!json.get_error ())
	{
		if (genesis_block.decode_hex (genesis_block_l))
		{
			json.get_error ().set ("Could not deserialize genesis block");
		}
	}
	json.get ("major_version", major_version);
	json.get ("minor_version", minor_version);
	json.get ("patch_version", patch_version);
	json.get ("pre_release_version", pre_release_version);
	json.get ("maker", maker);
	auto timestamp_l = json.get<uint64_t> ("timestamp");
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	auto current_active_difficulty_text = json.get<std::string> ("active_difficulty");
	auto ec = nano::from_string_hex (current_active_difficulty_text, active_difficulty);
	debug_assert (!ec);
	return json.get_error ();
}

bool telemetry_data::operator== (telemetry_data const & data_a) const
{
	return (signature == data_a.signature && node_id == data_a.node_id && block_count == data_a.block_count && cemented_count == data_a.cemented_count && unchecked_count == data_a.unchecked_count && account_count == data_a.account_count && bandwidth_cap == data_a.bandwidth_cap && uptime == data_a.uptime && peer_count == data_a.peer_count && protocol_version == data_a.protocol_version && genesis_block == data_a.genesis_block && major_version == data_a.major_version && minor_version == data_a.minor_version && patch_version == data_a.patch_version && pre_release_version == data_a.pre_release_version && maker == data_a.maker && timestamp == data_a.timestamp && active_difficulty == data_a.active_difficulty && unknown_data == data_a.unknown_data);
}

bool telemetry_data::operator!= (telemetry_data const & data_a) const
{
	return !(*this == data_a);
}

void telemetry_data::sign (nano::keypair const & node_id_a)
{
	debug_assert (node_id == node_id_a.pub);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	signature = nano::sign_message (node_id_a.prv, node_id_a.pub, bytes.data (), bytes.size ());
}

bool telemetry_data::validate_signature () const
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	return nano::validate_message (node_id, bytes.data (), bytes.size (), signature);
}

void telemetry_data::operator() (nano::object_stream & obs) const
{
	// TODO: Telemetry data
}
}
