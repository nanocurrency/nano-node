#include <nano/lib/epoch.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

TEST (epochs, is_epoch_link)
{
	nano::epochs epochs;
	// Test epoch 1
	nano::keypair key1;
	auto link1 = 42;
	auto link2 = 43;
	ASSERT_FALSE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	epochs.add (nano::epoch::epoch_1, key1.pub, link1);
	ASSERT_TRUE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key1.pub, epochs.signer (nano::epoch::epoch_1));
	ASSERT_EQ (epochs.epoch (link1), nano::epoch::epoch_1);

	// Test epoch 2
	nano::keypair key2;
	epochs.add (nano::epoch::epoch_2, key2.pub, link2);
	ASSERT_TRUE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key2.pub, epochs.signer (nano::epoch::epoch_2));
	ASSERT_EQ (nano::uint256_union (link1), epochs.link (nano::epoch::epoch_1));
	ASSERT_EQ (nano::uint256_union (link2), epochs.link (nano::epoch::epoch_2));
	ASSERT_EQ (epochs.epoch (link2), nano::epoch::epoch_2);
}

TEST (epochs, is_sequential)
{
	ASSERT_TRUE (nano::epochs::is_sequential (nano::epoch::epoch_0, nano::epoch::epoch_1));
	ASSERT_TRUE (nano::epochs::is_sequential (nano::epoch::epoch_1, nano::epoch::epoch_2));

	ASSERT_FALSE (nano::epochs::is_sequential (nano::epoch::epoch_0, nano::epoch::epoch_2));
	ASSERT_FALSE (nano::epochs::is_sequential (nano::epoch::epoch_0, nano::epoch::invalid));
	ASSERT_FALSE (nano::epochs::is_sequential (nano::epoch::unspecified, nano::epoch::epoch_1));
	ASSERT_FALSE (nano::epochs::is_sequential (nano::epoch::epoch_1, nano::epoch::epoch_0));
	ASSERT_FALSE (nano::epochs::is_sequential (nano::epoch::epoch_2, nano::epoch::epoch_0));
	ASSERT_FALSE (nano::epochs::is_sequential (nano::epoch::epoch_2, nano::epoch::epoch_2));
}
