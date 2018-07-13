#pragma once

#include <rai/lib/expected.hpp>
#include <string>
#include <system_error>
#include <type_traits>

using tl::expected;
using tl::make_unexpected;

namespace nano
{
/** Returns the error code if non-zero, otherwise the value */
template <class T>
auto either (T && value, std::error_code ec) -> expected<typename std::remove_reference<T>::type, std::error_code>
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
	account_not_found_wallet,
	account_exists,
	bad_account_number,
	bad_private_key,
	bad_public_key,
	bad_seed,
	bad_threshold,
	bad_wallet_number,
	bad_work_format,
	invalid_amount,
	invalid_amount_big,
	invalid_count,
	invalid_index,
	invalid_ip_address,
	invalid_port,
	invalid_work,
	insufficient_balance,
	numeric_conversion,
	wallet_lmdb_max_dbs,
	wallet_locked,
	wallet_not_found
};

/** Block related errors */
enum class error_blocks
{
	generic = 1,
	bad_hash_number,
	invalid_block,
	invalid_block_hash,
	invalid_type,
	not_found,
	work_low
};

/** RPC related errors */
enum class error_rpc
{
	generic = 1,
	bad_destination,
	bad_key,
	bad_link,
	bad_previous,
	bad_representative_number,
	bad_source,
	bad_timeout,
	block_create_balance_mismatch,
	block_create_key_required,
	block_create_public_key_mismatch,
	block_create_requirements_state,
	block_create_requirements_open,
	block_create_requirements_receive,
	block_create_requirements_change,
	block_create_requirements_send,
	invalid_balance,
	invalid_destinations,
	invalid_offset,
	invalid_missing_type,
	invalid_sources,
	payment_account_balance,
	payment_unable_create_account,
	rpc_control_disabled,
	source_not_found
};

/** process_result related errors */
enum class error_process
{
	generic = 1,
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist or has already been received
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	opened_burn_account, // The impossible happened, someone found the private key associated with the public key '0'.
	balance_mismatch, // Balance and amount delta don't match
	block_position, // This block cannot follow the previous block
	other
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
REGISTER_ERROR_CODES (nano, error_rpc);
REGISTER_ERROR_CODES (nano, error_process);
