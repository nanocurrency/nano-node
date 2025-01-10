#include <celerix/lib/logging.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <ostream>

using namespace std::chrono_literals;

namespace
{
struct non_copyable
{
	non_copyable () = default;
	non_copyable (non_copyable const &) = delete;
	non_copyable (non_copyable &&) = default;
	non_copyable & operator= (non_copyable const &) = delete;
	non_copyable & operator= (non_copyable &&) = default;

	friend std::ostream & operator<< (std::ostream & os, non_copyable const & nc)
	{
		os << "non_copyable";
		return os;
	}
};
}

TEST (tracing, no_copy)
{
	non_copyable nc;

	celerix::logger logger;
	logger.trace (celerix::log::type::test, celerix::log::detail::test, celerix::log::arg{ "non_copyable", nc });
}

namespace
{
struct non_moveable
{
	non_moveable () = default;
	non_moveable (non_moveable const &) = delete;
	non_moveable (non_moveable &&) = delete;
	non_moveable & operator= (non_moveable const &) = delete;
	non_moveable & operator= (non_moveable &&) = delete;

	friend std::ostream & operator<< (std::ostream & os, non_moveable const & nm)
	{
		os << "non_moveable";
		return os;
	}
};
}

TEST (tracing, no_move)
{
	non_moveable nm;

	celerix::logger logger;
	logger.trace (celerix::log::type::test, celerix::log::detail::test, celerix::log::arg{ "non_moveable", nm });
}

TEST (log_parse, parse_level)
{
	ASSERT_EQ (celerix::log::parse_level ("error"), celerix::log::level::error);
	ASSERT_EQ (celerix::log::parse_level ("off"), celerix::log::level::off);
	ASSERT_THROW (celerix::log::parse_level ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_level (""), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_level ("_last"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_level ("_error"), std::invalid_argument);
}

TEST (log_parse, parse_type)
{
	ASSERT_EQ (celerix::log::parse_type ("node"), celerix::log::type::node);
	ASSERT_THROW (celerix::log::parse_type ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_type (""), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_type ("_last"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_type ("_node"), std::invalid_argument);
}

TEST (log_parse, parse_detail)
{
	ASSERT_EQ (celerix::log::parse_detail ("all"), celerix::log::detail::all);
	ASSERT_EQ (celerix::log::parse_detail ("process_confirmed"), celerix::log::detail::process_confirmed);
	ASSERT_THROW (celerix::log::parse_detail ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_detail (""), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_detail ("_last"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_detail ("_all"), std::invalid_argument);
}

TEST (log_parse, parse_logger_id)
{
	ASSERT_EQ (celerix::log::parse_logger_id ("node"), std::make_pair (celerix::log::type::node, celerix::log::detail::all));
	ASSERT_EQ (celerix::log::parse_logger_id ("node::all"), std::make_pair (celerix::log::type::node, celerix::log::detail::all));
	ASSERT_EQ (celerix::log::parse_logger_id ("node::process_confirmed"), std::make_pair (celerix::log::type::node, celerix::log::detail::process_confirmed));
	ASSERT_THROW (celerix::log::parse_logger_id ("_last"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("node::enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("node::"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("node::_all"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("invalid."), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("invalid._all"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("::"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id ("::all"), std::invalid_argument);
	ASSERT_THROW (celerix::log::parse_logger_id (""), std::invalid_argument);
}