#include <celerix/lib/stream.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/vote.hpp>

#include <boost/property_tree/json_parser.hpp>

celerix::vote::vote (bool & error_a, celerix::stream & stream_a)
{
	error_a = deserialize (stream_a);
}

celerix::vote::vote (celerix::account const & account_a, celerix::raw_key const & prv_a, uint64_t timestamp_a, uint8_t duration, std::vector<celerix::block_hash> const & hashes) :
	hashes{ hashes },
	timestamp_m{ packed_timestamp (timestamp_a, duration) },
	account{ account_a }
{
	debug_assert (hashes.size () <= max_hashes);

	signature = celerix::sign_message (prv_a, account_a, hash ());
}

void celerix::vote::serialize (celerix::stream & stream_a) const
{
	debug_assert (hashes.size () <= max_hashes);

	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_little (timestamp_m));
	for (auto const & hash : hashes)
	{
		write (stream_a, hash);
	}
}

bool celerix::vote::deserialize (celerix::stream & stream_a)
{
	auto error = false;
	try
	{
		celerix::read (stream_a, account.bytes);
		celerix::read (stream_a, signature.bytes);
		celerix::read (stream_a, timestamp_m);

		while (stream_a.in_avail () > 0 && hashes.size () < max_hashes)
		{
			celerix::block_hash block_hash;
			celerix::read (stream_a, block_hash);
			hashes.push_back (block_hash);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

std::size_t celerix::vote::size (uint8_t count)
{
	debug_assert (count <= max_hashes);
	return partial_size + count * sizeof (celerix::block_hash);
}

std::string const celerix::vote::hash_prefix = "vote ";

celerix::block_hash celerix::vote::hash () const
{
	celerix::block_hash result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	blake2b_update (&hash, hash_prefix.data (), hash_prefix.size ());
	for (auto const & block_hash : hashes)
	{
		blake2b_update (&hash, block_hash.bytes.data (), sizeof (block_hash.bytes));
	}
	union
	{
		uint64_t qword;
		std::array<uint8_t, 8> bytes;
	};
	qword = timestamp_m;
	blake2b_update (&hash, bytes.data (), sizeof (bytes));
	blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
	return result;
}

celerix::block_hash celerix::vote::full_hash () const
{
	celerix::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ().bytes));
	blake2b_update (&state, account.bytes.data (), sizeof (account.bytes.data ()));
	blake2b_update (&state, signature.bytes.data (), sizeof (signature.bytes.data ()));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

bool celerix::vote::validate () const
{
	return celerix::validate_message (account, hash (), signature);
}

bool celerix::vote::operator== (celerix::vote const & other_a) const
{
	return timestamp_m == other_a.timestamp_m && hashes == other_a.hashes && account == other_a.account && signature == other_a.signature;
}

bool celerix::vote::operator!= (celerix::vote const & other_a) const
{
	return !(*this == other_a);
}

/**
 * Returns the timestamp of the vote (with the duration bits masked, set to zero)
 * If it is a final vote, all the bits including duration bits are returned as they are, all FF
 */
uint64_t celerix::vote::timestamp () const
{
	return (timestamp_m == std::numeric_limits<uint64_t>::max ())
	? timestamp_m // final vote
	: (timestamp_m & timestamp_mask);
}

uint8_t celerix::vote::duration_bits () const
{
	// Duration field is specified in the 4 low-order bits of the timestamp.
	// This makes the timestamp have a minimum granularity of 16ms
	// The duration is specified as 2^(duration + 4) giving it a range of 16-524,288ms in power of two increments
	auto result = timestamp_m & ~timestamp_mask;
	debug_assert (result < 16);
	return static_cast<uint8_t> (result);
}

std::chrono::milliseconds celerix::vote::duration () const
{
	return std::chrono::milliseconds{ 1u << (duration_bits () + 4) };
}

bool celerix::vote::is_final () const
{
	return is_final_timestamp (timestamp_m);
}

void celerix::vote::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("account", account.to_account ());
	tree.put ("signature", signature.number ());
	tree.put ("sequence", std::to_string (timestamp ()));
	tree.put ("timestamp", std::to_string (timestamp ()));
	tree.put ("duration", std::to_string (duration_bits ()));
	boost::property_tree::ptree blocks_tree;
	for (auto const & hash : hashes)
	{
		boost::property_tree::ptree entry;
		entry.put ("", hash.to_string ());
		blocks_tree.push_back (std::make_pair ("", entry));
	}
	tree.add_child ("blocks", blocks_tree);
}

std::string celerix::vote::to_json () const
{
	std::stringstream stream;
	boost::property_tree::ptree tree;
	serialize_json (tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

std::string celerix::vote::hashes_string () const
{
	return celerix::util::join (hashes, ", ", [] (auto const & hash) {
		return hash.to_string ();
	});
}

uint64_t celerix::vote::packed_timestamp (uint64_t timestamp, uint8_t duration)
{
	debug_assert (duration <= duration_max && "Invalid duration");
	debug_assert ((!(timestamp == timestamp_max) || (duration == duration_max)) && "Invalid final vote");
	return (timestamp & timestamp_mask) | duration;
}

bool celerix::vote::is_final_timestamp (uint64_t timestamp)
{
	return timestamp == std::numeric_limits<uint64_t>::max ();
}

void celerix::vote::operator() (celerix::object_stream & obs) const
{
	obs.write ("account", account);
	obs.write ("final", is_final_timestamp (timestamp_m));
	obs.write ("timestamp", timestamp_m);
	obs.write_range ("hashes", hashes);
}
