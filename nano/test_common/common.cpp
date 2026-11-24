#include <nano/lib/env.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/test_common/common.hpp>

nano::logger & nano::test::default_logger ()
{
	static nano::logger logger{ "tests" };
	return logger;
}

nano::stats & nano::test::default_stats ()
{
	static nano::stats stats{ nano::test::default_logger () };
	return stats;
}

double nano::test::deadline_scaling_factor ()
{
	static double const factor = nano::env::get<double> ("NANO_TEST_DEADLINE_SCALE_FACTOR").value_or (1.0);
	return factor;
}