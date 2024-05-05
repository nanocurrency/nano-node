#include <nano/lib/enum_util.hpp>
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

namespace
{
enum class test_enum
{
	_invalid,
	one,
	two,
	three,
	_last
};

enum class test_enum2
{
	one,
};
}

TEST (enum_util, name)
{
	ASSERT_EQ (nano::enum_util::name (test_enum::_invalid), "_invalid");
	ASSERT_EQ (nano::enum_util::name (test_enum::one), "one");
	ASSERT_EQ (nano::enum_util::name (test_enum::two), "two");
	ASSERT_EQ (nano::enum_util::name (test_enum::three), "three");
	ASSERT_EQ (nano::enum_util::name (test_enum::_last), "_last");
}

TEST (enum_util, values)
{
	auto values = nano::enum_util::values<test_enum> ();
	ASSERT_EQ (values.size (), 3);
	ASSERT_EQ (values[0], test_enum::one);
	ASSERT_EQ (values[1], test_enum::two);
	ASSERT_EQ (values[2], test_enum::three);

	auto all_values = nano::enum_util::values<test_enum> (/* don't ignore reserved */ false);
	ASSERT_EQ (all_values.size (), 5);
	ASSERT_EQ (all_values[0], test_enum::_invalid);
	ASSERT_EQ (all_values[1], test_enum::one);
	ASSERT_EQ (all_values[2], test_enum::two);
	ASSERT_EQ (all_values[3], test_enum::three);
	ASSERT_EQ (all_values[4], test_enum::_last);
}

TEST (enum_util, parse)
{
	ASSERT_EQ (nano::enum_util::try_parse<test_enum> ("one"), test_enum::one);
	ASSERT_EQ (nano::enum_util::try_parse<test_enum> ("two"), test_enum::two);
	ASSERT_EQ (nano::enum_util::try_parse<test_enum> ("three"), test_enum::three);
	ASSERT_FALSE (nano::enum_util::try_parse<test_enum> ("four").has_value ());
	ASSERT_FALSE (nano::enum_util::try_parse<test_enum> ("_invalid").has_value ());
	ASSERT_FALSE (nano::enum_util::try_parse<test_enum> ("_last").has_value ());

	ASSERT_NO_THROW (nano::enum_util::parse<test_enum> ("one"));
	ASSERT_THROW (nano::enum_util::parse<test_enum> ("four"), std::invalid_argument);
	ASSERT_THROW (nano::enum_util::parse<test_enum> ("_invalid"), std::invalid_argument);
}

TEST (enum_util, cast)
{
	ASSERT_EQ (nano::enum_util::cast<test_enum> (test_enum2::one), test_enum::one);
}