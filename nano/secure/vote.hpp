#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/uniquer.hpp>
#include <nano/secure/fwd.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <deque>
#include <vector>

namespace nano
{
/**
 * A representative's signed statement for a batch of block hashes.
 * The signature covers hash (): a fixed prefix, the hashes and the packed timestamp, so the same content signed by two representatives shares a hash () and differs only in full_hash ().
 * The timestamp packs milliseconds since epoch into the upper 60 bits and the duration exponent into the low 4 bits; the all-ones value is reserved for final votes.
 * The serialized form carries no hash count: the confirm_ack header holds it and sizes the stream handed to deserialize (), which reads hashes until it ends.
 */
class vote final
{
public:
	vote () = default;
	// Deserialize from stream, sets error on failure
	vote (bool & error, nano::stream &);
	// Sign the hashes with the representative's key; the timestamp is packed with the duration exponent and the hashes must fit max_hashes
	vote (nano::account const &, nano::raw_key const &, nano::millis_t timestamp, uint8_t duration, std::vector<nano::block_hash> const & hashes);

	// Serialize to stream: account, signature, packed timestamp, then the hashes with no count in front
	void serialize (nano::stream &) const;
	// Deserialize from stream, returns true on error; reads hashes until the stream ends, at most max_hashes
	bool deserialize (nano::stream &);
	// Serialized size of a vote carrying the given number of hashes
	static std::size_t size (uint8_t count);

	// Digest the signature covers, identical for the same hashes and timestamp regardless of the signer
	nano::block_hash hash () const;
	// Digest over hash (), account and signature, unique per vote; keys the vote uniquer
	nano::block_hash full_hash () const;
	// Verify the signature against the account, returns true on error
	bool validate () const;

	bool operator== (nano::vote const &) const = default;

	// Property tree with the account, signature, timestamp (also as the legacy `sequence`), duration bits and the hashes under `blocks`
	void serialize_json (boost::property_tree::ptree & tree) const;
	std::string to_json () const;

	// Timestamp with the duration bits cleared; a final vote returns timestamp_final unchanged
	uint64_t timestamp () const;
	// Duration exponent from the low 4 bits
	uint8_t duration_bits () const;
	// How long the vote stays valid, 2^(duration_bits + 4) milliseconds, so 16 ms up to about 9 minutes
	std::chrono::milliseconds duration () const;
	// Whether the vote is final
	bool is_final () const;

public:
	// The vote timestamp packs the timestamp itself into the upper 60 bits and the duration exponent into the low 4 bits; the all-ones value marks a final vote
	static uint64_t constexpr timestamp_mask = { 0xffff'ffff'ffff'fff0ULL }; // Masks off the duration bits to extract the timestamp
	static uint64_t constexpr timestamp_final = { 0xffff'ffff'ffff'ffffULL }; // Timestamp of a final vote, the reserved all-ones value
	static uint64_t constexpr timestamp_max = { 0xffff'ffff'ffff'fff0ULL }; // Highest timestamp, packable only together with duration_max, which forms timestamp_final; not itself the final marker
	static uint64_t constexpr timestamp_min = { 0x0000'0000'0000'0010ULL }; // Lowest nonzero timestamp, a single timestamp step
	static uint8_t constexpr duration_max = { 0x0fu }; // Highest duration exponent, fills the low 4 bits

	// Most hashes one vote carries, the message header counts them in a single byte
	static std::size_t constexpr max_hashes = 255;

	// Split hashes into the votes they fit, keeping their order
	static std::deque<std::vector<nano::block_hash>> batch_hashes (std::deque<nano::block_hash> const &);

	// Whether a packed timestamp is the final vote marker
	static bool is_final_timestamp (uint64_t timestamp);

public: // Payload
	std::vector<nano::block_hash> hashes; // Block hashes the vote covers
	nano::account account{ 0 }; // Representative that cast the vote
	nano::signature signature{ 0 }; // The account's signature over hash ()

private: // Payload
	uint64_t timestamp_m{ 0 }; // Timestamp packed with the duration exponent, read through timestamp () and duration_bits ()

private:
	static std::size_t constexpr partial_size = sizeof (account) + sizeof (signature) + sizeof (timestamp_m); // Serialized size without the hashes
	static std::string const hash_prefix; // Domain separator at the front of hash (), so a vote digest never collides with other signed material

	// Combine a timestamp and a duration exponent into the packed form; rejects an exponent over duration_max and timestamp_max without it
	static uint64_t packed_timestamp (uint64_t timestamp, uint8_t duration);

public: // Logging
	void operator() (nano::object_stream &) const;
};

// Shares one instance among votes with the same full_hash (), so a vote received from many peers is kept once
using vote_uniquer = nano::uniquer<nano::block_hash, nano::vote>;
}
