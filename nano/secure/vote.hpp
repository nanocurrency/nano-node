#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/uniquer.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

#include <vector>

namespace nano
{
using vote_blocks_vec_iter = std::vector<nano::block_hash>::const_iterator;
class iterate_vote_blocks_as_hash final
{
public:
	iterate_vote_blocks_as_hash () = default;
	nano::block_hash operator() (nano::block_hash const & item) const;
};

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

	nano::block_hash hash () const;
	nano::block_hash full_hash () const;
	bool validate () const;

	bool operator== (nano::vote const &) const;
	bool operator!= (nano::vote const &) const;

	boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> end () const;

	void serialize_json (boost::property_tree::ptree & tree) const;
	std::string to_json () const;
	std::string hashes_string () const;

	uint64_t timestamp () const;
	uint8_t duration_bits () const;
	std::chrono::milliseconds duration () const;

	static uint64_t constexpr timestamp_mask = { 0xffff'ffff'ffff'fff0ULL };
	static nano::seconds_t constexpr timestamp_max = { 0xffff'ffff'ffff'fff0ULL };
	static uint64_t constexpr timestamp_min = { 0x0000'0000'0000'0010ULL };
	static uint8_t constexpr duration_max = { 0x0fu };

	/* Check if timestamp represents a final vote */
	static bool is_final_timestamp (uint64_t timestamp);

private:
	static std::string const hash_prefix;

	static uint64_t packed_timestamp (uint64_t timestamp, uint8_t duration);

public: // Payload
	// The hashes for which this vote directly covers
	std::vector<nano::block_hash> hashes;
	// Account that's voting
	nano::account account{ 0 };
	// Signature of timestamp + block hashes
	nano::signature signature{ 0 };

private: // Payload
	// Vote timestamp
	uint64_t timestamp_m{ 0 };
};

using vote_uniquer = nano::uniquer<nano::block_hash, nano::vote>;
}