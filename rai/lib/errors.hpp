#pragma once

#include <rai/lib/expected.hpp>
#include <string>
#include <system_error>

using tl::expected;
using tl::make_unexpected;

namespace nano
{
/** Returns the error code if non-zero, otherwise the value */
template <class T>
auto either (T && value, std::error_code ec) -> expected<std::remove_reference_t<T>, std::error_code>
{
	if (ec)
	{
		return make_unexpected (ec);
	}
	else
	{
		return std::move (value);
	}
}

/** Common error codes */
enum class error_common
{
	generic = 1,
	account_not_found,
	account_exists,
	bad_account_number,
	bad_public_key,
	bad_seed,
	bad_wallet_number,
	bad_work_format,
	invalid_work,
	invalid_index,
	numeric_conversion,
	wallet_locked,
	wallet_not_found
};

/** Block related errors */
enum class error_blocks
{
	generic = 1,
	bad_hash_number,
	invalid_block_hash,
	not_found
};
}

// Convenience macro to implement the standard boilerplate for using std::error_code with enums
// Use this at the end of any header defining one or more error code enums.
#define REGISTER_ERROR_CODES(namespace_name, enum_type)                                                                      \
	namespace namespace_name                                                                                                 \
	{                                                                                                                        \
		static_assert (static_cast<int> (enum_type::generic) > 0, "The first error enum must be generic = 1");               \
		class enum_type##_messages : public std::error_category                                                              \
		{                                                                                                                    \
		public:                                                                                                              \
			const char * name () const noexcept override                                                                     \
			{                                                                                                                \
				return #enum_type;                                                                                           \
			}                                                                                                                \
                                                                                                                             \
			std::string message (int ev) const override;                                                                     \
		};                                                                                                                   \
                                                                                                                             \
		inline const std::error_category & enum_type##_category ()                                                           \
		{                                                                                                                    \
			static enum_type##_messages instance;                                                                            \
			return instance;                                                                                                 \
		}                                                                                                                    \
                                                                                                                             \
		inline std::error_code make_error_code (::namespace_name::enum_type err)                                             \
		{                                                                                                                    \
			return { static_cast<int> (err), enum_type##_category () };                                                      \
		}                                                                                                                    \
                                                                                                                             \
		inline auto unexpected_error (::namespace_name::enum_type err) -> decltype (make_unexpected (make_error_code (err))) \
		{                                                                                                                    \
			return make_unexpected (make_error_code (err));                                                                  \
		}                                                                                                                    \
	}                                                                                                                        \
	namespace std                                                                                                            \
	{                                                                                                                        \
		template <>                                                                                                          \
		struct is_error_code_enum<::namespace_name::enum_type> : std::true_type                                              \
		{                                                                                                                    \
		};                                                                                                                   \
	}

REGISTER_ERROR_CODES (nano, error_common);
REGISTER_ERROR_CODES (nano, error_blocks);
