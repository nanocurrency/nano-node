#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/nodeconfig.hpp>

#include <gtest/gtest.h>

/*
 * The node's limiter keeps one bucket for bootstrap server traffic and one for everything else, each sized by its own limit and burst ratio
 */
TEST (bandwidth_limiter, buckets_per_traffic_type)
{
	using nano::transport::traffic_type;
	nano::node_config config;
	config.bandwidth_limit = 1000;
	config.bandwidth_limit_burst_ratio = 4.0;
	config.bootstrap_bandwidth_limit = 500;
	config.bootstrap_bandwidth_burst_ratio = 1.0;
	nano::bandwidth_limiter limiter{ config, nano::node_flags{} };

	// The generic bucket starts full at four times its rate, which one request of that size drains
	ASSERT_EQ (1000, limiter.get_limit ().rate);
	ASSERT_EQ (4.0, limiter.get_limit ().burst_ratio);
	ASSERT_TRUE (limiter.try_consume (4000, traffic_type::generic));
	ASSERT_FALSE (limiter.try_consume (1000, traffic_type::vote));

	// Bootstrap client traffic draws on the generic bucket, bootstrap server traffic on its own
	ASSERT_FALSE (limiter.try_consume (500, traffic_type::bootstrap));
	ASSERT_EQ (500, limiter.get_limit (traffic_type::bootstrap_server).rate);
	ASSERT_TRUE (limiter.try_consume (500, traffic_type::bootstrap_server));
	ASSERT_FALSE (limiter.try_consume (500, traffic_type::bootstrap_server));

	// A refused request says how long until it would pass
	auto const result = limiter.consume (1000, traffic_type::generic);
	ASSERT_FALSE (result);
	ASSERT_GT (result.retry_after.count (), 0);
	ASSERT_LE (result.retry_after.count (), 1000);
}

/*
 * The super rebroadcaster flag lifts the generic limit and leaves the bootstrap server limit in place
 */
TEST (bandwidth_limiter, super_rebroadcaster)
{
	using nano::transport::traffic_type;
	nano::node_config config;
	config.bandwidth_limit = 1000;
	config.bootstrap_bandwidth_limit = 500;
	nano::node_flags flags;
	flags.super_rebroadcaster = true;
	nano::bandwidth_limiter limiter{ config, flags };

	// A rate of zero is no limit, so a request far above the configured limit passes
	ASSERT_EQ (0, limiter.get_limit ().rate);
	ASSERT_TRUE (limiter.try_consume (1000000, traffic_type::generic));

	// Bootstrap server traffic is still held to its limit
	ASSERT_EQ (500, limiter.get_limit (traffic_type::bootstrap_server).rate);
	ASSERT_TRUE (limiter.try_consume (500, traffic_type::bootstrap_server));
	ASSERT_FALSE (limiter.try_consume (500, traffic_type::bootstrap_server));
}
