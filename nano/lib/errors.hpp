#pragma once

#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>

namespace nano
{
/** Common error codes */
enum class error_common
{
	generic = 1,
	exception,
	account_not_found,
	account_not_found_wallet,
	account_exists,
	bad_account_number,
	bad_balance,
	bad_link,
	bad_previous,
	bad_representative_number,
	bad_source,
	bad_signature,
	bad_private_key,
	bad_public_key,
	bad_seed,
	bad_threshold,
	bad_wallet_number,
	bad_work_format,
	disabled_local_work_generation,
	disabled_work_generation,
	failure_work_generation,
	missing_account,
	missing_balance,
	missing_link,
	missing_previous,
	missing_representative,
	missing_signature,
	missing_work,
	invalid_amount,
	invalid_amount_big,
	invalid_count,
	invalid_index,
	invalid_ip_address,
	invalid_port,
	invalid_type_conversion,
	invalid_work,
	insufficient_balance,
	numeric_conversion,
	tracking_not_enabled,
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
	bad_difficulty_format,
	bad_key,
	bad_link,
	bad_multiplier_format,
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
	confirmation_height_not_processing,
	confirmation_not_found,
	difficulty_limit,
	disabled_bootstrap_lazy,
	disabled_bootstrap_legacy,
	invalid_balance,
	invalid_destinations,
	invalid_offset,
	invalid_missing_type,
	invalid_root,
	invalid_sources,
	invalid_subtype,
	invalid_subtype_balance,
	invalid_subtype_epoch_link,
	invalid_subtype_previous,
	invalid_timestamp,
	payment_account_balance,
	payment_unable_create_account,
	rpc_control_disabled,
	sign_hash_disabled,
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

/** config.json deserialization related errors */
enum class error_config
{
	generic = 1,
	invalid_value,
	missing_value,
};
} // nano namespace

// Convenience macro to implement the standard boilerplate for using std::error_code with enums
// Use this at the end of any header defining one or more error code enums.
#define REGISTER_ERROR_CODES(namespace_name, enum_type)                                                        \
	namespace namespace_name                                                                                   \
	{                                                                                                          \
		static_assert (static_cast<int> (enum_type::generic) > 0, "The first error enum must be generic = 1"); \
		class enum_type##_messages : public std::error_category                                                \
		{                                                                                                      \
		public:                                                                                                \
			const char * name () const noexcept override                                                       \
			{                                                                                                  \
				return #enum_type;                                                                             \
			}                                                                                                  \
                                                                                                               \
			std::string message (int ev) const override;                                                       \
		};                                                                                                     \
                                                                                                               \
		inline const std::error_category & enum_type##_category ()                                             \
		{                                                                                                      \
			static enum_type##_messages instance;                                                              \
			return instance;                                                                                   \
		}                                                                                                      \
                                                                                                               \
		inline std::error_code make_error_code (::namespace_name::enum_type err)                               \
		{                                                                                                      \
			return { static_cast<int> (err), enum_type##_category () };                                        \
		}                                                                                                      \
	}                                                                                                          \
	namespace std                                                                                              \
	{                                                                                                          \
		template <>                                                                                            \
		struct is_error_code_enum<::namespace_name::enum_type> : std::true_type                                \
		{                                                                                                      \
		};                                                                                                     \
	}

REGISTER_ERROR_CODES (nano, error_common);
REGISTER_ERROR_CODES (nano, error_blocks);
REGISTER_ERROR_CODES (nano, error_rpc);
REGISTER_ERROR_CODES (nano, error_process);
REGISTER_ERROR_CODES (nano, error_config);

/* boost->std error_code bridge */
namespace nano
{
namespace error_conversion
{
	const std::error_category & generic_category ();
}
}

namespace std
{
template <>
struct is_error_code_enum<boost::system::errc::errc_t>
: public std::true_type
{
};

inline std::error_code make_error_code (boost::system::errc::errc_t e)
{
	return std::error_code (static_cast<int> (e),
	::nano::error_conversion::generic_category ());
}
}
namespace nano
{
namespace error_conversion
{
	namespace detail
	{
		class generic_category : public std::error_category
		{
		public:
			virtual const char * name () const noexcept override
			{
				return boost::system::generic_category ().name ();
			}
			virtual std::string message (int value) const override
			{
				return boost::system::generic_category ().message (value);
			}
		};
	}
	inline const std::error_category & generic_category ()
	{
		static detail::generic_category instance;
		return instance;
	}

	inline std::error_code convert (const boost::system::error_code & error)
	{
		if (error.category () == boost::system::generic_category ())
		{
			return std::error_code (error.value (),
			nano::error_conversion::generic_category ());
		}
		assert (false);

		return nano::error_common::invalid_type_conversion;
	}
}
}

namespace nano
{
/** Adapter for std/boost::error_code, std::exception and bool flags to facilitate unified error handling */
class error
{
public:
	error () = default;
	error (nano::error const & error_a) = default;
	error (nano::error && error_a) = default;

	error (std::error_code code_a)
	{
		code = code_a;
	}

	error (boost::system::error_code code_a)
	{
		code = std::make_error_code (static_cast<std::errc> (code_a.value ()));
	}

	error (std::string message_a)
	{
		code = nano::error_common::generic;
		message = std::move (message_a);
	}

	error (std::exception const & exception_a)
	{
		code = nano::error_common::exception;
		message = exception_a.what ();
	}

	error & operator= (nano::error const & err_a)
	{
		code = err_a.code;
		message = err_a.message;
		return *this;
	}

	error & operator= (nano::error && err_a)
	{
		code = err_a.code;
		message = std::move (err_a.message);
		return *this;
	}

	/** Assign error code */
	error & operator= (const std::error_code code_a)
	{
		code = code_a;
		message.clear ();
		return *this;
	}

	/** Assign boost error code (as converted to std::error_code) */
	error & operator= (const boost::system::error_code & code_a)
	{
		code = nano::error_conversion::convert (code_a);
		message.clear ();
		return *this;
	}

	/** Assign boost error code (as converted to std::error_code) */
	error & operator= (const boost::system::errc::errc_t & code_a)
	{
		code = nano::error_conversion::convert (boost::system::errc::make_error_code (code_a));
		message.clear ();
		return *this;
	}

	/** Set the error to nano::error_common::generic and the error message to \p message_a */
	error & operator= (const std::string message_a)
	{
		code = nano::error_common::generic;
		message = std::move (message_a);
		return *this;
	}

	/** Sets the error to nano::error_common::exception and adopts the exception error message. */
	error & operator= (std::exception const & exception_a)
	{
		code = nano::error_common::exception;
		message = exception_a.what ();
		return *this;
	}

	/** Return true if this#error_code equals the parameter */
	bool operator== (const std::error_code code_a)
	{
		return code == code_a;
	}

	/** Return true if this#error_code equals the parameter */
	bool operator== (const boost::system::error_code code_a)
	{
		return code.value () == code_a.value ();
	}

	/** Call the function iff the current error is zero */
	error & then (std::function<nano::error &()> next)
	{
		return code ? *this : next ();
	}

	/** If the current error is one of the listed codes, reset the error code */
	template <typename... ErrorCode>
	error & accept (ErrorCode... err)
	{
		// Convert variadic arguments to std::error_code
		auto codes = { std::error_code (err)... };
		if (std::any_of (codes.begin (), codes.end (), [this, &codes](auto & code_a) { return code == code_a; }))
		{
			code.clear ();
		}

		return *this;
	}

	/** Implicit error_code conversion */
	explicit operator std::error_code () const
	{
		return code;
	}

	/** Implicit bool conversion; true if there's an error */
	explicit operator bool () const
	{
		return code.value () != 0;
	}

	/** Implicit string conversion; returns the error message or an empty string. */
	explicit operator std::string () const
	{
		return get_message ();
	}

	/**
	 * Get error message, or an empty string if there's no error. If a custom error message is set,
	 * that will be returned, otherwise the error_code#message() is returned.
	 */
	std::string get_message () const
	{
		std::string res = message;
		if (code && res.empty ())
		{
			res = code.message ();
		}
		return res;
	}

	/** Set an error message, but only if the error code is already set */
	error & on_error (std::string message_a)
	{
		if (code)
		{
			message = std::move (message_a);
		}
		return *this;
	}

	/** Set an error message if the current error code matches \p code_a */
	error & on_error (std::error_code code_a, std::string message_a)
	{
		if (code == code_a)
		{
			message = std::move (message_a);
		}
		return *this;
	}

	/** Set an error message and an error code */
	error & set (std::string message_a, std::error_code code_a = nano::error_common::generic)
	{
		message = message_a;
		code = code_a;
		return *this;
	}

	/** Set a custom error message. If the error code is not set, it will be set to nano::error_common::generic. */
	error & set_message (std::string message_a)
	{
		if (!code)
		{
			code = nano::error_common::generic;
		}
		message = std::move (message_a);
		return *this;
	}

	/** Clear an errors */
	error & clear ()
	{
		code.clear ();
		message.clear ();
		return *this;
	}

private:
	std::error_code code;
	std::string message;
};

/**
 * A type that manages a nano::error.
 * The default return type is nano::error&, though shared_ptr<nano::error> is a good option in cases
 * where shared error state is desirable.
 */
template <typename RET_TYPE = nano::error &>
class error_aware
{
	static_assert (std::is_same<RET_TYPE, nano::error &>::value || std::is_same<RET_TYPE, std::shared_ptr<nano::error>>::value, "Must be nano::error& or shared_ptr<nano::error>");

public:
	/** Returns the error object managed by this object */
	virtual RET_TYPE get_error () = 0;
};
}
