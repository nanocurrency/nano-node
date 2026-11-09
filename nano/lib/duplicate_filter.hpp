#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <cryptopp/seckey.h>
#include <cryptopp/siphash.h>

namespace nano
{
/**
 * Probabilistic duplicate filter: a fixed table of SipHash-2-4-128 digests, one per slot, keyed at random per filter.
 * Two different inputs count as the same one only on a 128-bit digest collision; an input already seen passes again once another digest takes its slot, which a larger table makes rarer.
 * Entries age in epochs the owner moves forward with advance () rather than in clock time, which spares every insert a clock read.
 * Thread-safe.
 */
class duplicate_filter final
{
public:
	using digest = nano::uint128_t;

public:
	// A table of `size` slots, where entries older than `age_cutoff` epochs no longer count as present
	explicit duplicate_filter (std::size_t size, uint64_t age_cutoff = 0);

	// Inserts the digest, returning false if it was already present, which marks the input as a duplicate
	bool insert (std::span<uint8_t const> bytes);
	bool insert (digest const & digest);

	// Whether the digest is present
	bool contains (std::span<uint8_t const> bytes) const;
	bool contains (digest const & digest) const;

	// Removes the digest if present, so the same input passes the filter again
	void erase (std::span<uint8_t const> bytes);
	void erase (digest const & digest);
	void erase (nano::block const &);

	// Removes every entry, keeping the table size
	void clear ();

	// Advances the epoch by `epochs`, which must be positive
	void advance (uint64_t epochs = 1);

	// Digest of the bytes under this filter's key
	digest hash (std::span<uint8_t const> bytes) const;

	// Digest of the block's serialized form, which is how a publish message carries it
	digest hash (nano::block const &) const;

private:
	struct entry
	{
		duplicate_filter::digest digest{ 0 };
		uint64_t epoch{ 0 };
	};

	// Slot the digest maps to
	std::size_t index_of (digest const & digest) const;

	// Whether the entry holds the digest and is within the age cutoff, the mutex must be held
	bool matches (entry const &, digest const & digest) const;

private:
	uint64_t const age_cutoff;

	using siphash_t = CryptoPP::SipHash<2, 4, true>;
	CryptoPP::SecByteBlock key{ siphash_t::KEYLENGTH };

	std::vector<entry> items;
	uint64_t current_epoch{ 0 };

	mutable nano::mutex mutex{ mutex_identifier (mutexes::duplicate_filter) };
};
}
