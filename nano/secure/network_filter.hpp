#pragma once

#include <nano/lib/numbers.hpp>

#include <crypto/cryptopp/seckey.h>
#include <crypto/cryptopp/siphash.h>

#include <mutex>

namespace nano
{
/**
 * A probabilistic duplicate filter based on directed map caches, using SipHash 2/4/128
 * The probability of false negatives (unique packet marked as duplicate) is the probability of a 128-bit SipHash collision.
 * The probability of false positives (duplicate packet marked as unique) shrinks with a larger filter.
 * @note This class is thread-safe.
 */
class network_filter final
{
public:
	network_filter () = delete;
	network_filter (size_t size_a);
	/**
	 * Reads \p count_a bytes starting from \p bytes_a and inserts the siphash digest in the filter.
	 * @warning will read out of bounds if [ \p bytes_a, \p bytes_a + \p count_a ] is not a valid range
	 * @return a boolean representing the previous existence of the hash in the filter.
	 **/
	bool apply (uint8_t const * bytes_a, size_t count_a);

	/**
	 * Reads \p count_a bytes starting from \p bytes_a and digests the contents.
	 * Then, sets the corresponding element in the filter to zero, if it matches the digest exactly.
	 * @warning will read out of bounds if [ \p bytes_a, \p bytes_a + \p count_a ] is not a valid range
	 **/
	void clear (uint8_t const * bytes_a, size_t count_a);

	/**
	 * Serializes \p object_a and runs clears the resulting siphash digest.
	 * @return a boolean representing the previous existence of the hash in the filter.
	 **/
	template <typename OBJECT>
	void clear (OBJECT const & object_a);

	/** Sets every element of the filter to zero, keeping its size and capacity. */
	void clear ();

private:
	using siphash_t = CryptoPP::SipHash<2, 4, true>;
	using item_key_t = nano::uint128_t;

	/**
	 * Get element from digest.
	 * @note must have a lock on mutex
	 * @return a reference to the element with key \p hash_a
	 **/
	item_key_t & get_element (item_key_t const & hash_a);

	/**
	 * Hashes \p count_a bytes starting from \p bytes_a .
	 * @return the siphash digest of the contents in \p bytes_a .
	 **/
	item_key_t hash (uint8_t const * bytes_a, size_t count_a) const;

	std::vector<item_key_t> items;
	CryptoPP::SecByteBlock key{ siphash_t::KEYLENGTH };
	std::mutex mutex;
};
}
