#include <nano/crypto_lib/random_pool.hpp>
#include <nano/crypto_lib/secure_memory.hpp>
#include <nano/lib/assert.hpp>
#include <nano/lib/duplicate_filter.hpp>

#include <limits>

#include <cryptopp/siphash.h>

// Relaxed ordering throughout: the worst a reordered read can do is let a repeat through, which the filter accepts

nano::duplicate_filter::duplicate_filter (std::size_t size_a, uint64_t age_cutoff_a) :
	age_cutoff{ age_cutoff_a },
	items (size_a)
{
	static_assert (std::tuple_size_v<decltype (key)> == CryptoPP::SipHash<2, 4, true>::KEYLENGTH);
	release_assert (age_cutoff <= max_age_cutoff);
	nano::random_pool::generate_block (key.data (), key.size ());
}

nano::duplicate_filter::~duplicate_filter ()
{
	nano::secure_wipe_memory (key.data (), key.size ());
}

bool nano::duplicate_filter::insert (std::span<uint8_t const> bytes, uint64_t domain)
{
	return insert (hash (bytes, domain));
}

bool nano::duplicate_filter::insert (nano::filter_digest const & digest)
{
	auto & slot = items[index_of (digest)];
	auto const tag = tag_of (digest);

	auto existing = slot.load (std::memory_order_relaxed);
	while (true)
	{
		// The epoch is read after the slot, so that an entry is not compared with an epoch older than its own
		auto const epoch = current_epoch.load (std::memory_order_relaxed);
		if (matches (existing, tag, epoch))
		{
			return false;
		}

		// Replace likely old element with a new one, a failed swap reloads the slot, which may now hold this digest
		if (slot.compare_exchange_weak (existing, make_entry (tag, epoch), std::memory_order_relaxed))
		{
			return true;
		}
	}
}

bool nano::duplicate_filter::contains (std::span<uint8_t const> bytes, uint64_t domain) const
{
	return contains (hash (bytes, domain));
}

bool nano::duplicate_filter::contains (nano::filter_digest const & digest) const
{
	// The slot first and the epoch after it, as in insert ()
	auto const existing = items[index_of (digest)].load (std::memory_order_relaxed);
	auto const epoch = current_epoch.load (std::memory_order_relaxed);
	return matches (existing, tag_of (digest), epoch);
}

void nano::duplicate_filter::erase (std::span<uint8_t const> bytes, uint64_t domain)
{
	erase (hash (bytes, domain));
}

void nano::duplicate_filter::erase (nano::filter_digest const & digest)
{
	auto & slot = items[index_of (digest)];
	auto const tag = tag_of (digest);

	auto existing = slot.load (std::memory_order_relaxed);
	while (true)
	{
		// The epoch is read after the slot, so that an entry is not compared with an epoch older than its own
		auto const epoch = current_epoch.load (std::memory_order_relaxed);
		if (!matches (existing, tag, epoch))
		{
			return;
		}

		// A failed swap reloads the slot, which may now hold another digest
		if (slot.compare_exchange_weak (existing, empty, std::memory_order_relaxed))
		{
			return;
		}
	}
}

void nano::duplicate_filter::clear ()
{
	for (auto & slot : items)
	{
		slot.store (empty, std::memory_order_relaxed);
	}
}

bool nano::duplicate_filter::advance (uint64_t epochs)
{
	debug_assert (epochs > 0);
	auto const before = current_epoch.fetch_add (epochs, std::memory_order_relaxed);
	auto const after = before + epochs;

	// The bits above an entry's epoch bits count the laps
	bool const wrapped = (before >> epoch_bits) != (after >> epoch_bits);
	return wrapped;
}

nano::filter_digest nano::duplicate_filter::hash (std::span<uint8_t const> bytes, uint64_t domain) const
{
	nano::uint128_union digest{ 0 };
	CryptoPP::SipHash<2, 4, true> siphash{ key.data (), static_cast<unsigned int> (key.size ()) };

	// The domain has a fixed width and goes in ahead of the bytes, so different inputs never feed the hash the same sequence
	siphash.Update (reinterpret_cast<uint8_t const *> (&domain), sizeof (domain));
	siphash.Update (bytes.data (), bytes.size ());
	siphash.Final (digest.bytes.data ());

	return digest.number ();
}

std::size_t nano::duplicate_filter::index_of (nano::filter_digest const & digest) const
{
	debug_assert (!items.empty ());
	auto const lower_half = static_cast<uint64_t> (digest & std::numeric_limits<uint64_t>::max ());
	return static_cast<std::size_t> (lower_half % items.size ());
}

uint64_t nano::duplicate_filter::tag_of (nano::filter_digest const & digest)
{
	auto const upper_half = static_cast<uint64_t> (digest >> 64);
	auto const tag = upper_half >> epoch_bits;

	// Zero marks an empty slot, so a zero tag becomes one
	return tag != 0 ? tag : 1;
}

auto nano::duplicate_filter::make_entry (uint64_t tag, uint64_t epoch) -> entry
{
	return (tag << epoch_bits) | (epoch & epoch_mask);
}

bool nano::duplicate_filter::matches (entry existing, uint64_t tag, uint64_t epoch) const
{
	// Age modulo one lap of the entry's epoch bits, which the tag above them does not affect
	auto const age = (epoch - existing) & epoch_mask;
	bool const same_tag = (existing >> epoch_bits) == tag;

	// Only consider digests to be the same if the epoch is within the age cutoff
	return same_tag && age <= age_cutoff;
}
