#include <nano/lib/memory.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/message_visitor.hpp>

namespace nano::messages
{
/*
 * confirm_req
 */

confirm_req::confirm_req (bool & error_a, nano::stream & stream_a, message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

confirm_req::confirm_req (nano::network_constants const & constants, std::vector<std::pair<nano::block_hash, nano::root>> const & roots_hashes_a) :
	message (constants, message_type::confirm_req),
	roots_hashes (roots_hashes_a)
{
	debug_assert (!roots_hashes.empty ());
	debug_assert (roots_hashes.size () < 256);

	// Set `not_a_block` (1) block type for hashes + roots request
	// This is needed to keep compatibility with previous protocol versions (<= V25.1)
	header.block_type_set (nano::block_type::not_a_block);

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

confirm_req::confirm_req (nano::network_constants const & constants, nano::block_hash const & hash_a, nano::root const & root_a) :
	confirm_req (constants, std::vector<std::pair<nano::block_hash, nano::root>>{ { hash_a, root_a } })
{
}

void confirm_req::visit (message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void confirm_req::serialize (nano::stream & stream_a) const
{
	debug_assert (!roots_hashes.empty ());

	header.serialize (stream_a);

	// Write hashes & roots
	for (auto & root_hash : roots_hashes)
	{
		nano::write (stream_a, root_hash.first);
		nano::write (stream_a, root_hash.second);
	}
}

bool confirm_req::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == message_type::confirm_req);

	bool result = false;
	try
	{
		uint8_t const count = hash_count (header);
		for (auto i (0); i != count && !result; ++i)
		{
			nano::block_hash block_hash (0);
			nano::block_hash root (0);
			nano::read (stream_a, block_hash);
			nano::read (stream_a, root);
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

bool confirm_req::operator== (confirm_req const & other_a) const
{
	bool equal (false);
	if (!roots_hashes.empty () && !other_a.roots_hashes.empty ())
	{
		equal = roots_hashes == other_a.roots_hashes;
	}
	return equal;
}

uint8_t confirm_req::hash_count (const message_header & header)
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

std::size_t confirm_req::size (message_header const & header)
{
	auto const count = hash_count (header);
	return count * (sizeof (decltype (roots_hashes)::value_type::first) + sizeof (decltype (roots_hashes)::value_type::second));
}

void confirm_req::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	// Write roots as: [ { root: ##, hash: ## } ,...]
	obs.write_range ("roots", roots_hashes, [] (auto const & root_hash, nano::object_stream & obs) {
		auto [root, hash] = root_hash;
		obs.write ("root", root);
		obs.write ("hash", hash);
	});
}

/*
 * confirm_ack
 */

confirm_ack::confirm_ack (bool & error_a, nano::stream & stream_a, message_header const & header_a, nano::network_filter::digest_t const & digest_a, nano::vote_uniquer * uniquer_a) :
	message (header_a),
	vote{ nano::make_shared<nano::vote> (error_a, stream_a) },
	digest{ digest_a }
{
	if (!error_a && uniquer_a)
	{
		vote = uniquer_a->unique (vote);
	}
}

confirm_ack::confirm_ack (nano::network_constants const & constants, std::shared_ptr<nano::vote> const & vote_a, bool rebroadcasted_a) :
	message (constants, message_type::confirm_ack),
	vote{ vote_a }
{
	debug_assert (vote->hashes.size () < 256);

	header.block_type_set (nano::block_type::not_a_block);
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

void confirm_ack::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	vote->serialize (stream_a);
}

bool confirm_ack::operator== (confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void confirm_ack::visit (message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

uint8_t confirm_ack::hash_count (const message_header & header)
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

std::size_t confirm_ack::size (const message_header & header)
{
	auto const count = hash_count (header);
	return nano::vote::size (count);
}

bool confirm_ack::is_rebroadcasted () const
{
	return header.flag_test (rebroadcasted_flag);
}

void confirm_ack::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("vote", vote);
	obs.write ("rebroadcasted", is_rebroadcasted ());
}
}
