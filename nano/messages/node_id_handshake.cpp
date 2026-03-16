#include <nano/lib/keypair.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/messages/message_visitor.hpp>
#include <nano/messages/node_id_handshake.hpp>

namespace nano::messages
{
/*
 * node_id_handshake
 */

node_id_handshake::node_id_handshake (bool & error_a, nano::stream & stream_a, message_header const & header_a) :
	message (header_a)
{
	error_a = deserialize (stream_a);
}

node_id_handshake::node_id_handshake (nano::network_constants const & constants, std::optional<query_payload> query_a, std::optional<response_payload> response_a) :
	message (constants, message_type::node_id_handshake),
	query{ query_a },
	response{ response_a }
{
	if (query)
	{
		header.flag_set (query_flag);
		header.flag_set (v2_flag); // Always indicate support for V2 handshake when querying, old peers will just ignore it
		header.flag_set (v3_flag); // Always indicate support for V3 handshake when querying, old peers will just ignore it
	}
	if (response)
	{
		header.flag_set (response_flag);
		// Response version takes precedence over query version flags
		header.flag_set (v2_flag, false);
		header.flag_set (v3_flag, false);
		std::visit (nano::overloaded{
					[&] (response_payload::v3_payload const &) {
						header.flag_set (v3_flag);
					},
					[&] (response_payload::v2_payload const &) {
						header.flag_set (v2_flag);
					},
					[] (std::monostate) {},
					},
		response->ext);
	}
}

void node_id_handshake::serialize (nano::stream & stream) const
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

bool node_id_handshake::deserialize (nano::stream & stream)
{
	debug_assert (header.type == message_type::node_id_handshake);
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

bool node_id_handshake::is_query (message_header const & header)
{
	debug_assert (header.type == message_type::node_id_handshake);
	bool result = header.extensions.test (query_flag);
	return result;
}

bool node_id_handshake::is_response (message_header const & header)
{
	debug_assert (header.type == message_type::node_id_handshake);
	bool result = header.extensions.test (response_flag);
	return result;
}

handshake_version node_id_handshake::version (message_header const & header)
{
	debug_assert (header.type == message_type::node_id_handshake);
	if (header.extensions.test (v3_flag))
	{
		return handshake_version::v3;
	}
	if (header.extensions.test (v2_flag))
	{
		return handshake_version::v2;
	}
	return handshake_version::v1;
}

handshake_version node_id_handshake::version () const
{
	return version (header);
}

void node_id_handshake::visit (message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

std::size_t node_id_handshake::size () const
{
	return size (header);
}

std::size_t node_id_handshake::size (message_header const & header)
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

void node_id_handshake::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("query", query);
	obs.write ("response", response);
}

/*
 * node_id_handshake::query_payload
 */

void node_id_handshake::query_payload::serialize (nano::stream & stream) const
{
	nano::write (stream, cookie);
}

void node_id_handshake::query_payload::deserialize (nano::stream & stream)
{
	nano::read (stream, cookie);
}

void node_id_handshake::query_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("cookie", cookie);
}

/*
 * node_id_handshake::response_payload
 */

void node_id_handshake::response_payload::serialize (nano::stream & stream) const
{
	nano::write (stream, node_id);
	std::visit (nano::overloaded{
				[&] (v3_payload const & p) {
					nano::write (stream, p.salt);
					nano::write (stream, p.genesis);
					nano::write_big_endian (stream, p.flags.underlying ());
					nano::write_big_endian (stream, p.reserved);
				},
				[&] (v2_payload const & p) {
					nano::write (stream, p.salt);
					nano::write (stream, p.genesis);
				},
				[] (std::monostate) {},
				},
	ext);
	nano::write (stream, signature);
}

void node_id_handshake::response_payload::deserialize (nano::stream & stream, message_header const & header)
{
	nano::read (stream, node_id);
	switch (version (header))
	{
		case handshake_version::v3:
		{
			v3_payload pld{};
			nano::read (stream, pld.salt);
			nano::read (stream, pld.genesis);
			nano::read_big_endian (stream, pld.flags.underlying ());
			nano::read_big_endian (stream, pld.reserved);
			ext = pld;
		}
		break;
		case handshake_version::v2:
		{
			v2_payload pld{};
			nano::read (stream, pld.salt);
			nano::read (stream, pld.genesis);
			ext = pld;
		}
		break;
		case handshake_version::v1:
			break;
	}
	nano::read (stream, signature);
}

std::size_t node_id_handshake::response_payload::size (const message_header & header)
{
	switch (node_id_handshake::version (header))
	{
		case handshake_version::v3:
			return size_v3;
		case handshake_version::v2:
			return size_v2;
		case handshake_version::v1:
			return size_v1;
	}
	return size_v1;
}

std::vector<uint8_t> node_id_handshake::response_payload::data_to_sign (const nano::uint256_union & cookie) const
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		nano::write (stream, cookie);

		std::visit (nano::overloaded{
					[&] (v3_payload const & p) {
						nano::write (stream, p.salt);
						nano::write (stream, p.genesis);
						nano::write_big_endian (stream, p.flags.underlying ());
						nano::write_big_endian (stream, p.reserved);
					},
					[&] (v2_payload const & p) {
						nano::write (stream, p.salt);
						nano::write (stream, p.genesis);
					},
					[] (std::monostate) {},
					},
		ext);
	}
	return bytes;
}

std::optional<nano::block_hash> node_id_handshake::response_payload::genesis () const
{
	return std::visit (nano::overloaded{
					   [] (v3_payload const & p) -> std::optional<nano::block_hash> { return p.genesis; },
					   [] (v2_payload const & p) -> std::optional<nano::block_hash> { return p.genesis; },
					   [] (std::monostate) -> std::optional<nano::block_hash> { return std::nullopt; },
					   },
	ext);
}

nano::node_capabilities_flags node_id_handshake::response_payload::flags () const
{
	return std::visit (nano::overloaded{
					   [] (v3_payload const & p) -> nano::node_capabilities_flags { return p.flags; },
					   [] (auto const &) -> nano::node_capabilities_flags { return {}; },
					   },
	ext);
}

void node_id_handshake::response_payload::sign (const nano::uint256_union & cookie, nano::keypair const & key)
{
	debug_assert (key.pub == node_id);
	auto data = data_to_sign (cookie);
	signature = nano::sign_message (key.prv, key.pub, data.data (), data.size ());
	debug_assert (validate (cookie));
}

bool node_id_handshake::response_payload::validate (const nano::uint256_union & cookie) const
{
	auto data = data_to_sign (cookie);
	if (nano::validate_message (node_id, data.data (), data.size (), signature)) // true => error
	{
		return false; // Fail
	}
	return true; // OK
}

void node_id_handshake::response_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("node_id", node_id);
	obs.write ("signature", signature);

	std::visit (nano::overloaded{
				[&] (v3_payload const & p) {
					obs.write ("version", 3);
					obs.write ("salt", p.salt);
					obs.write ("genesis", p.genesis);
					obs.write ("capabilities", p.flags);
				},
				[&] (v2_payload const & p) {
					obs.write ("version", 2);
					obs.write ("salt", p.salt);
					obs.write ("genesis", p.genesis);
				},
				[&] (std::monostate) {
					obs.write ("version", 1);
				},
				},
	ext);
}
}
