#pragma once

#include <celerix/lib/fwd.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/lib/uniquer.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

#include <vector>

namespace celerix
{
class vote final
{
public:
	vote () = default;
	vote (celerix::vote const &) = default;
	vote (bool & error, celerix::stream &);
	vote (celerix::account const &, celerix::raw_key const &, celerix::millis_t timestamp, uint8_t duration, std::vector<celerix::block_hash> const & hashes);

	void serialize (celerix::stream &) const;
	/**
	 * Deserializes a vote from the bytes in `stream'
	 * @returns true if there was an error
	 */
	bool deserialize (celerix::stream &);
	static std::size_t size (uint8_t count);

	celerix::block_hash hash () const;
	celerix::block_hash full_hash () const;
	bool validate () const;

	bool operator== (celerix::vote const &) const;
	bool operator!= (celerix::vote const &) const;

	void serialize_json (boost::property_tree::ptree & tree) const;
	std::string to_json () const;
	std::string hashes_string () const;

	uint64_t timestamp () const;
	uint8_t duration_bits () const;
	std::chrono::milliseconds duration () const;
	bool is_final () const;

	static uint64_t constexpr timestamp_mask = { 0xffff'ffff'ffff'fff0ULL };
	static celerix::seconds_t constexpr timestamp_max = { 0xffff'ffff'ffff'fff0ULL };
	static uint64_t constexpr timestamp_min = { 0x0000'0000'0000'0010ULL };
	static uint8_t constexpr duration_max = { 0x0fu };

	static std::size_t constexpr max_hashes = 255;

	/* Check if timestamp represents a final vote */
	static bool is_final_timestamp (uint64_t timestamp);

public: // Payload
	// The hashes for which this vote directly covers
	std::vector<celerix::block_hash> hashes;
	// Account that's voting
	celerix::account account{ 0 };
	// Signature of timestamp + block hashes
	celerix::signature signature{ 0 };

private: // Payload
	// Vote timestamp
	uint64_t timestamp_m{ 0 };

private:
	// Size of vote payload without hashes
	static std::size_t constexpr partial_size = sizeof (account) + sizeof (signature) + sizeof (timestamp_m);
	static std::string const hash_prefix;

	static uint64_t packed_timestamp (uint64_t timestamp, uint8_t duration);

public: // Logging
	void operator() (celerix::object_stream &) const;
};

using vote_uniquer = celerix::uniquer<celerix::block_hash, celerix::vote>;
}
