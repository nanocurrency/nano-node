#include <celerix/lib/block_type.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/config.hpp>
#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/jsonconfig.hpp>
#include <celerix/lib/memory.hpp>
#include <celerix/lib/stats_enums.hpp>
#include <celerix/lib/stream.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/lib/work.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/messages.hpp>
#include <celerix/node/network.hpp>
#include <celerix/secure/vote.hpp>

#include <boost/asio/ip/address_v6.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/format.hpp>
#include <boost/pool/pool_alloc.hpp>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

/*
 * message_header
 */

celerix::message_header::message_header (celerix::network_constants const & constants, celerix::message_type type_a) :
	network{ constants.current_network },
	version_max{ constants.protocol_version },
	version_using{ constants.protocol_version },
	version_min{ constants.protocol_version_min },
	type (type_a)
{
}

celerix::message_header::message_header (bool & error_a, celerix::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void celerix::message_header::serialize (celerix::stream & stream_a) const
{
	celerix::write (stream_a, boost::endian::native_to_big (static_cast<uint16_t> (network)));
	celerix::write (stream_a, version_max);
	celerix::write (stream_a, version_using);
	celerix::write (stream_a, version_min);
	celerix::write (stream_a, type);
	celerix::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool celerix::message_header::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	try
	{
		uint16_t network_bytes;
		celerix::read (stream_a, network_bytes);
		network = static_cast<celerix::networks> (boost::endian::big_to_native (network_bytes));
		celerix::read (stream_a, version_max);
		celerix::read (stream_a, version_using);
		celerix::read (stream_a, version_min);
		celerix::read (stream_a, type);
		uint16_t extensions_l;
		celerix::read (stream_a, extensions_l);
		extensions = extensions_l;
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

celerix::block_type celerix::message_header::block_type () const
{
	return static_cast<celerix::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void celerix::message_header::block_type_set (celerix::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= (extensions_bitset_t{ static_cast<unsigned long long> (type_a) } << 8);
}

uint8_t celerix::message_header::count_get () const
{
	debug_assert (type == celerix::message_type::confirm_ack || type == celerix::message_type::confirm_req);
	debug_assert (!flag_test (confirm_v2_flag)); // Only valid for v1

	return static_cast<uint8_t> (((extensions & count_mask) >> 12).to_ullong ());
}

void celerix::message_header::count_set (uint8_t count_a)
{
	debug_assert (type == celerix::message_type::confirm_ack || type == celerix::message_type::confirm_req);
	debug_assert (!flag_test (confirm_v2_flag)); // Only valid for v1
	debug_assert (count_a < 16); // Max 4 bits

	extensions &= ~count_mask;
	extensions |= ((extensions_bitset_t{ count_a } << 12) & count_mask);
}

/*
 * We need those shenanigans because we need to keep compatibility with previous protocol versions (<= V25.1)
 */

uint8_t celerix::message_header::count_v2_get () const
{
	debug_assert (type == celerix::message_type::confirm_ack || type == celerix::message_type::confirm_req);
	debug_assert (flag_test (confirm_v2_flag)); // Only valid for v2

	// Extract 2 parts of 4 bits
	auto left = (extensions & count_v2_mask_left) >> 12;
	auto right = (extensions & count_v2_mask_right) >> 4;

	return static_cast<uint8_t> (((left << 4) | right).to_ullong ());
}

void celerix::message_header::count_v2_set (uint8_t count)
{
	debug_assert (type == celerix::message_type::confirm_ack || type == celerix::message_type::confirm_req);
	debug_assert (flag_test (confirm_v2_flag)); // Only valid for v2

	extensions &= ~(count_v2_mask_left | count_v2_mask_right);

	// Split count into 2 parts of 4 bits
	extensions_bitset_t trim_mask{ 0xf };
	auto left = (extensions_bitset_t{ count } >> 4) & trim_mask;
	auto right = (extensions_bitset_t{ count }) & trim_mask;

	extensions |= (left << 12) | (right << 4);
}

bool celerix::message_header::flag_test (uint8_t flag) const
{
	// Extension bits at index >= 8 are block type & count
	debug_assert (flag < 8);
	return extensions.test (flag);
}

void celerix::message_header::flag_set (uint8_t flag, bool enable)
{
	// Extension bits at index >= 8 are block type & count
	debug_assert (flag < 8);
	extensions.set (flag, enable);
}

bool celerix::message_header::bulk_pull_is_count_present () const
{
	auto result (false);
	if (type == celerix::message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_count_present_flag))
		{
			result = true;
		}
	}
	return result;
}

bool celerix::message_header::bulk_pull_ascending () const
{
	auto result (false);
	if (type == celerix::message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_ascending_flag))
		{
			result = true;
		}
	}
	return result;
}

bool celerix::message_header::frontier_req_is_only_confirmed_present () const
{
	auto result (false);
	if (type == celerix::message_type::frontier_req)
	{
		if (extensions.test (frontier_req_only_confirmed))
		{
			result = true;
		}
	}
	return result;
}

bool celerix::message_header::confirm_is_v2 () const
{
	debug_assert (type == celerix::message_type::confirm_ack || type == celerix::message_type::confirm_req);
	return flag_test (confirm_v2_flag);
}

void celerix::message_header::confirm_set_v2 (bool value)
{
	debug_assert (type == celerix::message_type::confirm_ack || type == celerix::message_type::confirm_req);
	flag_set (confirm_v2_flag, value);
}

std::size_t celerix::message_header::payload_length_bytes () const
{
	switch (type)
	{
		case celerix::message_type::bulk_pull:
		{
			return celerix::bulk_pull::size + (bulk_pull_is_count_present () ? celerix::bulk_pull::extended_parameters_size : 0);
		}
		case celerix::message_type::bulk_push:
		case celerix::message_type::telemetry_req:
		{
			// These don't have a payload
			return 0;
		}
		case celerix::message_type::frontier_req:
		{
			return celerix::frontier_req::size;
		}
		case celerix::message_type::bulk_pull_account:
		{
			return celerix::bulk_pull_account::size;
		}
		case celerix::message_type::keepalive:
		{
			return celerix::keepalive::size;
		}
		case celerix::message_type::publish:
		{
			return celerix::block::size (block_type ());
		}
		case celerix::message_type::confirm_ack:
		{
			return celerix::confirm_ack::size (*this);
		}
		case celerix::message_type::confirm_req:
		{
			return celerix::confirm_req::size (*this);
		}
		case celerix::message_type::node_id_handshake:
		{
			return celerix::node_id_handshake::size (*this);
		}
		case celerix::message_type::telemetry_ack:
		{
			return celerix::telemetry_ack::size (*this);
		}
		case celerix::message_type::asc_pull_req:
		{
			return celerix::asc_pull_req::size (*this);
		}
		case celerix::message_type::asc_pull_ack:
		{
			return celerix::asc_pull_ack::size (*this);
		}
		default:
		{
			debug_assert (false);
			return 0;
		}
	}
}

bool celerix::message_header::is_valid_message_type () const
{
	switch (type)
	{
		case celerix::message_type::bulk_pull:
		case celerix::message_type::bulk_push:
		case celerix::message_type::telemetry_req:
		case celerix::message_type::frontier_req:
		case celerix::message_type::bulk_pull_account:
		case celerix::message_type::keepalive:
		case celerix::message_type::publish:
		case celerix::message_type::confirm_ack:
		case celerix::message_type::confirm_req:
		case celerix::message_type::node_id_handshake:
		case celerix::message_type::telemetry_ack:
		case celerix::message_type::asc_pull_req:
		case celerix::message_type::asc_pull_ack:
		{
			return true;
		}
		default:
		{
			return false;
		}
	}
}

void celerix::message_header::operator() (celerix::object_stream & obs) const
{
	obs.write ("type", type);
	obs.write ("network", to_string (network));
	obs.write ("network_raw", static_cast<uint16_t> (network));
	obs.write ("version", static_cast<uint16_t> (version_using));
	obs.write ("version_min", static_cast<uint16_t> (version_min));
	obs.write ("version_max", static_cast<uint16_t> (version_max));
	obs.write ("extensions", static_cast<uint16_t> (extensions.to_ulong ()));
}

/*
 * message
 */

celerix::message::message (celerix::network_constants const & constants, celerix::message_type type_a) :
	header (constants, type_a)
{
}

celerix::message::message (celerix::message_header const & header_a) :
	header (header_a)
{
}

std::shared_ptr<std::vector<uint8_t>> celerix::message::to_bytes () const
{
	auto bytes = std::make_shared<std::vector<uint8_t>> ();
	celerix::vectorstream stream (*bytes);
	serialize (stream);
	return bytes;
}

celerix::shared_const_buffer celerix::message::to_shared_const_buffer () const
{
	return shared_const_buffer (to_bytes ());
}

celerix::message_type celerix::message::type () const
{
	return header.type;
}

void celerix::message::operator() (celerix::object_stream & obs) const
{
	obs.write ("header", header);
}

/*
 * keepalive
 */

celerix::keepalive::keepalive (celerix::network_constants const & constants) :
	message (constants, celerix::message_type::keepalive)
{
	celerix::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

celerix::keepalive::keepalive (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void celerix::keepalive::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void celerix::keepalive::serialize (celerix::stream & stream_a) const
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		debug_assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool celerix::keepalive::deserialize (celerix::stream & stream_a)
{
	debug_assert (header.type == celerix::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!try_read (stream_a, address) && !try_read (stream_a, port))
		{
			*i = celerix::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool celerix::keepalive::operator== (celerix::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

void celerix::keepalive::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write_range ("peers", peers);
}

/*
 * publish
 */

celerix::publish::publish (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a, celerix::network_filter::digest_t const & digest_a, celerix::block_uniquer * uniquer_a) :
	message (header_a),
	digest{ digest_a }
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

celerix::publish::publish (celerix::network_constants const & constants, std::shared_ptr<celerix::block> const & block_a, bool is_originator_a) :
	message (constants, celerix::message_type::publish),
	block{ block_a }
{
	header.block_type_set (block->type ());
	header.flag_set (originator_flag, is_originator_a);
}

void celerix::publish::serialize (celerix::stream & stream_a) const
{
	debug_assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool celerix::publish::deserialize (celerix::stream & stream_a, celerix::block_uniquer * uniquer_a)
{
	debug_assert (header.type == celerix::message_type::publish);
	block = celerix::deserialize_block (stream_a, header.block_type (), uniquer_a);
	auto result (block == nullptr);
	return result;
}

void celerix::publish::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool celerix::publish::operator== (celerix::publish const & other_a) const
{
	return *block == *other_a.block;
}

bool celerix::publish::is_originator () const
{
	return header.flag_test (originator_flag);
}

void celerix::publish::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("block", block);
	obs.write ("originator", is_originator ());
}

/*
 * confirm_req
 */

celerix::confirm_req::confirm_req (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

celerix::confirm_req::confirm_req (celerix::network_constants const & constants, std::vector<std::pair<celerix::block_hash, celerix::root>> const & roots_hashes_a) :
	message (constants, celerix::message_type::confirm_req),
	roots_hashes (roots_hashes_a)
{
	debug_assert (!roots_hashes.empty ());
	debug_assert (roots_hashes.size () < 256);

	// Set `not_a_block` (1) block type for hashes + roots request
	// This is needed to keep compatibility with previous protocol versions (<= V25.1)
	header.block_type_set (celerix::block_type::not_a_block);

	if (roots_hashes.size () >= 16)
	{
		// Set v2 flag and use extended count if there are more than 15 hash + root pairs
		header.confirm_set_v2 (true);
		header.count_v2_set (static_cast<uint8_t> (roots_hashes.size ()));
	}
	else
	{
		header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
	}
}

celerix::confirm_req::confirm_req (celerix::network_constants const & constants, celerix::block_hash const & hash_a, celerix::root const & root_a) :
	confirm_req (constants, std::vector<std::pair<celerix::block_hash, celerix::root>>{ { hash_a, root_a } })
{
}

void celerix::confirm_req::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void celerix::confirm_req::serialize (celerix::stream & stream_a) const
{
	debug_assert (!roots_hashes.empty ());

	header.serialize (stream_a);

	// Write hashes & roots
	for (auto & root_hash : roots_hashes)
	{
		celerix::write (stream_a, root_hash.first);
		celerix::write (stream_a, root_hash.second);
	}
}

bool celerix::confirm_req::deserialize (celerix::stream & stream_a)
{
	debug_assert (header.type == celerix::message_type::confirm_req);

	bool result = false;
	try
	{
		uint8_t const count = hash_count (header);
		for (auto i (0); i != count && !result; ++i)
		{
			celerix::block_hash block_hash (0);
			celerix::block_hash root (0);
			celerix::read (stream_a, block_hash);
			celerix::read (stream_a, root);
			if (!block_hash.is_zero () || !root.is_zero ())
			{
				roots_hashes.emplace_back (block_hash, root);
			}
		}

		result = roots_hashes.empty () || (roots_hashes.size () != count);
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}

	return result;
}

bool celerix::confirm_req::operator== (celerix::confirm_req const & other_a) const
{
	bool equal (false);
	if (!roots_hashes.empty () && !other_a.roots_hashes.empty ())
	{
		equal = roots_hashes == other_a.roots_hashes;
	}
	return equal;
}

uint8_t celerix::confirm_req::hash_count (const celerix::message_header & header)
{
	if (header.confirm_is_v2 ())
	{
		return header.count_v2_get ();
	}
	else
	{
		return header.count_get ();
	}
}

std::size_t celerix::confirm_req::size (celerix::message_header const & header)
{
	auto const count = hash_count (header);
	return count * (sizeof (decltype (roots_hashes)::value_type::first) + sizeof (decltype (roots_hashes)::value_type::second));
}

void celerix::confirm_req::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	// Write roots as: [ { root: ##, hash: ## } ,...]
	obs.write_range ("roots", roots_hashes, [] (auto const & root_hash, celerix::object_stream & obs) {
		auto [root, hash] = root_hash;
		obs.write ("root", root);
		obs.write ("hash", hash);
	});
}

/*
 * confirm_ack
 */

celerix::confirm_ack::confirm_ack (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a, celerix::network_filter::digest_t const & digest_a, celerix::vote_uniquer * uniquer_a) :
	message (header_a),
	vote{ celerix::make_shared<celerix::vote> (error_a, stream_a) },
	digest{ digest_a }
{
	if (!error_a && uniquer_a)
	{
		vote = uniquer_a->unique (vote);
	}
}

celerix::confirm_ack::confirm_ack (celerix::network_constants const & constants, std::shared_ptr<celerix::vote> const & vote_a, bool rebroadcasted_a) :
	message (constants, celerix::message_type::confirm_ack),
	vote{ vote_a }
{
	debug_assert (vote->hashes.size () < 256);

	header.block_type_set (celerix::block_type::not_a_block);
	header.flag_set (rebroadcasted_flag, rebroadcasted_a);

	if (vote->hashes.size () >= 16)
	{
		// Set v2 flag and use extended count if there are more than 15 hashes
		header.confirm_set_v2 (true);
		header.count_v2_set (static_cast<uint8_t> (vote->hashes.size ()));
	}
	else
	{
		header.count_set (static_cast<uint8_t> (vote->hashes.size ()));
	}
}

void celerix::confirm_ack::serialize (celerix::stream & stream_a) const
{
	header.serialize (stream_a);
	vote->serialize (stream_a);
}

bool celerix::confirm_ack::operator== (celerix::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void celerix::confirm_ack::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

uint8_t celerix::confirm_ack::hash_count (const celerix::message_header & header)
{
	if (header.confirm_is_v2 ())
	{
		return header.count_v2_get ();
	}
	else
	{
		return header.count_get ();
	}
}

std::size_t celerix::confirm_ack::size (const celerix::message_header & header)
{
	auto const count = hash_count (header);
	return celerix::vote::size (count);
}

bool celerix::confirm_ack::is_rebroadcasted () const
{
	return header.flag_test (rebroadcasted_flag);
}

void celerix::confirm_ack::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("vote", vote);
	obs.write ("rebroadcasted", is_rebroadcasted ());
}

/*
 * frontier_req
 */

celerix::frontier_req::frontier_req (celerix::network_constants const & constants) :
	message (constants, celerix::message_type::frontier_req)
{
}

celerix::frontier_req::frontier_req (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void celerix::frontier_req::serialize (celerix::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

bool celerix::frontier_req::deserialize (celerix::stream & stream_a)
{
	debug_assert (header.type == celerix::message_type::frontier_req);
	auto error (false);
	try
	{
		celerix::read (stream_a, start.bytes);
		celerix::read (stream_a, age);
		celerix::read (stream_a, count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void celerix::frontier_req::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool celerix::frontier_req::operator== (celerix::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

void celerix::frontier_req::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("start", start);
	obs.write ("age", age);
	obs.write ("count", count);
}

/*
 * bulk_pull
 */

celerix::bulk_pull::bulk_pull (celerix::network_constants const & constants) :
	message (constants, celerix::message_type::bulk_pull)
{
}

celerix::bulk_pull::bulk_pull (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void celerix::bulk_pull::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

void celerix::bulk_pull::serialize (celerix::stream & stream_a) const
{
	/*
	 * Ensure the "count_present" flag is set if there
	 * is a limit specifed.  Additionally, do not allow
	 * the "count_present" flag with a value of 0, since
	 * that is a sentinel which we use to mean "all blocks"
	 * and that is the behavior of not having the flag set
	 * so it is wasteful to do this.
	 */
	debug_assert ((count == 0 && !is_count_present ()) || (count != 0 && is_count_present ()));

	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);

	if (is_count_present ())
	{
		std::array<uint8_t, extended_parameters_size> count_buffer{ { 0 } };
		decltype (count) count_little_endian;
		static_assert (sizeof (count_little_endian) < (count_buffer.size () - 1), "count must fit within buffer");

		count_little_endian = boost::endian::native_to_little (count);
		memcpy (count_buffer.data () + 1, &count_little_endian, sizeof (count_little_endian));

		write (stream_a, count_buffer);
	}
}

bool celerix::bulk_pull::deserialize (celerix::stream & stream_a)
{
	debug_assert (header.type == celerix::message_type::bulk_pull);
	auto error (false);
	try
	{
		celerix::read (stream_a, start);
		celerix::read (stream_a, end);

		if (is_count_present ())
		{
			std::array<uint8_t, extended_parameters_size> extended_parameters_buffers;
			static_assert (sizeof (count) < (extended_parameters_buffers.size () - 1), "count must fit within buffer");

			celerix::read (stream_a, extended_parameters_buffers);
			if (extended_parameters_buffers.front () != 0)
			{
				error = true;
			}
			else
			{
				memcpy (&count, extended_parameters_buffers.data () + 1, sizeof (count));
				boost::endian::little_to_native_inplace (count);
			}
		}
		else
		{
			count = 0;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool celerix::bulk_pull::is_count_present () const
{
	return header.extensions.test (count_present_flag);
}

void celerix::bulk_pull::set_count_present (bool value_a)
{
	header.extensions.set (count_present_flag, value_a);
}

void celerix::bulk_pull::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("start", start);
	obs.write ("end", end);
	obs.write ("count", count);
}

/*
 * bulk_pull_account
 */

celerix::bulk_pull_account::bulk_pull_account (celerix::network_constants const & constants) :
	message (constants, celerix::message_type::bulk_pull_account)
{
}

celerix::bulk_pull_account::bulk_pull_account (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void celerix::bulk_pull_account::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

void celerix::bulk_pull_account::serialize (celerix::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

bool celerix::bulk_pull_account::deserialize (celerix::stream & stream_a)
{
	debug_assert (header.type == celerix::message_type::bulk_pull_account);
	auto error (false);
	try
	{
		celerix::read (stream_a, account);
		celerix::read (stream_a, minimum_amount);
		celerix::read (stream_a, flags);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void celerix::bulk_pull_account::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("account", account);
	obs.write ("minimum_amount", minimum_amount);
	obs.write ("flags", static_cast<uint8_t> (flags)); // TODO: Prettier flag printing
}

/*
 * bulk_push
 */

celerix::bulk_push::bulk_push (celerix::network_constants const & constants) :
	message (constants, celerix::message_type::bulk_push)
{
}

celerix::bulk_push::bulk_push (celerix::message_header const & header_a) :
	message (header_a)
{
}

bool celerix::bulk_push::deserialize (celerix::stream & stream_a)
{
	debug_assert (header.type == celerix::message_type::bulk_push);
	return false;
}

void celerix::bulk_push::serialize (celerix::stream & stream_a) const
{
	header.serialize (stream_a);
}

void celerix::bulk_push::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

void celerix::bulk_push::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data
}

/*
 * telemetry_req
 */

celerix::telemetry_req::telemetry_req (celerix::network_constants const & constants) :
	message (constants, celerix::message_type::telemetry_req)
{
}

celerix::telemetry_req::telemetry_req (celerix::message_header const & header_a) :
	message (header_a)
{
}

bool celerix::telemetry_req::deserialize (celerix::stream & stream_a)
{
	debug_assert (header.type == celerix::message_type::telemetry_req);
	return false;
}

void celerix::telemetry_req::serialize (celerix::stream & stream_a) const
{
	header.serialize (stream_a);
}

void celerix::telemetry_req::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.telemetry_req (*this);
}

void celerix::telemetry_req::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data
}

/*
 * telemetry_ack
 */

celerix::telemetry_ack::telemetry_ack (celerix::network_constants const & constants) :
	message (constants, celerix::message_type::telemetry_ack)
{
}

celerix::telemetry_ack::telemetry_ack (bool & error_a, celerix::stream & stream_a, celerix::message_header const & message_header) :
	message (message_header)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

celerix::telemetry_ack::telemetry_ack (celerix::network_constants const & constants, celerix::telemetry_data const & telemetry_data_a) :
	message (constants, celerix::message_type::telemetry_ack),
	data (telemetry_data_a)
{
	debug_assert (telemetry_data::size + telemetry_data_a.unknown_data.size () <= message_header::telemetry_size_mask.to_ulong ()); // Maximum size the mask allows
	header.extensions &= ~message_header::telemetry_size_mask;
	header.extensions |= std::bitset<16> (static_cast<unsigned long long> (telemetry_data::size) + telemetry_data_a.unknown_data.size ());
}

void celerix::telemetry_ack::serialize (celerix::stream & stream_a) const
{
	header.serialize (stream_a);
	if (!is_empty_payload ())
	{
		data.serialize (stream_a);
	}
}

bool celerix::telemetry_ack::deserialize (celerix::stream & stream_a)
{
	auto error (false);
	debug_assert (header.type == celerix::message_type::telemetry_ack);
	try
	{
		if (!is_empty_payload ())
		{
			data.deserialize (stream_a, celerix::narrow_cast<uint16_t> (header.extensions.to_ulong ()));
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void celerix::telemetry_ack::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.telemetry_ack (*this);
}

uint16_t celerix::telemetry_ack::size () const
{
	return size (header);
}

uint16_t celerix::telemetry_ack::size (celerix::message_header const & message_header_a)
{
	return static_cast<uint16_t> ((message_header_a.extensions & message_header::telemetry_size_mask).to_ullong ());
}

bool celerix::telemetry_ack::is_empty_payload () const
{
	return size () == 0;
}

void celerix::telemetry_ack::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	if (!is_empty_payload ())
	{
		obs.write ("data", data);
	}
}

/*
 * telemetry_data
 */

void celerix::telemetry_data::deserialize (celerix::stream & stream_a, uint16_t payload_length_a)
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

void celerix::telemetry_data::serialize_without_signature (celerix::stream & stream_a) const
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

void celerix::telemetry_data::serialize (celerix::stream & stream_a) const
{
	write (stream_a, signature);
	serialize_without_signature (stream_a);
}

celerix::error celerix::telemetry_data::serialize_json (celerix::jsonconfig & json, bool ignore_identification_metrics_a) const
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
	json.put ("active_difficulty", celerix::to_string_hex (active_difficulty));
	// Keep these last for UI purposes
	if (!ignore_identification_metrics_a)
	{
		json.put ("node_id", node_id.to_node_id ());
		json.put ("signature", signature.to_string ());
	}
	return json.get_error ();
}

celerix::error celerix::telemetry_data::deserialize_json (celerix::jsonconfig & json, bool ignore_identification_metrics_a)
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
	auto ec = celerix::from_string_hex (current_active_difficulty_text, active_difficulty);
	debug_assert (!ec);
	return json.get_error ();
}

bool celerix::telemetry_data::operator== (celerix::telemetry_data const & data_a) const
{
	return (signature == data_a.signature && node_id == data_a.node_id && block_count == data_a.block_count && cemented_count == data_a.cemented_count && unchecked_count == data_a.unchecked_count && account_count == data_a.account_count && bandwidth_cap == data_a.bandwidth_cap && uptime == data_a.uptime && peer_count == data_a.peer_count && protocol_version == data_a.protocol_version && genesis_block == data_a.genesis_block && major_version == data_a.major_version && minor_version == data_a.minor_version && patch_version == data_a.patch_version && pre_release_version == data_a.pre_release_version && maker == data_a.maker && timestamp == data_a.timestamp && active_difficulty == data_a.active_difficulty && unknown_data == data_a.unknown_data);
}

bool celerix::telemetry_data::operator!= (celerix::telemetry_data const & data_a) const
{
	return !(*this == data_a);
}

void celerix::telemetry_data::sign (celerix::keypair const & node_id_a)
{
	debug_assert (node_id == node_id_a.pub);
	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	signature = celerix::sign_message (node_id_a.prv, node_id_a.pub, bytes.data (), bytes.size ());
}

bool celerix::telemetry_data::validate_signature () const
{
	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	return celerix::validate_message (node_id, bytes.data (), bytes.size (), signature);
}

void celerix::telemetry_data::operator() (celerix::object_stream & obs) const
{
	// TODO: Telemetry data
}

/*
 * node_id_handshake
 */

celerix::node_id_handshake::node_id_handshake (bool & error_a, celerix::stream & stream_a, celerix::message_header const & header_a) :
	message (header_a)
{
	error_a = deserialize (stream_a);
}

celerix::node_id_handshake::node_id_handshake (celerix::network_constants const & constants, std::optional<query_payload> query_a, std::optional<response_payload> response_a) :
	message (constants, celerix::message_type::node_id_handshake),
	query{ query_a },
	response{ response_a }
{
	if (query)
	{
		header.flag_set (query_flag);
		header.flag_set (v2_flag); // Always indicate support for V2 handshake when querying, old peers will just ignore it
	}
	if (response)
	{
		header.flag_set (response_flag);
		header.flag_set (v2_flag, response->v2.has_value ()); // We only use V2 handshake when replying to peers that indicated support for it
	}
}

void celerix::node_id_handshake::serialize (celerix::stream & stream) const
{
	header.serialize (stream);
	if (query)
	{
		query->serialize (stream);
	}
	if (response)
	{
		response->serialize (stream);
	}
}

bool celerix::node_id_handshake::deserialize (celerix::stream & stream)
{
	debug_assert (header.type == celerix::message_type::node_id_handshake);
	bool error = false;
	try
	{
		if (is_query (header))
		{
			query_payload pld{};
			pld.deserialize (stream);
			query = pld;
		}

		if (is_response (header))
		{
			response_payload pld{};
			pld.deserialize (stream, header);
			response = pld;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

bool celerix::node_id_handshake::is_query (celerix::message_header const & header)
{
	debug_assert (header.type == celerix::message_type::node_id_handshake);
	bool result = header.extensions.test (query_flag);
	return result;
}

bool celerix::node_id_handshake::is_response (celerix::message_header const & header)
{
	debug_assert (header.type == celerix::message_type::node_id_handshake);
	bool result = header.extensions.test (response_flag);
	return result;
}

bool celerix::node_id_handshake::is_v2 (celerix::message_header const & header)
{
	debug_assert (header.type == celerix::message_type::node_id_handshake);
	bool result = header.extensions.test (v2_flag);
	return result;
}

bool celerix::node_id_handshake::is_v2 () const
{
	return is_v2 (header);
}

void celerix::node_id_handshake::visit (celerix::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

std::size_t celerix::node_id_handshake::size () const
{
	return size (header);
}

std::size_t celerix::node_id_handshake::size (celerix::message_header const & header)
{
	std::size_t result = 0;
	if (is_query (header))
	{
		result += query_payload::size;
	}
	if (is_response (header))
	{
		result += response_payload::size (header);
	}
	return result;
}

void celerix::node_id_handshake::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("query", query);
	obs.write ("response", response);
}

/*
 * node_id_handshake::query_payload
 */

void celerix::node_id_handshake::query_payload::serialize (celerix::stream & stream) const
{
	celerix::write (stream, cookie);
}

void celerix::node_id_handshake::query_payload::deserialize (celerix::stream & stream)
{
	celerix::read (stream, cookie);
}

void celerix::node_id_handshake::query_payload::operator() (celerix::object_stream & obs) const
{
	obs.write ("cookie", cookie);
}

/*
 * node_id_handshake::response_payload
 */

void celerix::node_id_handshake::response_payload::serialize (celerix::stream & stream) const
{
	if (v2)
	{
		celerix::write (stream, node_id);
		celerix::write (stream, v2->salt);
		celerix::write (stream, v2->genesis);
		celerix::write (stream, signature);
	}
	// TODO: Remove legacy handshake
	else
	{
		celerix::write (stream, node_id);
		celerix::write (stream, signature);
	}
}

void celerix::node_id_handshake::response_payload::deserialize (celerix::stream & stream, celerix::message_header const & header)
{
	if (is_v2 (header))
	{
		celerix::read (stream, node_id);
		v2_payload pld{};
		celerix::read (stream, pld.salt);
		celerix::read (stream, pld.genesis);
		v2 = pld;
		celerix::read (stream, signature);
	}
	else
	{
		celerix::read (stream, node_id);
		celerix::read (stream, signature);
	}
}

std::size_t celerix::node_id_handshake::response_payload::size (const celerix::message_header & header)
{
	return is_v2 (header) ? size_v2 : size_v1;
}

std::vector<uint8_t> celerix::node_id_handshake::response_payload::data_to_sign (const celerix::uint256_union & cookie) const
{
	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream stream{ bytes };

		if (v2)
		{
			celerix::write (stream, cookie);
			celerix::write (stream, v2->salt);
			celerix::write (stream, v2->genesis);
		}
		// TODO: Remove legacy handshake
		else
		{
			celerix::write (stream, cookie);
		}
	}
	return bytes;
}

void celerix::node_id_handshake::response_payload::sign (const celerix::uint256_union & cookie, celerix::keypair const & key)
{
	debug_assert (key.pub == node_id);
	auto data = data_to_sign (cookie);
	signature = celerix::sign_message (key.prv, key.pub, data.data (), data.size ());
	debug_assert (validate (cookie));
}

bool celerix::node_id_handshake::response_payload::validate (const celerix::uint256_union & cookie) const
{
	auto data = data_to_sign (cookie);
	if (celerix::validate_message (node_id, data.data (), data.size (), signature)) // true => error
	{
		return false; // Fail
	}
	return true; // OK
}

void celerix::node_id_handshake::response_payload::operator() (celerix::object_stream & obs) const
{
	obs.write ("node_id", node_id);
	obs.write ("signature", signature);

	obs.write ("v2", v2.has_value ());
	if (v2)
	{
		obs.write ("salt", v2->salt);
		obs.write ("genesis", v2->genesis);
	}
}

/*
 * asc_pull_req
 */

celerix::asc_pull_req::asc_pull_req (const celerix::network_constants & constants) :
	message (constants, celerix::message_type::asc_pull_req)
{
}

celerix::asc_pull_req::asc_pull_req (bool & error, celerix::stream & stream, const celerix::message_header & header) :
	message (header)
{
	error = deserialize (stream);
}

void celerix::asc_pull_req::visit (celerix::message_visitor & visitor) const
{
	visitor.asc_pull_req (*this);
}

void celerix::asc_pull_req::serialize (celerix::stream & stream) const
{
	header.serialize (stream);
	celerix::write (stream, type);
	celerix::write_big_endian (stream, id);

	serialize_payload (stream);
}

bool celerix::asc_pull_req::deserialize (celerix::stream & stream)
{
	debug_assert (header.type == celerix::message_type::asc_pull_req);
	bool error = false;
	try
	{
		celerix::read (stream, type);
		celerix::read_big_endian (stream, id);

		deserialize_payload (stream);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void celerix::asc_pull_req::serialize_payload (celerix::stream & stream) const
{
	debug_assert (verify_consistency ());

	std::visit ([&stream] (auto && pld) { pld.serialize (stream); }, payload);
}

void celerix::asc_pull_req::deserialize_payload (celerix::stream & stream)
{
	switch (type)
	{
		case asc_pull_type::blocks:
		{
			blocks_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::account_info:
		{
			account_info_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::frontiers:
		{
			frontiers_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		default:
			throw std::runtime_error ("Unknown asc_pull_type");
	}
}

void celerix::asc_pull_req::update_header ()
{
	// TODO: Avoid serializing the payload twice
	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream payload_stream (bytes);
		serialize_payload (payload_stream);
	}
	debug_assert (bytes.size () <= std::numeric_limits<uint16_t>::max ()); // Max uint16 for storing size
	debug_assert (bytes.size () >= 1);
	header.extensions = std::bitset<16> (bytes.size ());
}

std::size_t celerix::asc_pull_req::size (const celerix::message_header & header)
{
	uint16_t payload_length = celerix::narrow_cast<uint16_t> (header.extensions.to_ulong ());
	return partial_size + payload_length;
}

bool celerix::asc_pull_req::verify_consistency () const
{
	struct consistency_visitor
	{
		celerix::asc_pull_type type;

		void operator() (empty_payload) const
		{
			debug_assert (false, "missing payload");
		}
		void operator() (blocks_payload) const
		{
			debug_assert (type == asc_pull_type::blocks);
		}
		void operator() (account_info_payload) const
		{
			debug_assert (type == asc_pull_type::account_info);
		}
		void operator() (frontiers_payload) const
		{
			debug_assert (type == asc_pull_type::frontiers);
		}
	};
	std::visit (consistency_visitor{ type }, payload);
	return true; // Just for convenience of calling from asserts
}

void celerix::asc_pull_req::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("type", type);
	obs.write ("id", id);

	std::visit ([&obs] (auto && pld) { pld (obs); }, payload); // Log payload
}

/*
 * asc_pull_req::blocks_payload
 */

void celerix::asc_pull_req::blocks_payload::serialize (celerix::stream & stream) const
{
	celerix::write (stream, start);
	celerix::write (stream, count);
	celerix::write (stream, start_type);
}

void celerix::asc_pull_req::blocks_payload::deserialize (celerix::stream & stream)
{
	celerix::read (stream, start);
	celerix::read (stream, count);
	celerix::read (stream, start_type);
}

void celerix::asc_pull_req::blocks_payload::operator() (celerix::object_stream & obs) const
{
	obs.write ("start", start);
	obs.write ("start_type", start_type);
	obs.write ("count", count);
}

/*
 * asc_pull_req::account_info_payload
 */

void celerix::asc_pull_req::account_info_payload::serialize (stream & stream) const
{
	celerix::write (stream, target);
	celerix::write (stream, target_type);
}

void celerix::asc_pull_req::account_info_payload::deserialize (stream & stream)
{
	celerix::read (stream, target);
	celerix::read (stream, target_type);
}

void celerix::asc_pull_req::account_info_payload::operator() (celerix::object_stream & obs) const
{
	obs.write ("target", target);
	obs.write ("target_type", target_type);
}

/*
 * asc_pull_req::frontiers_payload
 */

void celerix::asc_pull_req::frontiers_payload::serialize (celerix::stream & stream) const
{
	celerix::write (stream, start);
	celerix::write_big_endian (stream, count);
}

void celerix::asc_pull_req::frontiers_payload::deserialize (celerix::stream & stream)
{
	celerix::read (stream, start);
	celerix::read_big_endian (stream, count);
}

void celerix::asc_pull_req::frontiers_payload::operator() (celerix::object_stream & obs) const
{
	obs.write ("start", start);
	obs.write ("count", count);
}

/*
 * asc_pull_ack
 */

celerix::asc_pull_ack::asc_pull_ack (const celerix::network_constants & constants) :
	message (constants, celerix::message_type::asc_pull_ack)
{
}

celerix::asc_pull_ack::asc_pull_ack (bool & error, celerix::stream & stream, const celerix::message_header & header) :
	message (header)
{
	error = deserialize (stream);
}

void celerix::asc_pull_ack::visit (celerix::message_visitor & visitor) const
{
	visitor.asc_pull_ack (*this);
}

void celerix::asc_pull_ack::serialize (celerix::stream & stream) const
{
	debug_assert (header.extensions.to_ulong () > 0); // Block payload must have at least `not_a_block` terminator
	header.serialize (stream);
	celerix::write (stream, type);
	celerix::write_big_endian (stream, id);

	serialize_payload (stream);
}

bool celerix::asc_pull_ack::deserialize (celerix::stream & stream)
{
	debug_assert (header.type == celerix::message_type::asc_pull_ack);
	bool error = false;
	try
	{
		celerix::read (stream, type);
		celerix::read_big_endian (stream, id);

		deserialize_payload (stream);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void celerix::asc_pull_ack::serialize_payload (celerix::stream & stream) const
{
	debug_assert (verify_consistency ());

	std::visit ([&stream] (auto && pld) { pld.serialize (stream); }, payload);
}

void celerix::asc_pull_ack::deserialize_payload (celerix::stream & stream)
{
	switch (type)
	{
		case asc_pull_type::blocks:
		{
			blocks_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::account_info:
		{
			account_info_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::frontiers:
		{
			frontiers_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		default:
			throw std::runtime_error ("Unknown asc_pull_type");
	}
}

void celerix::asc_pull_ack::update_header ()
{
	// TODO: Avoid serializing the payload twice
	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream payload_stream (bytes);
		serialize_payload (payload_stream);
	}
	debug_assert (bytes.size () <= std::numeric_limits<uint16_t>::max ()); // Max uint16 for storing size
	debug_assert (bytes.size () >= 1);
	header.extensions = std::bitset<16> (bytes.size ());
}

std::size_t celerix::asc_pull_ack::size (const celerix::message_header & header)
{
	uint16_t payload_length = celerix::narrow_cast<uint16_t> (header.extensions.to_ulong ());
	return partial_size + payload_length;
}

bool celerix::asc_pull_ack::verify_consistency () const
{
	struct consistency_visitor
	{
		celerix::asc_pull_type type;

		void operator() (empty_payload) const
		{
			debug_assert (false, "missing payload");
		}
		void operator() (blocks_payload) const
		{
			debug_assert (type == asc_pull_type::blocks);
		}
		void operator() (account_info_payload) const
		{
			debug_assert (type == asc_pull_type::account_info);
		}
		void operator() (frontiers_payload) const
		{
			debug_assert (type == asc_pull_type::frontiers);
		}
	};
	std::visit (consistency_visitor{ type }, payload);
	return true; // Just for convenience of calling from asserts
}

void celerix::asc_pull_ack::operator() (celerix::object_stream & obs) const
{
	celerix::message::operator() (obs); // Write common data

	obs.write ("type", type);
	obs.write ("id", id);

	std::visit ([&obs] (auto && pld) { pld (obs); }, payload); // Log payload
}

/*
 * asc_pull_ack::blocks_payload
 */

void celerix::asc_pull_ack::blocks_payload::serialize (celerix::stream & stream) const
{
	debug_assert (blocks.size () <= max_blocks);

	for (auto & block : blocks)
	{
		debug_assert (block != nullptr);
		celerix::serialize_block (stream, *block);
	}
	// For convenience, end with null block terminator
	celerix::write (stream, celerix::block_type::not_a_block);
}

void celerix::asc_pull_ack::blocks_payload::deserialize (celerix::stream & stream)
{
	auto current = celerix::deserialize_block (stream);
	while (current && blocks.size () < max_blocks)
	{
		blocks.push_back (current);
		current = celerix::deserialize_block (stream);
	}
}

void celerix::asc_pull_ack::blocks_payload::operator() (celerix::object_stream & obs) const
{
	obs.write_range ("blocks", blocks);
}

/*
 * asc_pull_ack::account_info_payload
 */

void celerix::asc_pull_ack::account_info_payload::serialize (celerix::stream & stream) const
{
	celerix::write (stream, account);
	celerix::write (stream, account_open);
	celerix::write (stream, account_head);
	celerix::write_big_endian (stream, account_block_count);
	celerix::write (stream, account_conf_frontier);
	celerix::write_big_endian (stream, account_conf_height);
}

void celerix::asc_pull_ack::account_info_payload::deserialize (celerix::stream & stream)
{
	celerix::read (stream, account);
	celerix::read (stream, account_open);
	celerix::read (stream, account_head);
	celerix::read_big_endian (stream, account_block_count);
	celerix::read (stream, account_conf_frontier);
	celerix::read_big_endian (stream, account_conf_height);
}

void celerix::asc_pull_ack::account_info_payload::operator() (celerix::object_stream & obs) const
{
	obs.write ("account", account);
	obs.write ("open", account_open);
	obs.write ("head", account_head);
	obs.write ("block_count", account_block_count);
	obs.write ("conf_frontier", account_conf_frontier);
	obs.write ("conf_height", account_conf_height);
}

/*
 * asc_pull_ack::frontiers_payload
 */

void celerix::asc_pull_ack::frontiers_payload::serialize_frontier (celerix::stream & stream, celerix::asc_pull_ack::frontiers_payload::frontier const & frontier)
{
	auto const & [account, hash] = frontier;
	celerix::write (stream, account);
	celerix::write (stream, hash);
}

celerix::asc_pull_ack::frontiers_payload::frontier celerix::asc_pull_ack::frontiers_payload::deserialize_frontier (celerix::stream & stream)
{
	celerix::account account;
	celerix::block_hash hash;
	celerix::read (stream, account);
	celerix::read (stream, hash);
	return { account, hash };
}

void celerix::asc_pull_ack::frontiers_payload::serialize (celerix::stream & stream) const
{
	debug_assert (frontiers.size () <= max_frontiers);

	for (auto const & frontier : frontiers)
	{
		serialize_frontier (stream, frontier);
	}
	serialize_frontier (stream, { celerix::account{ 0 }, celerix::block_hash{ 0 } });
}

void celerix::asc_pull_ack::frontiers_payload::deserialize (celerix::stream & stream)
{
	auto current = deserialize_frontier (stream);
	while ((!current.first.is_zero () && !current.second.is_zero ()) && frontiers.size () < max_frontiers)
	{
		frontiers.push_back (current);
		current = deserialize_frontier (stream);
	}
}

void celerix::asc_pull_ack::frontiers_payload::operator() (celerix::object_stream & obs) const
{
	obs.write_range ("frontiers", frontiers, [] (auto const & entry, celerix::object_stream & obs) {
		auto & [account, hash] = entry;
		obs.write ("account", account);
		obs.write ("hash", hash);
	});
}

/*
 *
 */

std::string_view celerix::to_string (celerix::message_type type)
{
	return celerix::enum_util::name (type);
}

celerix::stat::detail celerix::to_stat_detail (celerix::message_type type)
{
	return celerix::enum_util::cast<celerix::stat::detail> (type);
}

celerix::log::detail celerix::to_log_detail (celerix::message_type type)
{
	return celerix::enum_util::cast<celerix::log::detail> (type);
}
