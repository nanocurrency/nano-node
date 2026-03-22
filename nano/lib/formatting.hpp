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
	auto format (T const & value, fmt::format_context & ctx) const
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
struct fmt::formatter<boost::asio::ip::address_v4> : fmt::ostream_formatter
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
struct fmt::formatter<nano::account> : fmt::ostream_formatter
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

// Custom formatter for boost::system::error_code for more informative logging
template <>
struct fmt::formatter<boost::system::error_code>
{
	auto parse (fmt::format_parse_context & ctx)
	{
		return ctx.begin ();
	}

	auto format (const boost::system::error_code & ec, fmt::format_context & ctx)
	{
		return fmt::format_to (ctx.out (), "{} {}:{}", ec.message (), ec.value (), ec.category ().name ());
	}
};

// Lazy formatting wrappers for public_key alternative representations
namespace nano::log
{
struct as_account_formatter
{
	nano::public_key const & key;

	friend std::ostream & operator<< (std::ostream & os, as_account_formatter const & wrapper)
	{
		wrapper.key.encode_account (os);
		return os;
	}
};

struct as_node_id_formatter
{
	nano::public_key const & key;

	friend std::ostream & operator<< (std::ostream & os, as_node_id_formatter const & wrapper)
	{
		wrapper.key.encode_node_id (os);
		return os;
	}
};

struct as_nano_formatter
{
	nano::uint128_t const & value;
	int precision{ 1 };

	friend std::ostream & operator<< (std::ostream & os, as_nano_formatter const & wrapper)
	{
		nano::encode_balance (os, wrapper.value, nano::nano_ratio, wrapper.precision, true);
		return os;
	}
};

struct as_raw_nano_formatter
{
	nano::uint128_t const & value;

	friend std::ostream & operator<< (std::ostream & os, as_raw_nano_formatter const & wrapper)
	{
		os << wrapper.value;
		return os;
	}
};

inline auto as_account (nano::public_key const & key)
{
	return as_account_formatter{ key };
}
inline auto as_node_id (nano::public_key const & key)
{
	return as_node_id_formatter{ key };
}
inline auto as_nano (nano::uint128_t const & value, int precision = 1)
{
	return as_nano_formatter{ value, precision };
}
inline auto as_raw_nano (nano::uint128_t const & value)
{
	return as_raw_nano_formatter{ value };
}
}

template <>
struct fmt::formatter<nano::log::as_account_formatter> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::log::as_node_id_formatter> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::log::as_nano_formatter> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::log::as_raw_nano_formatter> : fmt::ostream_formatter
{
};