#include <nano/lib/hashing.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <set>

/*
 * The mix is the finalizer of splitmix64, pinned by reference values so that a change to it is a deliberate one.
 */
TEST (hashing, mix64_reference)
{
	ASSERT_EQ (0x0ULL, nano::mix64 (0));
	ASSERT_EQ (0x5692161d100b05e5ULL, nano::mix64 (1));
	ASSERT_EQ (0xdbd238973a2b148aULL, nano::mix64 (2));
	ASSERT_EQ (0xe220a8397b1dcdafULL, nano::mix64 (0x9e3779b97f4a7c15ULL));
	ASSERT_EQ (0xb4d055fcf2cbbd7bULL, nano::mix64 (0xffffffffffffffffULL));
}

/*
 * The mix is a bijection: distinct inputs, even ones a single bit apart, never collide.
 */
TEST (hashing, mix64_distinct)
{
	std::set<uint64_t> inputs;
	for (uint64_t value = 0; value < 4096; ++value)
	{
		inputs.insert (value);
	}
	for (unsigned bit = 0; bit < 64; ++bit)
	{
		inputs.insert (uint64_t{ 1 } << bit);
	}

	std::set<uint64_t> outputs;
	for (auto value : inputs)
	{
		outputs.insert (nano::mix64 (value));
	}
	ASSERT_EQ (inputs.size (), outputs.size ());
}

/*
 * Addresses of objects allocated one after another share their low bits and differ in few others.
 * Hashed, they spread over any small number of buckets, where the raw address leaves whole buckets empty.
 */
TEST (hashing, pointer_hash_spreads_addresses)
{
	constexpr size_t count = 256;
	for (uintptr_t stride : { uintptr_t{ 16 }, uintptr_t{ 64 }, uintptr_t{ 4096 } })
	{
		for (size_t buckets = 2; buckets <= 6; ++buckets)
		{
			std::map<uint64_t, size_t> raw;
			std::map<uint64_t, size_t> hashed;
			for (size_t i = 0; i < count; ++i)
			{
				auto const address = uintptr_t{ 0x10000000 } + i * stride;
				++raw[address % buckets];
				++hashed[nano::pointer_hash (reinterpret_cast<void const *> (address)) % buckets];
			}

			// Every bucket gets between half and one and a half times its share
			ASSERT_EQ (buckets, hashed.size ()) << "stride " << stride << " buckets " << buckets;
			for (auto const & [bucket, size] : hashed)
			{
				ASSERT_GE (size, count / buckets / 2) << "stride " << stride << " buckets " << buckets << " bucket " << bucket;
				ASSERT_LE (size, count / buckets * 3 / 2) << "stride " << stride << " buckets " << buckets << " bucket " << bucket;
			}

			// The raw address never reaches every bucket of an even count, since the stride is even
			if (buckets % 2 == 0)
			{
				ASSERT_LT (raw.size (), buckets) << "stride " << stride << " buckets " << buckets;
			}
		}
	}
}

/*
 * The pointer hash is the mix of the address, with the null pointer at zero.
 */
TEST (hashing, pointer_hash_of_address)
{
	int object{};
	ASSERT_EQ (nano::mix64 (reinterpret_cast<uintptr_t> (&object)), nano::pointer_hash (&object));
	ASSERT_EQ (0, nano::pointer_hash (nullptr));
}
