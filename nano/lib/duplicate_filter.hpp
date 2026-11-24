#pragma once

#include <nano/lib/numbers.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nano
{
// What a duplicate filter hashes an input to: its keyed 128-bit hash
using filter_digest = nano::uint128_t;

/**
 * Probabilistic duplicate filter: a fixed table of slots, one entry each, over SipHash-2-4-128 digests keyed at random per filter.
 * The lower half of a digest picks its slot and an entry keeps a 48-bit tag from the upper half, so two different inputs count as the same one only when they share both; an input already seen passes again once another digest takes its slot, which a larger table makes rarer.
 * An input is its bytes together with a domain, a number the caller picks to say what the bytes are, so the same bytes in two domains are two inputs.
 * Entries age in epochs the owner moves forward with advance () rather than in clock time, which spares every insert a clock read. An entry keeps the low 16 bits of the epoch it went in at, which tells its age for one lap of those bits, so the owner clears the filter when advance () reports that they have wrapped.
 * Thread-safe without a lock: an entry is one atomic word replaced with a compare and swap, so of several threads inserting one digest at once normally only one is told it is new.
 * The filter is allowed to let a repeat through, and now and then it does: another digest took the slot, the entry expired, or an insert ran alongside an advance () or a clear (). Whatever comes after the filter has to cope with repeats anyway, so these cases are left open rather than paid for on every insert. What it does not do is hold back an input it has not seen, short of two digests sharing a slot and a tag.
 */
class duplicate_filter final
{
public: // Constants
	// Bits of the epoch an entry keeps, the other bits of its word hold the tag
	static constexpr unsigned epoch_bits = 16;

	// Largest age cutoff that the bits of the epoch an entry keeps can tell from a younger entry
	static constexpr uint64_t max_age_cutoff = (uint64_t{ 1 } << epoch_bits) - 1;

public:
	// A table of `size` slots, where entries older than `age_cutoff` epochs no longer count as present. The cutoff is at most `max_age_cutoff`
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

	// Advances the epoch by `epochs`, which must be positive. Returns true when the epoch bits that entries keep have wrapped
	bool advance (uint64_t epochs = 1);

	// Digest of the bytes in the domain under this filter's key
	nano::filter_digest hash (std::span<uint8_t const> bytes, uint64_t domain = 0) const;

private:
	// An entry, the tag of its digest above the low bits of the epoch it went in at
	using entry = uint64_t;

	// What a slot with no entry holds
	static constexpr entry empty = 0;

	// The bits of an entry that hold the epoch
	static constexpr uint64_t epoch_mask = max_age_cutoff;

	// Slot the digest maps to
	std::size_t index_of (nano::filter_digest const & digest) const;

	// Tag an entry keeps of the digest, never zero, so no digest matches a slot with no entry
	static uint64_t tag_of (nano::filter_digest const & digest);

	// Entry for a digest with this tag going in at `epoch`
	static entry make_entry (uint64_t tag, uint64_t epoch);

	// Whether the entry holds the tag and is within the age cutoff at `epoch`
	bool matches (entry existing, uint64_t tag, uint64_t epoch) const;

private:
	uint64_t const age_cutoff;

	// SipHash key, drawn at random when the filter is made and wiped when it is destroyed
	std::array<uint8_t, 16> key;

	std::vector<std::atomic<entry>> items;
	std::atomic<uint64_t> current_epoch{ 0 };
};
}
