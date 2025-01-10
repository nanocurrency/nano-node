#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/network_filter.hpp>
#include <celerix/lib/stream.hpp>
#include <celerix/secure/common.hpp>

celerix::network_filter::network_filter (size_t size_a, epoch_t age_cutoff_a) :
	items (size_a, { 0 }),
	age_cutoff{ age_cutoff_a }
{
	celerix::random_pool::generate_block (key, key.size ());
}

void celerix::network_filter::update (epoch_t epoch_inc)
{
	debug_assert (epoch_inc > 0);
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	current_epoch += epoch_inc;
}

bool celerix::network_filter::compare (entry const & existing, digest_t const & digest) const
{
	debug_assert (!mutex.try_lock ());
	// Only consider digests to be the same if the epoch is within the age cutoff
	return existing.digest == digest && existing.epoch + age_cutoff >= current_epoch;
}

bool celerix::network_filter::apply (uint8_t const * bytes_a, size_t count_a, celerix::uint128_t * digest_out)
{
	// Get hash before locking
	auto digest = hash (bytes_a, count_a);
	if (digest_out)
	{
		*digest_out = digest;
	}
	return apply (digest);
}

bool celerix::network_filter::apply (digest_t const & digest)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	auto & element = get_element (digest);
	bool existed = compare (element, digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = { digest, current_epoch };
	}
	return existed;
}

bool celerix::network_filter::check (uint8_t const * bytes, size_t count) const
{
	return check (hash (bytes, count));
}

bool celerix::network_filter::check (digest_t const & digest) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto & element = get_element (digest);
	return compare (element, digest);
}

void celerix::network_filter::clear (digest_t const & digest)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto & element = get_element (digest);
	if (compare (element, digest))
	{
		element = { 0 };
	}
}

void celerix::network_filter::clear (std::vector<digest_t> const & digests)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	for (auto const & digest : digests)
	{
		auto & element = get_element (digest);
		if (compare (element, digest))
		{
			element = { 0 };
		}
	}
}

void celerix::network_filter::clear (uint8_t const * bytes_a, size_t count_a)
{
	clear (hash (bytes_a, count_a));
}

template <typename OBJECT>
void celerix::network_filter::clear (OBJECT const & object_a)
{
	clear (hash (object_a));
}

void celerix::network_filter::clear ()
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	items.assign (items.size (), { 0 });
}

template <typename OBJECT>
celerix::uint128_t celerix::network_filter::hash (OBJECT const & object_a) const
{
	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream stream (bytes);
		object_a->serialize (stream);
	}
	return hash (bytes.data (), bytes.size ());
}

auto celerix::network_filter::get_element (celerix::uint128_t const & hash_a) -> entry &
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

auto celerix::network_filter::get_element (celerix::uint128_t const & hash_a) const -> entry const &
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

celerix::uint128_t celerix::network_filter::hash (uint8_t const * bytes_a, size_t count_a) const
{
	celerix::uint128_union digest{ 0 };
	siphash_t siphash (key, static_cast<unsigned int> (key.size ()));
	siphash.CalculateDigest (digest.bytes.data (), bytes_a, count_a);
	return digest.number ();
}

// Explicitly instantiate
template celerix::uint128_t celerix::network_filter::hash (std::shared_ptr<celerix::block> const &) const;
template void celerix::network_filter::clear (std::shared_ptr<celerix::block> const &);
