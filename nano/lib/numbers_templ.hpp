#pragma once

#include <nano/lib/numbers.hpp>

#include <boost/functional/hash.hpp>

namespace std
{
template <>
struct hash<::nano::uint128_union>
{
	size_t operator() (::nano::uint128_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1];
	}
};
template <>
struct hash<::nano::uint256_union>
{
	size_t operator() (::nano::uint256_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1] + value.qwords[2] + value.qwords[3];
	}
};
template <>
struct hash<::nano::public_key>
{
	size_t operator() (::nano::public_key const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::block_hash>
{
	size_t operator() (::nano::block_hash const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::hash_or_account>
{
	size_t operator() (::nano::hash_or_account const & value) const noexcept
	{
		return hash<::nano::block_hash>{}(value.as_block_hash ());
	}
};
template <>
struct hash<::nano::root>
{
	size_t operator() (::nano::root const & value) const noexcept
	{
		return hash<::nano::hash_or_account>{}(value);
	}
};
template <>
struct hash<::nano::link>
{
	size_t operator() (::nano::link const & value) const noexcept
	{
		return hash<::nano::hash_or_account>{}(value);
	}
};
template <>
struct hash<::nano::raw_key>
{
	size_t operator() (::nano::raw_key const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::wallet_id>
{
	size_t operator() (::nano::wallet_id const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value);
	}
};
template <>
struct hash<::nano::uint512_union>
{
	size_t operator() (::nano::uint512_union const & value) const noexcept
	{
		return hash<::nano::uint256_union>{}(value.uint256s[0]) + hash<::nano::uint256_union> () (value.uint256s[1]);
	}
};
template <>
struct hash<::nano::qualified_root>
{
	size_t operator() (::nano::qualified_root const & value) const noexcept
	{
		return hash<::nano::uint512_union>{}(value);
	}
};
}

namespace boost
{
template <>
struct hash<::nano::uint128_union>
{
	size_t operator() (::nano::uint128_union const & value) const noexcept
	{
		return std::hash<::nano::uint128_union> () (value);
	}
};
template <>
struct hash<::nano::uint256_union>
{
	size_t operator() (::nano::uint256_union const & value) const noexcept
	{
		return std::hash<::nano::uint256_union> () (value);
	}
};
template <>
struct hash<::nano::public_key>
{
	size_t operator() (::nano::public_key const & value) const noexcept
	{
		return std::hash<::nano::public_key> () (value);
	}
};
template <>
struct hash<::nano::block_hash>
{
	size_t operator() (::nano::block_hash const & value) const noexcept
	{
		return std::hash<::nano::block_hash> () (value);
	}
};
template <>
struct hash<::nano::hash_or_account>
{
	size_t operator() (::nano::hash_or_account const & value) const noexcept
	{
		return std::hash<::nano::hash_or_account> () (value);
	}
};
template <>
struct hash<::nano::root>
{
	size_t operator() (::nano::root const & value) const noexcept
	{
		return std::hash<::nano::root> () (value);
	}
};
template <>
struct hash<::nano::link>
{
	size_t operator() (::nano::link const & value) const noexcept
	{
		return std::hash<::nano::link> () (value);
	}
};
template <>
struct hash<::nano::raw_key>
{
	size_t operator() (::nano::raw_key const & value) const noexcept
	{
		return std::hash<::nano::raw_key> () (value);
	}
};
template <>
struct hash<::nano::wallet_id>
{
	size_t operator() (::nano::wallet_id const & value) const noexcept
	{
		return std::hash<::nano::wallet_id> () (value);
	}
};
template <>
struct hash<::nano::uint512_union>
{
	size_t operator() (::nano::uint512_union const & value) const noexcept
	{
		return std::hash<::nano::uint512_union> () (value);
	}
};
template <>
struct hash<::nano::qualified_root>
{
	size_t operator() (::nano::qualified_root const & value) const noexcept
	{
		return std::hash<::nano::qualified_root> () (value);
	}
};
}
