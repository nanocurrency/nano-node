#include <gtest/gtest.h>

#include <nano/secure/common.hpp>
#include <nano/secure/epoch.hpp>

TEST (epochs, is_epoch_link)
{
	nano::epochs epochs;
	nano::keypair key;
	ASSERT_FALSE (epochs.is_epoch_link (42));
	epochs.add (nano::epoch::epoch_1, key.pub, 42);
	ASSERT_TRUE (epochs.is_epoch_link (42));
	ASSERT_EQ (key.pub, epochs.signer (42));
	ASSERT_EQ (nano::uint256_union (42), epochs.link (nano::epoch::epoch_1));
}
