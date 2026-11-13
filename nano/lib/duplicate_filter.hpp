#pragma once

#include <nano/lib/numbers.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nano
{
// What a duplicate filter keeps of an input: its keyed 128-bit hash
using filter_digest = nano::uint128_t;

/**
 * Probabilistic duplicate filter: a fixed table of SipHash-2-4-128 digests, one per slot, keyed at random per filter.
 * Two different inputs count as the same one only on a 128-bit digest collision; an input already seen passes again once another digest takes its slot, which a larger table makes rarer.
 * An input is its bytes together with a domain, a number the caller picks to say what the bytes are, so the same bytes in two domains are two inputs.
 * Entries age in epochs the owner moves forward with advance () rather than in clock time, which spares every insert a clock read.
 * Not synchronized, so an owner that shares it between threads serializes the calls. Only hash () may run alongside any of them, as it reads nothing but the key, which never changes.
 */
class duplicate_filter final
{
public:
	// A table of `size` slots, where entries older than `age_cutoff` epochs no longer count as present
	explicit duplicate_filter (std::size_t size, uint64_t age_cutoff = 0);
	~duplicate_filter ();

	// Inserts the digest, returning false if it was already present, which marks the input as a duplicate
	bool insert (std::span<uint8_t const> bytes, uint64_t domain = 0);
	bool insert (nano::filter_digest const & digest);

	// Whether the digest is present
	bool contains (std::span<uint8_t const> bytes, uint64_t domain = 0) const;
	bool contains (nano::filter_digest const & digest) const;

	// Removes the digest if present, so the same input passes the filter again
	void erase (std::span<uint8_t const> bytes, uint64_t domain = 0);
	void erase (nano::filter_digest const & digest);

	// Removes every entry, keeping the table size
	void clear ();

	// Advances the epoch by `epochs`, which must be positive
	void advance (uint64_t epochs = 1);

	// Digest of the bytes in the domain under this filter's key
	nano::filter_digest hash (std::span<uint8_t const> bytes, uint64_t domain = 0) const;

private:
	struct entry
	{
		nano::filter_digest digest{ 0 };
		uint64_t epoch{ 0 };
	};

	// Slot the digest maps to
	std::size_t index_of (nano::filter_digest const & digest) const;

	// Whether the entry holds the digest and is within the age cutoff
	bool matches (entry const &, nano::filter_digest const & digest) const;

private:
	uint64_t const age_cutoff;

	// SipHash key, drawn at random when the filter is made and wiped when it is destroyed
	std::array<uint8_t, 16> key;

	std::vector<entry> items;
	uint64_t current_epoch{ 0 };
};
}
