#pragma once

#include <celerix/lib/numbers.hpp>

#include <boost/functional/hash.hpp>

namespace std
{
template <>
struct hash<::celerix::uint128_union>
{
	size_t operator() (::celerix::uint128_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1];
	}
};
template <>
struct hash<::celerix::uint256_union>
{
	size_t operator() (::celerix::uint256_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1] + value.qwords[2] + value.qwords[3];
	}
};
template <>
struct hash<::celerix::public_key>
{
	size_t operator() (::celerix::public_key const & value) const noexcept
	{
		return hash<::celerix::uint256_union>{}(value);
	}
};
template <>
struct hash<::celerix::block_hash>
{
	size_t operator() (::celerix::block_hash const & value) const noexcept
	{
		return hash<::celerix::uint256_union>{}(value);
	}
};
template <>
struct hash<::celerix::hash_or_account>
{
	size_t operator() (::celerix::hash_or_account const & value) const noexcept
	{
		return hash<::celerix::block_hash>{}(value.as_block_hash ());
	}
};
template <>
struct hash<::celerix::root>
{
	size_t operator() (::celerix::root const & value) const noexcept
	{
		return hash<::celerix::hash_or_account>{}(value);
	}
};
template <>
struct hash<::celerix::link>
{
	size_t operator() (::celerix::link const & value) const noexcept
	{
		return hash<::celerix::hash_or_account>{}(value);
	}
};
template <>
struct hash<::celerix::raw_key>
{
	size_t operator() (::celerix::raw_key const & value) const noexcept
	{
		return hash<::celerix::uint256_union>{}(value);
	}
};
template <>
struct hash<::celerix::wallet_id>
{
	size_t operator() (::celerix::wallet_id const & value) const noexcept
	{
		return hash<::celerix::uint256_union>{}(value);
	}
};
template <>
struct hash<::celerix::uint512_union>
{
	size_t operator() (::celerix::uint512_union const & value) const noexcept
	{
		return hash<::celerix::uint256_union>{}(value.uint256s[0]) + hash<::celerix::uint256_union> () (value.uint256s[1]);
	}
};
template <>
struct hash<::celerix::qualified_root>
{
	size_t operator() (::celerix::qualified_root const & value) const noexcept
	{
		return hash<::celerix::uint512_union>{}(value);
	}
};
}

namespace boost
{
template <>
struct hash<::celerix::uint128_union>
{
	size_t operator() (::celerix::uint128_union const & value) const noexcept
	{
		return std::hash<::celerix::uint128_union> () (value);
	}
};
template <>
struct hash<::celerix::uint256_union>
{
	size_t operator() (::celerix::uint256_union const & value) const noexcept
	{
		return std::hash<::celerix::uint256_union> () (value);
	}
};
template <>
struct hash<::celerix::public_key>
{
	size_t operator() (::celerix::public_key const & value) const noexcept
	{
		return std::hash<::celerix::public_key> () (value);
	}
};
template <>
struct hash<::celerix::block_hash>
{
	size_t operator() (::celerix::block_hash const & value) const noexcept
	{
		return std::hash<::celerix::block_hash> () (value);
	}
};
template <>
struct hash<::celerix::hash_or_account>
{
	size_t operator() (::celerix::hash_or_account const & value) const noexcept
	{
		return std::hash<::celerix::hash_or_account> () (value);
	}
};
template <>
struct hash<::celerix::root>
{
	size_t operator() (::celerix::root const & value) const noexcept
	{
		return std::hash<::celerix::root> () (value);
	}
};
template <>
struct hash<::celerix::link>
{
	size_t operator() (::celerix::link const & value) const noexcept
	{
		return std::hash<::celerix::link> () (value);
	}
};
template <>
struct hash<::celerix::raw_key>
{
	size_t operator() (::celerix::raw_key const & value) const noexcept
	{
		return std::hash<::celerix::raw_key> () (value);
	}
};
template <>
struct hash<::celerix::wallet_id>
{
	size_t operator() (::celerix::wallet_id const & value) const noexcept
	{
		return std::hash<::celerix::wallet_id> () (value);
	}
};
template <>
struct hash<::celerix::uint512_union>
{
	size_t operator() (::celerix::uint512_union const & value) const noexcept
	{
		return std::hash<::celerix::uint512_union> () (value);
	}
};
template <>
struct hash<::celerix::qualified_root>
{
	size_t operator() (::celerix::qualified_root const & value) const noexcept
	{
		return std::hash<::celerix::qualified_root> () (value);
	}
};
}
