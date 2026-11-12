#include <nano/crypto_lib/random_pool.hpp>
#include <nano/crypto_lib/secure_memory.hpp>
#include <nano/lib/assert.hpp>
#include <nano/lib/duplicate_filter.hpp>

#include <cryptopp/siphash.h>

nano::duplicate_filter::duplicate_filter (std::size_t size_a, uint64_t age_cutoff_a) :
	age_cutoff{ age_cutoff_a },
	items (size_a)
{
	static_assert (std::tuple_size_v<decltype (key)> == CryptoPP::SipHash<2, 4, true>::KEYLENGTH);
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
	auto & element = items[index_of (digest)];
	bool const existed = matches (element, digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = { digest, current_epoch };
	}
	return !existed;
}

bool nano::duplicate_filter::contains (std::span<uint8_t const> bytes, uint64_t domain) const
{
	return contains (hash (bytes, domain));
}

bool nano::duplicate_filter::contains (nano::filter_digest const & digest) const
{
	return matches (items[index_of (digest)], digest);
}

void nano::duplicate_filter::erase (std::span<uint8_t const> bytes, uint64_t domain)
{
	erase (hash (bytes, domain));
}

void nano::duplicate_filter::erase (nano::filter_digest const & digest)
{
	auto & element = items[index_of (digest)];
	if (matches (element, digest))
	{
		element = {};
	}
}

void nano::duplicate_filter::clear ()
{
	items.assign (items.size (), {});
}

void nano::duplicate_filter::advance (uint64_t epochs)
{
	debug_assert (epochs > 0);
	current_epoch += epochs;
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
	return static_cast<std::size_t> (digest % items.size ());
}

bool nano::duplicate_filter::matches (entry const & existing, nano::filter_digest const & digest) const
{
	// Only consider digests to be the same if the epoch is within the age cutoff
	return existing.digest == digest && existing.epoch + age_cutoff >= current_epoch;
}
