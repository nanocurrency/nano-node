#pragma once

#include <nano/lib/duplicate_filter.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/node/fwd.hpp>

#include <cstdint>
#include <mutex>
#include <span>

namespace nano::transport
{
/**
 * Duplicate filter for network messages, a front for a `nano::duplicate_filter`, which knows nothing of messages.
 * It says which messages are deduplicated, and a message goes in as its payload under a domain made of what the header says the payload is, so the same bytes carried as two kinds of message are two entries.
 * Thread-safe.
 */
class message_filter final
{
public:
	// Takes its table size and age cutoff from the config, along with whether it deduplicates at all
	explicit message_filter (nano::network_config const &);

	// Whether messages of this type go through the filter: blocks and votes do, and none does once deduplication is turned off
	bool deduplicates (nano::messages::message_type) const;

	// Digest of a message's payload, read the way its header says
	nano::filter_digest hash (nano::messages::message_header const &, std::span<uint8_t const> payload) const;

	// Digest of the publish that carries the block
	nano::filter_digest hash (nano::block const &) const;

	// Inserts the digest, returning false if it was already present, which marks the message as a duplicate
	bool insert (nano::filter_digest const & digest);

	// Whether the digest is present
	bool contains (nano::filter_digest const & digest) const;

	// Removes the digest if present, so the same message passes the filter again
	void erase (nano::filter_digest const & digest);
	void erase (nano::block const &);

	// Removes every entry
	void clear ();

	// Advances the epoch by `epochs`, which must be positive
	void advance (uint64_t epochs = 1);

private: // Dependencies
	nano::network_config const & config;

private:
	// Holds no lock of its own, so every call into it but hash () is made under the mutex
	nano::duplicate_filter filter;

	mutable std::mutex mutex;
};
}
