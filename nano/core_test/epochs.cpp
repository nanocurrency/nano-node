#include <celerix/lib/epoch.hpp>
#include <celerix/secure/common.hpp>

#include <gtest/gtest.h>

TEST (epochs, is_epoch_link)
{
	celerix::epochs epochs;
	// Test epoch 1
	celerix::keypair key1;
	auto link1 = 42;
	auto link2 = 43;
	ASSERT_FALSE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	epochs.add (celerix::epoch::epoch_1, key1.pub, link1);
	ASSERT_TRUE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key1.pub, epochs.signer (celerix::epoch::epoch_1));
	ASSERT_EQ (epochs.epoch (link1), celerix::epoch::epoch_1);

	// Test epoch 2
	celerix::keypair key2;
	epochs.add (celerix::epoch::epoch_2, key2.pub, link2);
	ASSERT_TRUE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key2.pub, epochs.signer (celerix::epoch::epoch_2));
	ASSERT_EQ (celerix::uint256_union (link1), epochs.link (celerix::epoch::epoch_1));
	ASSERT_EQ (celerix::uint256_union (link2), epochs.link (celerix::epoch::epoch_2));
	ASSERT_EQ (epochs.epoch (link2), celerix::epoch::epoch_2);
}

TEST (epochs, is_sequential)
{
	ASSERT_TRUE (celerix::epochs::is_sequential (celerix::epoch::epoch_0, celerix::epoch::epoch_1));
	ASSERT_TRUE (celerix::epochs::is_sequential (celerix::epoch::epoch_1, celerix::epoch::epoch_2));

	ASSERT_FALSE (celerix::epochs::is_sequential (celerix::epoch::epoch_0, celerix::epoch::epoch_2));
	ASSERT_FALSE (celerix::epochs::is_sequential (celerix::epoch::epoch_0, celerix::epoch::invalid));
	ASSERT_FALSE (celerix::epochs::is_sequential (celerix::epoch::unspecified, celerix::epoch::epoch_1));
	ASSERT_FALSE (celerix::epochs::is_sequential (celerix::epoch::epoch_1, celerix::epoch::epoch_0));
	ASSERT_FALSE (celerix::epochs::is_sequential (celerix::epoch::epoch_2, celerix::epoch::epoch_0));
	ASSERT_FALSE (celerix::epochs::is_sequential (celerix::epoch::epoch_2, celerix::epoch::epoch_2));
}
