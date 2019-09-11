#include <nano/secure/common.hpp>
#include <nano/secure/epoch.hpp>

#include <gtest/gtest.h>

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
}
