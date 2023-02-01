#include <nano/lib/stats_enums.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (enums, stat_type)
{
	ASSERT_FALSE (nano::to_string (static_cast<nano::stat::type> (0)).empty ());
	ASSERT_FALSE (nano::to_string (nano::stat::type::_last).empty ());
}

TEST (enums, stat_detail)
{
	ASSERT_FALSE (nano::to_string (static_cast<nano::stat::detail> (0)).empty ());
	ASSERT_FALSE (nano::to_string (nano::stat::detail::_last).empty ());
}

TEST (enums, stat_dir)
{
	ASSERT_FALSE (nano::to_string (static_cast<nano::stat::dir> (0)).empty ());
	ASSERT_FALSE (nano::to_string (nano::stat::dir::_last).empty ());
}