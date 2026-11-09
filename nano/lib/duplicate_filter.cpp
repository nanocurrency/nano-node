#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/duplicate_filter.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/stream.hpp>

nano::duplicate_filter::duplicate_filter (std::size_t size_a, uint64_t age_cutoff_a) :
	age_cutoff{ age_cutoff_a },
	items (size_a)
{
	nano::random_pool::generate_block (key, key.size ());
}

bool nano::duplicate_filter::insert (std::span<uint8_t const> bytes)
{
	// Hash before locking
	return insert (hash (bytes));
}

bool nano::duplicate_filter::insert (digest const & digest)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto & element = items[index_of (digest)];
	bool const existed = matches (element, digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = { digest, current_epoch };
	}
	return !existed;
}

bool nano::duplicate_filter::contains (std::span<uint8_t const> bytes) const
{
	return contains (hash (bytes));
}

bool nano::duplicate_filter::contains (digest const & digest) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return matches (items[index_of (digest)], digest);
}

void nano::duplicate_filter::erase (std::span<uint8_t const> bytes)
{
	erase (hash (bytes));
}

void nano::duplicate_filter::erase (digest const & digest)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & element = items[index_of (digest)];
	if (matches (element, digest))
	{
		element = {};
	}
}

void nano::duplicate_filter::erase (nano::block const & block)
{
	erase (hash (block));
}

void nano::duplicate_filter::clear ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	items.assign (items.size (), {});
}

void nano::duplicate_filter::advance (uint64_t epochs)
{
	debug_assert (epochs > 0);
	nano::lock_guard<nano::mutex> lock{ mutex };
	current_epoch += epochs;
}

auto nano::duplicate_filter::hash (std::span<uint8_t const> bytes) const -> digest
{
	nano::uint128_union digest{ 0 };
	siphash_t siphash{ key, static_cast<unsigned int> (key.size ()) };
	siphash.CalculateDigest (digest.bytes.data (), bytes.data (), bytes.size ());
	return digest.number ();
}

auto nano::duplicate_filter::hash (nano::block const & block) const -> digest
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		block.serialize (stream);
	}
	return hash (bytes);
}

std::size_t nano::duplicate_filter::index_of (digest const & digest) const
{
	debug_assert (!items.empty ());
	return static_cast<std::size_t> (digest % items.size ());
}

bool nano::duplicate_filter::matches (entry const & existing, digest const & digest) const
{
	debug_assert (!mutex.try_lock ());
	// Only consider digests to be the same if the epoch is within the age cutoff
	return existing.digest == digest && existing.epoch + age_cutoff >= current_epoch;
}
