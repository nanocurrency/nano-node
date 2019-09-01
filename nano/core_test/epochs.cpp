#include <gtest/gtest.h>

#include <nano/secure/common.hpp>
#include <nano/secure/epoch.hpp>

TEST (epochs, is_epoch_link)
{
	nano::epochs epochs;
	nano::keypair key1;
	nano::keypair key2;
	ASSERT_FALSE (epochs.is_epoch_link (42));
	ASSERT_FALSE (epochs.is_epoch_link (43));
	epochs.add (nano::epoch::epoch_1, key1.pub, 42);
	ASSERT_TRUE (epochs.is_epoch_link (42));
	ASSERT_FALSE (epochs.is_epoch_link (43));
	epochs.add (nano::epoch::epoch_2, key2.pub, 43);
	ASSERT_TRUE (epochs.is_epoch_link (43));
	ASSERT_EQ (key1.pub, epochs.signer (nano::epoch::epoch_1));
	ASSERT_EQ (key2.pub, epochs.signer (nano::epoch::epoch_2));
	ASSERT_EQ (nano::uint256_union (42), epochs.link (nano::epoch::epoch_1));
	ASSERT_EQ (nano::uint256_union (43), epochs.link (nano::epoch::epoch_2));
	
}
