#include <nano/lib/enum_flags_templ.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <sstream>

TEST (enums, stat_type)
{
	ASSERT_FALSE (to_string (static_cast<nano::stat::type> (0)).empty ());
	ASSERT_NO_THROW (std::string{ to_string (static_cast<nano::stat::type> (0)) });

	ASSERT_FALSE (to_string (nano::stat::type::_last).empty ());
	ASSERT_NO_THROW (std::string{ to_string (nano::stat::type::_last) });
	ASSERT_EQ (to_string (nano::stat::type::_last), "_last");
}

TEST (enums, stat_detail)
{
	ASSERT_FALSE (to_string (static_cast<nano::stat::detail> (0)).empty ());
	ASSERT_NO_THROW (std::string{ to_string (static_cast<nano::stat::detail> (0)) });

	ASSERT_FALSE (to_string (nano::stat::detail::_last).empty ());
	ASSERT_NO_THROW (std::string{ to_string (nano::stat::detail::_last) });
	ASSERT_EQ (to_string (nano::stat::detail::_last), "_last");
}

TEST (enums, stat_dir)
{
	ASSERT_FALSE (to_string (static_cast<nano::stat::dir> (0)).empty ());
	ASSERT_NO_THROW (std::string{ to_string (static_cast<nano::stat::dir> (0)) });

	ASSERT_FALSE (to_string (nano::stat::dir::_last).empty ());
	ASSERT_NO_THROW (std::string{ to_string (nano::stat::dir::_last) });
	ASSERT_EQ (to_string (nano::stat::dir::_last), "_last");
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
	two,
	three,
};
}

TEST (enum_util, name)
{
	ASSERT_EQ (nano::enum_to_string (test_enum::_invalid), "_invalid");
	ASSERT_EQ (nano::enum_to_string (test_enum::one), "one");
	ASSERT_EQ (nano::enum_to_string (test_enum::two), "two");
	ASSERT_EQ (nano::enum_to_string (test_enum::three), "three");
	ASSERT_EQ (nano::enum_to_string (test_enum::_last), "_last");
}

TEST (enum_util, values)
{
	auto values = nano::enum_values<test_enum> ();
	ASSERT_EQ (values.size (), 3);
	ASSERT_EQ (values[0], test_enum::one);
	ASSERT_EQ (values[1], test_enum::two);
	ASSERT_EQ (values[2], test_enum::three);

	auto all_values = nano::enum_values<test_enum, /* don't ignore reserved */ false> ();
	ASSERT_EQ (all_values.size (), 5);
	ASSERT_EQ (all_values[0], test_enum::_invalid);
	ASSERT_EQ (all_values[1], test_enum::one);
	ASSERT_EQ (all_values[2], test_enum::two);
	ASSERT_EQ (all_values[3], test_enum::three);
	ASSERT_EQ (all_values[4], test_enum::_last);
}

TEST (enum_util, parse)
{
	ASSERT_EQ (nano::enum_try_parse<test_enum> ("one"), test_enum::one);
	ASSERT_EQ (nano::enum_try_parse<test_enum> ("two"), test_enum::two);
	ASSERT_EQ (nano::enum_try_parse<test_enum> ("three"), test_enum::three);
	ASSERT_FALSE (nano::enum_try_parse<test_enum> ("four").has_value ());
	ASSERT_FALSE (nano::enum_try_parse<test_enum> ("_invalid").has_value ());
	ASSERT_FALSE (nano::enum_try_parse<test_enum> ("_last").has_value ());

	ASSERT_NO_THROW (nano::enum_parse<test_enum> ("one"));
	ASSERT_THROW (nano::enum_parse<test_enum> ("four"), std::invalid_argument);
	ASSERT_THROW (nano::enum_parse<test_enum> ("_invalid"), std::invalid_argument);
}

TEST (enum_util, convert)
{
	ASSERT_EQ (nano::enum_convert<test_enum> (test_enum2::one), test_enum::one);
	ASSERT_EQ (nano::enum_convert<test_enum> (test_enum2::two), test_enum::two);
	ASSERT_EQ (nano::enum_convert<test_enum> (test_enum2::three), test_enum::three);
}

namespace
{
enum class test_flags : uint8_t
{
	none = 0,
	alpha = 1 << 0,
	beta = 1 << 1,
	gamma = 1 << 2,
};

std::string_view to_string (test_flags value)
{
	switch (value)
	{
		case test_flags::none:
			return "none";
		case test_flags::alpha:
			return "alpha";
		case test_flags::beta:
			return "beta";
		case test_flags::gamma:
			return "gamma";
	}
	return "unknown";
}

}

TEST (enum_flags, default_construction)
{
	nano::enum_flags<test_flags> flags;
	ASSERT_TRUE (flags.none ());
	ASSERT_FALSE (flags.any ());
	ASSERT_FALSE (static_cast<bool> (flags));
}

TEST (enum_flags, single_flag_construction)
{
	nano::enum_flags<test_flags> flags{ test_flags::alpha };
	ASSERT_TRUE (flags.test (test_flags::alpha));
	ASSERT_FALSE (flags.test (test_flags::beta));
	ASSERT_TRUE (flags.any ());
	ASSERT_FALSE (flags.none ());
}

TEST (enum_flags, set_reset)
{
	nano::enum_flags<test_flags> flags;
	flags.set (test_flags::alpha);
	flags.set (test_flags::gamma);
	ASSERT_TRUE (flags.test (test_flags::alpha));
	ASSERT_FALSE (flags.test (test_flags::beta));
	ASSERT_TRUE (flags.test (test_flags::gamma));

	flags.reset (test_flags::alpha);
	ASSERT_FALSE (flags.test (test_flags::alpha));
	ASSERT_TRUE (flags.test (test_flags::gamma));
}

TEST (enum_flags, operators)
{
	nano::enum_flags<test_flags> a{ test_flags::alpha };
	nano::enum_flags<test_flags> b{ test_flags::beta };
	auto combined = a | b;
	ASSERT_TRUE (combined.test (test_flags::alpha));
	ASSERT_TRUE (combined.test (test_flags::beta));
	ASSERT_FALSE (combined.test (test_flags::gamma));

	auto masked = combined & a;
	ASSERT_TRUE (masked.test (test_flags::alpha));
	ASSERT_FALSE (masked.test (test_flags::beta));
}

TEST (enum_flags, compound_assignment)
{
	nano::enum_flags<test_flags> flags;
	flags |= test_flags::alpha;
	flags |= test_flags::beta;
	ASSERT_TRUE (flags.test (test_flags::alpha));
	ASSERT_TRUE (flags.test (test_flags::beta));

	flags &= test_flags::alpha;
	ASSERT_TRUE (flags.test (test_flags::alpha));
	ASSERT_FALSE (flags.test (test_flags::beta));
}

TEST (enum_flags, equality)
{
	nano::enum_flags<test_flags> a{ test_flags::alpha };
	nano::enum_flags<test_flags> b{ test_flags::alpha };
	nano::enum_flags<test_flags> c{ test_flags::beta };
	ASSERT_EQ (a, b);
	ASSERT_NE (a, c);
}

TEST (enum_flags, from_underlying)
{
	auto flags = nano::enum_flags<test_flags>::from_underlying (0x03); // alpha | beta
	ASSERT_TRUE (flags.test (test_flags::alpha));
	ASSERT_TRUE (flags.test (test_flags::beta));
	ASSERT_FALSE (flags.test (test_flags::gamma));
	ASSERT_EQ (flags.underlying (), 0x03);
}

TEST (enum_flags, to_string_none)
{
	nano::enum_flags<test_flags> flags;
	ASSERT_EQ (to_string (flags), "<none>");
}

TEST (enum_flags, to_string_single)
{
	nano::enum_flags<test_flags> flags{ test_flags::beta };
	ASSERT_EQ (to_string (flags), "beta");
}

TEST (enum_flags, to_string_multiple)
{
	nano::enum_flags<test_flags> flags;
	flags.set (test_flags::alpha);
	flags.set (test_flags::gamma);
	ASSERT_EQ (to_string (flags), "alpha, gamma");
}

TEST (enum_flags, to_string_all)
{
	nano::enum_flags<test_flags> flags;
	flags.set (test_flags::alpha);
	flags.set (test_flags::beta);
	flags.set (test_flags::gamma);
	ASSERT_EQ (to_string (flags), "alpha, beta, gamma");
}