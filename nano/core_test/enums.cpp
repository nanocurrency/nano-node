#include <nano/lib/stats_enums.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (enums, stat_type)
{
	ASSERT_FALSE (nano::to_string (static_cast<nano::stat::type> (0)).empty ());
	ASSERT_NO_THROW (std::string{ nano::to_string (static_cast<nano::stat::type> (0)) });

	ASSERT_FALSE (nano::to_string (nano::stat::type::_last).empty ());
	ASSERT_NO_THROW (std::string{ nano::to_string (nano::stat::type::_last) });
	ASSERT_EQ (nano::to_string (nano::stat::type::_last), "_last");
}

TEST (enums, stat_detail)
{
	ASSERT_FALSE (nano::to_string (static_cast<nano::stat::detail> (0)).empty ());
	ASSERT_NO_THROW (std::string{ nano::to_string (static_cast<nano::stat::detail> (0)) });

	ASSERT_FALSE (nano::to_string (nano::stat::detail::_last).empty ());
	ASSERT_NO_THROW (std::string{ nano::to_string (nano::stat::detail::_last) });
	ASSERT_EQ (nano::to_string (nano::stat::detail::_last), "_last");
}

TEST (enums, stat_dir)
{
	ASSERT_FALSE (nano::to_string (static_cast<nano::stat::dir> (0)).empty ());
	ASSERT_NO_THROW (std::string{ nano::to_string (static_cast<nano::stat::dir> (0)) });

	ASSERT_FALSE (nano::to_string (nano::stat::dir::_last).empty ());
	ASSERT_NO_THROW (std::string{ nano::to_string (nano::stat::dir::_last) });
	ASSERT_EQ (nano::to_string (nano::stat::dir::_last), "_last");
}

TEST (enums, log_type)
{
	ASSERT_FALSE (to_string (static_cast<nano::log::type> (0)).empty ());
	ASSERT_NO_THROW (std::string{ to_string (static_cast<nano::log::type> (0)) });

	ASSERT_FALSE (to_string (nano::log::type::_last).empty ());
	ASSERT_NO_THROW (std::string{ to_string (nano::log::type::_last) });
	ASSERT_EQ (to_string (nano::log::type::_last), "_last");
}

TEST (enums, log_detail)
{
	ASSERT_FALSE (to_string (static_cast<nano::log::detail> (0)).empty ());
	ASSERT_NO_THROW (std::string{ to_string (static_cast<nano::log::detail> (0)) });

	ASSERT_FALSE (to_string (nano::log::detail::_last).empty ());
	ASSERT_NO_THROW (std::string{ to_string (nano::log::detail::_last) });
	ASSERT_EQ (to_string (nano::log::detail::_last), "_last");
}

TEST (enums, log_category)
{
	ASSERT_FALSE (to_string (static_cast<nano::log::type> (0)).empty ());
	ASSERT_NO_THROW (std::string{ to_string (static_cast<nano::log::type> (0)) });

	ASSERT_FALSE (to_string (nano::log::type::_last).empty ());
	ASSERT_NO_THROW (std::string{ to_string (nano::log::type::_last) });
	ASSERT_EQ (to_string (nano::log::type::_last), "_last");
}