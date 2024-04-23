#include <nano/lib/blocks.hpp>
#include <nano/node/scheduler/bucket.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

#include <unordered_set>

nano::keypair & keyzero ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key0 ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key1 ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key2 ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key3 ()
{
	static nano::keypair result;
	return result;
}
std::shared_ptr<nano::state_block> & blockzero ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (keyzero ().pub)
						 .previous (0)
						 .representative (keyzero ().pub)
						 .balance (0)
						 .link (0)
						 .sign (keyzero ().prv, keyzero ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block0 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key0 ().pub)
						 .previous (0)
						 .representative (key0 ().pub)
						 .balance (nano::Gxrb_ratio)
						 .link (0)
						 .sign (key0 ().prv, key0 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block1 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key1 ().pub)
						 .previous (0)
						 .representative (key1 ().pub)
						 .balance (nano::Mxrb_ratio)
						 .link (0)
						 .sign (key1 ().prv, key1 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block2 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key2 ().pub)
						 .previous (0)
						 .representative (key2 ().pub)
						 .balance (nano::Gxrb_ratio)
						 .link (0)
						 .sign (key2 ().prv, key2 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block3 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key3 ().pub)
						 .previous (0)
						 .representative (key3 ().pub)
						 .balance (nano::Mxrb_ratio)
						 .link (0)
						 .sign (key3 ().prv, key3 ().pub)
						 .work (0)
						 .build ();
	return result;
}

TEST (bucket, construction)
{
	nano::scheduler::bucket bucket{ 0 };
}

TEST (bucket, insert_zero)
{
	nano::scheduler::bucket bucket{ 0 };
	auto block = block0 ();
	auto drop = bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 0 }, block);
	ASSERT_EQ (drop, block);
}

TEST (bucket, push_available)
{
	nano::scheduler::bucket bucket{ 1 };
	auto block = block0 ();
	ASSERT_EQ (nullptr, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 1000 }, block));
}

TEST (bucket, push_overflow_other)
{
	nano::scheduler::bucket bucket{ 1 };
	auto block0 = ::block0 ();
	auto block1 = ::block1 ();
	ASSERT_EQ (nullptr, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 1000 }, block0));
	ASSERT_EQ (block0, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 900 }, block1));
}

TEST (bucket, push_overflow_self)
{
	nano::scheduler::bucket bucket{ 1 };
	auto block0 = ::block0 ();
	auto block1 = ::block1 ();
	ASSERT_EQ (nullptr, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 1000 }, block0));
	ASSERT_EQ (block1, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 1100 }, block1));
}

// Inserting duplicate block should not return an overflow or reject
TEST (bucket, accept_duplicate)
{
	nano::scheduler::bucket bucket{ 1 };
	auto block0 = ::block0 ();
	ASSERT_EQ (nullptr, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 1000 }, block0));
	ASSERT_EQ (nullptr, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 900 }, block0));
	ASSERT_EQ (nullptr, bucket.insert (std::chrono::steady_clock::time_point{} + std::chrono::milliseconds{ 1100 }, block0));
}
