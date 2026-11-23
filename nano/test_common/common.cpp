#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/test_common/common.hpp>

#include <cstdlib>
#include <string>

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
	static double const factor = [] () {
		auto const scale_str = std::getenv ("DEADLINE_SCALE_FACTOR");
		return scale_str ? std::stod (scale_str) : 1.0;
	}();
	return factor;
}