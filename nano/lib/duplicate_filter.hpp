
#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>

#include <cryptopp/seckey.h>
#include <cryptopp/siphash.h>

namespace nano
{
/**
 * A probabilistic duplicate filter based on directed map caches, using SipHash 2/4/128
 * The probability of false negatives (unique packet marked as duplicate) is the probability of a 128-bit SipHash collision.
 * The probability of false positives (duplicate packet marked as unique) shrinks with a larger filter.
 * @note This class is thread-safe.
 */
class duplicate_filter final
{
public:
	using digest = nano::uint128_t;

public:
	explicit duplicate_filter (size_t size, uint64_t age_cutoff = 0);

	/**
	 * Advances the filter to the next epoch.
	 * Should be called periodically to time out old entries.
	 */
	void advance (uint64_t epoch_inc = 1);

	/**
	 * Reads \p count_a bytes starting from \p bytes_a and inserts the siphash digest in the filter.
	 * @param \p digest_a if given, will be set to the resulting siphash digest
	 * @warning will read out of bounds if [ \p bytes_a, \p bytes_a + \p count_a ] is not a valid range
	 * @return true if the hash was new and went in, false if it was already present.
	 **/
	bool insert (uint8_t const * bytes, size_t count, digest * digest_out = nullptr);
	bool insert (digest const & digest);

	/**
	 * Checks if the digest is in the filter.
	 * @return a boolean representing the existence of the hash in the filter.
	 */
	bool contains (uint8_t const * bytes, size_t count) const;
	bool contains (digest const & digest) const;

	/**
	 * Sets the corresponding element in the filter to zero, if it matches \p digest_a exactly.
	 **/
	void erase (digest const & digest);

	/**
	 * Erase many digests from the filter
	 **/
	void erase (std::vector<digest> const &);

	/**
	 * Reads \p count_a bytes starting from \p bytes_a and digests the contents.
	 * Then, sets the corresponding element in the filter to zero, if it matches the digest exactly.
	 * @warning will read out of bounds if [ \p bytes_a, \p bytes_a + \p count_a ] is not a valid range
	 **/
	void erase (uint8_t const * bytes, size_t count);

	/**
	 * Serializes \p object_a and erases the resulting siphash digest from the filter.
	 **/
	template <typename OBJECT>
	void erase (OBJECT const & object);

	/** Sets every element of the filter to zero, keeping its size and capacity. */
	void clear ();

	/**
	 * Serializes \p object_a and returns the resulting siphash digest
	 */
	template <typename OBJECT>
	digest hash (OBJECT const & object) const;

	/**
	 * Hashes \p count_a bytes starting from \p bytes_a .
	 * @return the siphash digest of the contents in \p bytes_a .
	 **/
	digest hash (uint8_t const * bytes, size_t count) const;

private:
	uint64_t const age_cutoff;
	uint64_t current_epoch{ 0 };

	using siphash_t = CryptoPP::SipHash<2, 4, true>;
	CryptoPP::SecByteBlock key{ siphash_t::KEYLENGTH };

	mutable nano::mutex mutex{ mutex_identifier (mutexes::duplicate_filter) };

private:
	struct entry
	{
		duplicate_filter::digest digest;
		uint64_t epoch;
	};

	std::vector<entry> items;

	/**
	 * Get element from digest.
	 * @note must have a lock on mutex
	 * @return a reference to the element with key \p hash_a
	 **/
	entry & get_element (digest const & hash);
	entry const & get_element (digest const & hash) const;

	bool compare (entry const & existing, digest const & digest) const;
};
}
