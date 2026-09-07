#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/uniquer.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

#include <vector>

namespace nano
{
class vote final
{
public:
	vote () = default;
	vote (nano::vote const &) = default;
	vote (bool & error, nano::stream &);
	vote (nano::account const &, nano::raw_key const &, nano::millis_t timestamp, uint8_t duration, std::vector<nano::block_hash> const & hashes);

	void serialize (nano::stream &) const;
	/**
	 * Deserializes a vote from the bytes in `stream'
	 * @returns true if there was an error
	 */
	bool deserialize (nano::stream &);
	static std::size_t size (uint8_t count); // TODO: This name is confusing, vote size is number of hashes present, not the message size

	nano::block_hash hash () const;
	nano::block_hash full_hash () const;
	bool validate () const;

	bool operator== (nano::vote const &) const;
	bool operator!= (nano::vote const &) const;

	void serialize_json (boost::property_tree::ptree & tree) const;
	std::string to_json () const;
	std::string hashes_string () const;

	uint64_t timestamp () const;
	uint8_t duration_bits () const;
	std::chrono::milliseconds duration () const;
	bool is_final () const;

public:
	// The vote timestamp packs the timestamp itself into the upper 60 bits and the duration exponent into the low 4 bits; the all-ones value marks a final vote
	static uint64_t constexpr timestamp_mask = { 0xffff'ffff'ffff'fff0ULL }; // Masks off the duration bits to extract the timestamp
	static uint64_t constexpr timestamp_final = { 0xffff'ffff'ffff'ffffULL }; // Timestamp of a final vote, the reserved all-ones value
	static uint64_t constexpr timestamp_max = { 0xffff'ffff'ffff'fff0ULL }; // Highest timestamp, packable only together with duration_max, which forms timestamp_final; not itself the final marker
	static uint64_t constexpr timestamp_min = { 0x0000'0000'0000'0010ULL }; // Lowest nonzero timestamp, a single timestamp step
	static uint8_t constexpr duration_max = { 0x0fu }; // Highest duration exponent, fills the low 4 bits

	static std::size_t constexpr max_hashes = 255;

	// Whether the given timestamp is the final vote marker
	static bool is_final_timestamp (uint64_t timestamp);

public: // Payload
	// The hashes for which this vote directly covers
	std::vector<nano::block_hash> hashes;
	// Account that's voting
	nano::account account{ 0 };
	// Signature of timestamp + block hashes
	nano::signature signature{ 0 };

private: // Payload
	// Vote timestamp (milliseconds since epoch)
	uint64_t timestamp_m{ 0 };

private:
	// Size of vote payload without hashes
	static std::size_t constexpr partial_size = sizeof (account) + sizeof (signature) + sizeof (timestamp_m);
	static std::string const hash_prefix;

	static uint64_t packed_timestamp (uint64_t timestamp, uint8_t duration);

public: // Logging
	void operator() (nano::object_stream &) const;
};

using vote_uniquer = nano::uniquer<nano::block_hash, nano::vote>;
}
