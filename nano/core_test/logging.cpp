#include <nano/lib/files.hpp>
#include <nano/lib/formatting.hpp>
#include <nano/lib/logging.hpp>
#include <nano/test_common/testutil.hpp>

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

	nano::logger logger;
	logger.trace (nano::log::type::test, nano::log::detail::test, nano::log::arg{ "non_copyable", nc });
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

	nano::logger logger;
	logger.trace (nano::log::type::test, nano::log::detail::test, nano::log::arg{ "non_moveable", nm });
}

TEST (log_format, as_size)
{
	auto format = [] (std::uintmax_t value) {
		return fmt::format ("{}", nano::log::as_size (value));
	};

	ASSERT_EQ (format (0), "0 B");
	ASSERT_EQ (format (1023), "1023 B");
	ASSERT_EQ (format (1024), "1.00 KiB");
	ASSERT_EQ (format (1024 * 1024), "1.00 MiB");
	ASSERT_EQ (format (3ULL * 1024 * 1024 * 1024 / 2), "1.50 GiB");
	ASSERT_EQ (format (1024ULL * 1024 * 1024 * 1024), "1.00 TiB");
	// Largest unit is used even when the value overflows it
	ASSERT_EQ (format (2048ULL * 1024 * 1024 * 1024 * 1024), "2048.00 TiB");
}

TEST (log_format, as_disk_space)
{
	auto format = [] (std::uintmax_t capacity, std::uintmax_t available) {
		return fmt::format ("{}", nano::log::as_disk_space (nano::disk_space_info{ .capacity = capacity, .available = available }));
	};

	ASSERT_EQ (format (1024ULL * 1024 * 1024 * 1024, 512ULL * 1024 * 1024 * 1024), "512.00 GiB available of 1.00 TiB (50.0% free)");
	ASSERT_EQ (format (1024 * 1024, 0), "0 B available of 1.00 MiB (0.0% free)");
	// An unknown capacity must not divide by zero
	ASSERT_EQ (format (0, 0), "0 B available of 0 B (0.0% free)");
}

TEST (log_parse, parse_level)
{
	ASSERT_EQ (nano::log::parse_level ("error"), nano::log::level::error);
	ASSERT_EQ (nano::log::parse_level ("off"), nano::log::level::off);
	ASSERT_THROW (nano::log::parse_level ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_level (""), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_level ("_last"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_level ("_error"), std::invalid_argument);
}

TEST (log_parse, parse_type)
{
	ASSERT_EQ (nano::log::parse_type ("node"), nano::log::type::node);
	ASSERT_THROW (nano::log::parse_type ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_type (""), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_type ("_last"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_type ("_node"), std::invalid_argument);
}

TEST (log_parse, parse_detail)
{
	ASSERT_EQ (nano::log::parse_detail ("all"), nano::log::detail::all);
	ASSERT_EQ (nano::log::parse_detail ("test"), nano::log::detail::test);
	ASSERT_THROW (nano::log::parse_detail ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_detail (""), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_detail ("_last"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_detail ("_all"), std::invalid_argument);
}

TEST (log_parse, parse_logger_id)
{
	ASSERT_EQ (nano::log::parse_logger_id ("node"), std::make_pair (nano::log::type::node, nano::log::detail::all));
	ASSERT_EQ (nano::log::parse_logger_id ("node::all"), std::make_pair (nano::log::type::node, nano::log::detail::all));
	ASSERT_EQ (nano::log::parse_logger_id ("node::test"), std::make_pair (nano::log::type::node, nano::log::detail::test));
	ASSERT_THROW (nano::log::parse_logger_id ("_last"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("node::enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("node::"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("node::_all"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("invalid."), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("invalid._all"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("::"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id ("::all"), std::invalid_argument);
	ASSERT_THROW (nano::log::parse_logger_id (""), std::invalid_argument);
}