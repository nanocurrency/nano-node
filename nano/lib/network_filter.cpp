#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/network_filter.hpp>
#include <nano/lib/stream.hpp>
#include <nano/secure/common.hpp>

nano::network_filter::network_filter (size_t size_a) :
	items (size_a, nano::uint128_t{ 0 })
{
	nano::random_pool::generate_block (key, key.size ());
}

bool nano::network_filter::apply (uint8_t const * bytes_a, size_t count_a, nano::uint128_t * digest_out)
{
	// Get hash before locking
	auto digest = hash (bytes_a, count_a);
	if (digest_out)
	{
		*digest_out = digest;
	}
	return apply (digest);
}

bool nano::network_filter::apply (digest_t const & digest)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto & element = get_element (digest);
	bool existed = (element == digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = digest;
	}
	return existed;
}

bool nano::network_filter::check (uint8_t const * bytes, size_t count) const
{
	return check (hash (bytes, count));
}

bool nano::network_filter::check (digest_t const & digest) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & element = get_element (digest);
	return element == digest;
}

void nano::network_filter::clear (nano::uint128_t const & digest_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & element (get_element (digest_a));
	if (element == digest_a)
	{
		element = nano::uint128_t{ 0 };
	}
}

void nano::network_filter::clear (std::vector<nano::uint128_t> const & digests_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	for (auto const & digest : digests_a)
	{
		auto & element (get_element (digest));
		if (element == digest)
		{
			element = nano::uint128_t{ 0 };
		}
	}
}

void nano::network_filter::clear (uint8_t const * bytes_a, size_t count_a)
{
	clear (hash (bytes_a, count_a));
}

template <typename OBJECT>
void nano::network_filter::clear (OBJECT const & object_a)
{
	clear (hash (object_a));
}

void nano::network_filter::clear ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	items.assign (items.size (), nano::uint128_t{ 0 });
}

template <typename OBJECT>
nano::uint128_t nano::network_filter::hash (OBJECT const & object_a) const
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		object_a->serialize (stream);
	}
	return hash (bytes.data (), bytes.size ());
}

nano::uint128_t & nano::network_filter::get_element (nano::uint128_t const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

nano::uint128_t const & nano::network_filter::get_element (nano::uint128_t const & hash_a) const
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

nano::uint128_t nano::network_filter::hash (uint8_t const * bytes_a, size_t count_a) const
{
	nano::uint128_union digest{ 0 };
	siphash_t siphash (key, static_cast<unsigned int> (key.size ()));
	siphash.CalculateDigest (digest.bytes.data (), bytes_a, count_a);
	return digest.number ();
}

// Explicitly instantiate
template nano::uint128_t nano::network_filter::hash (std::shared_ptr<nano::block> const &) const;
template void nano::network_filter::clear (std::shared_ptr<nano::block> const &);
