#include <nano/lib/memory.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/message_visitor.hpp>
#include <nano/messages/vote_relay.hpp>

namespace nano::messages
{
/*
 * vote_relay_req
 */

vote_relay_req::vote_relay_req (nano::network_constants const & constants) :
	message (constants, message_type::vote_relay_req)
{
}

vote_relay_req::vote_relay_req (nano::network_constants const & constants, id_t id_a, std::vector<std::pair<nano::block_hash, nano::root>> const & roots_hashes_a, std::vector<nano::account> const & reps_a, bool include_non_final_a) :
	message (constants, message_type::vote_relay_req),
	id{ id_a },
	roots_hashes{ roots_hashes_a },
	reps{ reps_a }
{
	debug_assert (roots_hashes.size () > 0);
	debug_assert (roots_hashes.size () <= max_hashes);
	debug_assert (reps.size () <= max_reps);

	if (include_non_final_a)
	{
		flags |= flag_include_non_final;
	}

	update_header ();
}

bool vote_relay_req::include_non_final () const
{
	return (flags & flag_include_non_final) != 0;
}

vote_relay_req::vote_relay_req (bool & error, nano::stream & stream, message_header const & header_a) :
	message (header_a)
{
	error = deserialize (stream);
}

void vote_relay_req::visit (message_visitor & visitor) const
{
	visitor.vote_relay_req (*this);
}

void vote_relay_req::serialize (nano::stream & stream) const
{
	debug_assert (roots_hashes.size () <= max_hashes);
	debug_assert (reps.size () <= max_reps);

	header.serialize (stream);
	nano::write_big_endian (stream, id);
	nano::write (stream, flags);
	nano::write (stream, nano::narrow_cast<uint8_t> (roots_hashes.size ()));
	for (auto const & [hash, root] : roots_hashes)
	{
		nano::write (stream, hash);
		nano::write (stream, root);
	}
	nano::write (stream, nano::narrow_cast<uint8_t> (reps.size ()));
	for (auto const & rep : reps)
	{
		nano::write (stream, rep);
	}
}

bool vote_relay_req::deserialize (nano::stream & stream)
{
	debug_assert (header.type == message_type::vote_relay_req);
	bool error = false;
	try
	{
		nano::read_big_endian (stream, id);
		nano::read (stream, flags);
		uint8_t count{ 0 };
		nano::read (stream, count);
		if (count == 0 || count > max_hashes)
		{
			return true;
		}
		roots_hashes.reserve (count);
		for (uint8_t i = 0; i < count; ++i)
		{
			nano::block_hash hash;
			nano::read (stream, hash);
			nano::root root;
			nano::read (stream, root);
			roots_hashes.emplace_back (hash, root);
		}
		uint8_t reps_count{ 0 };
		nano::read (stream, reps_count);
		if (reps_count > max_reps)
		{
			return true;
		}
		reps.reserve (reps_count);
		for (uint8_t i = 0; i < reps_count; ++i)
		{
			nano::account rep;
			nano::read (stream, rep);
			reps.push_back (rep);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

std::size_t vote_relay_req::payload_size () const
{
	return sizeof (id) + sizeof (flags) + sizeof (uint8_t) + roots_hashes.size () * (sizeof (nano::block_hash) + sizeof (nano::root)) + sizeof (uint8_t) + reps.size () * sizeof (nano::account);
}

void vote_relay_req::update_header ()
{
	auto const size = payload_size ();
	debug_assert (size <= std::numeric_limits<uint16_t>::max ()); // Max uint16 for storing size
	header.extensions = std::bitset<16> (size);
}

std::size_t vote_relay_req::size (message_header const & header)
{
	// Payload length is stored in the header extensions
	return nano::narrow_cast<uint16_t> (header.extensions.to_ulong ());
}

void vote_relay_req::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("id", id);
	obs.write ("include_non_final", include_non_final ());
	obs.write ("count", roots_hashes.size ());
	obs.write ("reps", reps.size ());
}

/*
 * vote_relay_ack
 */

vote_relay_ack::vote_relay_ack (nano::network_constants const & constants) :
	message (constants, message_type::vote_relay_ack)
{
}

vote_relay_ack::vote_relay_ack (nano::network_constants const & constants, id_t id_a, std::vector<std::shared_ptr<nano::vote>> const & votes_a) :
	message (constants, message_type::vote_relay_ack),
	id{ id_a },
	votes{ votes_a }
{
	debug_assert (votes.size () > 0);
	debug_assert (votes.size () <= max_votes);

	update_header ();
}

vote_relay_ack::vote_relay_ack (bool & error, nano::stream & stream, message_header const & header_a, nano::vote_uniquer * uniquer) :
	message (header_a)
{
	error = deserialize (stream, uniquer);
}

void vote_relay_ack::visit (message_visitor & visitor) const
{
	visitor.vote_relay_ack (*this);
}

void vote_relay_ack::serialize (nano::stream & stream) const
{
	debug_assert (votes.size () <= max_votes);

	header.serialize (stream);
	nano::write_big_endian (stream, id);
	nano::write (stream, nano::narrow_cast<uint8_t> (votes.size ()));
	for (auto const & vote : votes)
	{
		nano::write (stream, nano::narrow_cast<uint8_t> (vote->hashes.size ()));
		vote->serialize (stream);
	}
}

bool vote_relay_ack::deserialize (nano::stream & stream, nano::vote_uniquer * uniquer)
{
	debug_assert (header.type == message_type::vote_relay_ack);
	bool error = false;
	try
	{
		nano::read_big_endian (stream, id);
		uint8_t count{ 0 };
		nano::read (stream, count);
		if (count > max_votes)
		{
			return true;
		}
		votes.reserve (count);
		for (uint8_t i = 0; i < count && !error; ++i)
		{
			uint8_t hash_count{ 0 };
			nano::read (stream, hash_count);
			auto vote = nano::make_shared<nano::vote> (error, stream, hash_count);
			if (!error)
			{
				if (uniquer)
				{
					vote = uniquer->unique (vote);
				}
				votes.push_back (vote);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

std::size_t vote_relay_ack::vote_size (std::shared_ptr<nano::vote> const & vote)
{
	return sizeof (uint8_t) + nano::vote::size (nano::narrow_cast<uint8_t> (vote->hashes.size ()));
}

std::size_t vote_relay_ack::payload_size () const
{
	std::size_t result = sizeof (id) + sizeof (uint8_t);
	for (auto const & vote : votes)
	{
		result += vote_size (vote);
	}
	return result;
}

void vote_relay_ack::update_header ()
{
	auto const size = payload_size ();
	debug_assert (size <= max_payload); // Max uint16 for storing size
	header.extensions = std::bitset<16> (size);
}

std::size_t vote_relay_ack::size (message_header const & header)
{
	// Payload length is stored in the header extensions
	return nano::narrow_cast<uint16_t> (header.extensions.to_ulong ());
}

void vote_relay_ack::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("id", id);
	obs.write ("count", votes.size ());
}
}
