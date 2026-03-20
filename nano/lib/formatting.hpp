#pragma once

#include <nano/lib/common.hpp>
#include <nano/lib/numbers.hpp>

#include <boost/system/error_code.hpp>

#include <concepts>

#include <fmt/format.h>
#include <fmt/ostream.h>

// Generic formatter for enums with an ADL-findable to_string() returning string_view
template <typename T>
	requires (std::is_enum_v<T> && requires (T const & t) {
		{
			to_string (t)
		} -> std::same_as<std::string_view>;
	})
struct fmt::formatter<T> : fmt::formatter<std::string_view>
{
	auto format (T const & value, format_context & ctx) const
	{
		return fmt::formatter<std::string_view>::format (to_string (value), ctx);
	}
};

template <>
struct fmt::formatter<nano::endpoint> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::ip_address> : fmt::ostream_formatter
{
};

template <>
struct fmt::formatter<nano::uint128_t> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::uint256_t> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::uint512_t> : fmt::ostream_formatter
{
};

template <>
struct fmt::formatter<nano::uint128_union> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::uint256_union> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::uint512_union> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::hash_or_account> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::block_hash> : fmt::formatter<nano::uint256_union>
{
};
template <>
struct fmt::formatter<nano::public_key> : fmt::formatter<nano::uint256_union>
{
};
template <>
struct fmt::formatter<nano::qualified_root> : fmt::formatter<nano::uint512_union>
{
};
template <>
struct fmt::formatter<nano::root> : fmt::formatter<nano::hash_or_account>
{
};
template <>
struct fmt::formatter<nano::wallet_id> : fmt::formatter<nano::uint256_union>
{
};

template <>
struct fmt::formatter<boost::system::error_code> : fmt::formatter<std::string>
{
	auto format (const boost::system::error_code & ec, format_context & ctx)
	{
		return fmt::format_to (ctx.out (), "{} {}:{}", ec.message (), ec.value (), ec.category ().name ());
	}
};