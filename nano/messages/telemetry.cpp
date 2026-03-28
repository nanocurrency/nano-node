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
	debug_assert (telemetry_data_a.serialized_size () <= message_header::telemetry_size_mask.to_ulong ()); // Maximum size the mask allows
	header.extensions &= ~message_header::telemetry_size_mask;
	header.extensions |= std::bitset<16> (static_cast<unsigned long long> (telemetry_data_a.serialized_size ()));
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
	uint8_t maker_l;
	read (stream_a, maker_l);
	maker = static_cast<telemetry_maker> (maker_l);

	uint64_t timestamp_l;
	read (stream_a, timestamp_l);
	boost::endian::big_to_native_inplace (timestamp_l);
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	read (stream_a, active_difficulty);
	boost::endian::big_to_native_inplace (active_difficulty);
	if (payload_length_a >= size_v2)
	{
		uint8_t database_backend_l;
		read (stream_a, database_backend_l);
		database_backend = static_cast<telemetry_database_backend> (database_backend_l);
		read (stream_a, confirmation_latency_ms_p50);
		boost::endian::big_to_native_inplace (confirmation_latency_ms_p50);
		read (stream_a, confirmation_latency_ms_p90);
		boost::endian::big_to_native_inplace (confirmation_latency_ms_p90);
		read (stream_a, confirmation_latency_ms_p99);
		boost::endian::big_to_native_inplace (confirmation_latency_ms_p99);
		uint8_t bootstrap_status_l;
		read (stream_a, bootstrap_status_l);
		bootstrap_status = static_cast<telemetry_bootstrap_status> (bootstrap_status_l);
		version = telemetry_data_version::v2;
	}
	else
	{
		version = telemetry_data_version::v1;
	}
	auto known_size = size_for_version (version);
	if (payload_length_a > known_size)
	{
		read (stream_a, unknown_data, payload_length_a - known_size);
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
	write (stream_a, static_cast<uint8_t> (maker));
	write (stream_a, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ()));
	write (stream_a, boost::endian::native_to_big (active_difficulty));
	if (version >= telemetry_data_version::v2)
	{
		write (stream_a, static_cast<uint8_t> (database_backend));
		write (stream_a, boost::endian::native_to_big (confirmation_latency_ms_p50));
		write (stream_a, boost::endian::native_to_big (confirmation_latency_ms_p90));
		write (stream_a, boost::endian::native_to_big (confirmation_latency_ms_p99));
		write (stream_a, static_cast<uint8_t> (bootstrap_status));
	}
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
	json.put ("maker", to_string (maker));
	json.put ("timestamp", std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ());
	json.put ("active_difficulty", nano::to_string_hex (active_difficulty));
	if (version >= telemetry_data_version::v2)
	{
		json.put ("database_backend", to_string (database_backend));
		json.put ("confirmation_latency_ms_p50", confirmation_latency_ms_p50);
		json.put ("confirmation_latency_ms_p90", confirmation_latency_ms_p90);
		json.put ("confirmation_latency_ms_p99", confirmation_latency_ms_p99);
		json.put ("bootstrap_status", to_string (bootstrap_status));
	}
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
	auto maker_l = json.get<std::string> ("maker");
	maker = telemetry_maker_from_string (maker_l);
	auto timestamp_l = json.get<uint64_t> ("timestamp");
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	auto current_active_difficulty_text = json.get<std::string> ("active_difficulty");
	auto ec = nano::from_string_hex (current_active_difficulty_text, active_difficulty);
	debug_assert (!ec);
	if (json.has_key ("database_backend"))
	{
		auto database_backend_l = json.get<std::string> ("database_backend");
		database_backend = telemetry_database_backend_from_string (database_backend_l);
		json.get ("confirmation_latency_ms_p50", confirmation_latency_ms_p50);
		json.get ("confirmation_latency_ms_p90", confirmation_latency_ms_p90);
		json.get ("confirmation_latency_ms_p99", confirmation_latency_ms_p99);
		auto bootstrap_status_l = json.get<std::string> ("bootstrap_status");
		bootstrap_status = telemetry_bootstrap_status_from_string (bootstrap_status_l);
		version = telemetry_data_version::v2;
	}
	else
	{
		version = telemetry_data_version::v1;
	}
	return json.get_error ();
}

bool telemetry_data::operator== (telemetry_data const & data_a) const
{
	return (signature == data_a.signature && node_id == data_a.node_id && block_count == data_a.block_count && cemented_count == data_a.cemented_count && unchecked_count == data_a.unchecked_count && account_count == data_a.account_count && bandwidth_cap == data_a.bandwidth_cap && uptime == data_a.uptime && peer_count == data_a.peer_count && protocol_version == data_a.protocol_version && genesis_block == data_a.genesis_block && major_version == data_a.major_version && minor_version == data_a.minor_version && patch_version == data_a.patch_version && pre_release_version == data_a.pre_release_version && maker == data_a.maker && timestamp == data_a.timestamp && active_difficulty == data_a.active_difficulty && version == data_a.version && database_backend == data_a.database_backend && confirmation_latency_ms_p50 == data_a.confirmation_latency_ms_p50 && confirmation_latency_ms_p90 == data_a.confirmation_latency_ms_p90 && confirmation_latency_ms_p99 == data_a.confirmation_latency_ms_p99 && bootstrap_status == data_a.bootstrap_status && unknown_data == data_a.unknown_data);
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

uint16_t telemetry_data::serialized_size () const
{
	return size_for_version (version) + static_cast<uint16_t> (unknown_data.size ());
}

uint16_t telemetry_data::size_for_version (telemetry_data_version ver)
{
	switch (ver)
	{
		case telemetry_data_version::v1:
			return size_v1;
		case telemetry_data_version::v2:
			return size_v2;
		default:
			debug_assert (false, "unknown telemetry version");
			return size_v2;
	}
}

void telemetry_data::operator() (nano::object_stream & obs) const
{
	// TODO: Telemetry data
}
}

/*
 *
 */

namespace nano::messages
{
telemetry_database_backend to_telemetry_database_backend (nano::database_backend backend)
{
	switch (backend)
	{
		case nano::database_backend::lmdb:
			return telemetry_database_backend::lmdb;
		case nano::database_backend::rocksdb:
			return telemetry_database_backend::rocksdb;
		default:
			return telemetry_database_backend::unknown;
	}
}

std::string to_string (telemetry_maker maker)
{
	switch (maker)
	{
		case telemetry_maker::nf_node:
			return "nf_node";
		case telemetry_maker::nf_pruned_node:
			return "nf_pruned_node";
		case telemetry_maker::nano_node_light:
			return "nano_node_light";
		case telemetry_maker::rs_nano:
			return "rs_nano";
	}
	return "invalid";
}

std::string to_string (telemetry_database_backend backend)
{
	switch (backend)
	{
		case telemetry_database_backend::unknown:
			return "unknown";
		case telemetry_database_backend::lmdb:
			return "lmdb";
		case telemetry_database_backend::rocksdb:
			return "rocksdb";
	}
	return "invalid";
}

std::string to_string (telemetry_bootstrap_status status)
{
	switch (status)
	{
		case telemetry_bootstrap_status::unknown:
			return "unknown";
		case telemetry_bootstrap_status::syncing:
			return "syncing";
		case telemetry_bootstrap_status::synced:
			return "synced";
	}
	return "invalid";
}

telemetry_maker telemetry_maker_from_string (std::string const & str)
{
	if (str == "nf_node")
		return telemetry_maker::nf_node;
	if (str == "nf_pruned_node")
		return telemetry_maker::nf_pruned_node;
	if (str == "nano_node_light")
		return telemetry_maker::nano_node_light;
	if (str == "rs_nano")
		return telemetry_maker::rs_nano;
	return telemetry_maker::nf_node;
}

telemetry_database_backend telemetry_database_backend_from_string (std::string const & str)
{
	if (str == "lmdb")
		return telemetry_database_backend::lmdb;
	if (str == "rocksdb")
		return telemetry_database_backend::rocksdb;
	return telemetry_database_backend::unknown;
}

telemetry_bootstrap_status telemetry_bootstrap_status_from_string (std::string const & str)
{
	if (str == "syncing")
		return telemetry_bootstrap_status::syncing;
	if (str == "synced")
		return telemetry_bootstrap_status::synced;
	return telemetry_bootstrap_status::unknown;
}
}
