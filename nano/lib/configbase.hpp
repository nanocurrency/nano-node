#pragma once

#include <nano/lib/errors.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/type_traits.hpp>

#include <istream>
#include <limits>
#include <string>
#include <type_traits>

namespace boost::asio::ip
{
class address_v6;
}

namespace nano
{
/** Type trait to determine if T is compatible with boost's lexical_cast */
template <class T>
struct is_lexical_castable : std::integral_constant<bool,
							 (std::is_default_constructible<T>::value && (boost::has_right_shift<std::basic_istream<wchar_t>, T>::value || boost::has_right_shift<std::basic_istream<char>, T>::value))>
{
};

/** Describes the value type T expects, for configuration error messages */
template <typename T>
std::string type_desc ()
{
	if constexpr (std::is_same_v<T, bool>)
	{
		return "a boolean";
	}
	else if constexpr (std::is_same_v<T, char>)
	{
		return "a character";
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		return "a string";
	}
	else if constexpr (std::is_same_v<T, boost::asio::ip::address_v6>)
	{
		return "an IPv6 address such as ::1 or ::ffff:127.0.0.1";
	}
	else if constexpr (std::is_integral_v<T>)
	{
		if constexpr (sizeof (T) <= 2)
		{
			return "an integer between " + std::to_string (std::numeric_limits<T>::min ()) + " and " + std::to_string (std::numeric_limits<T>::max ());
		}
		else
		{
			return std::string{ "a " } + std::to_string (sizeof (T) * 8) + "-bit " + (std::is_signed_v<T> ? "signed" : "unsigned") + " integer";
		}
	}
	else if constexpr (std::is_same_v<T, float>)
	{
		return "a single precision floating point number";
	}
	else if constexpr (std::is_same_v<T, double>)
	{
		return "a double precision floating point number";
	}
	else
	{
		return "an unknown type";
	}
}

/** Base type for configuration wrappers */
class configbase : public nano::error_aware<>
{
public:
	configbase () = default;
	configbase (std::shared_ptr<nano::error> const & error_a) :
		error (error_a)
	{
	}

	/** Returns the current error */
	nano::error & get_error () override
	{
		return *error;
	}

	/** Turn on or off automatic error message generation */
	void set_auto_error_message (bool auto_a)
	{
		auto_error_message = auto_a;
	}

protected:
	template <typename T>
	void construct_error_message (bool optional, std::string const & key)
	{
		if (auto_error_message && *error)
		{
			if (optional)
			{
				error->set_message (key + " is not " + type_desc<T> ());
			}
			else
			{
				error->set_message (key + " is required and must be " + type_desc<T> ());
			}
		}
	}

	/** Set error if not already set. That is, first error remains until get_error().clear() is called. */
	template <typename T, typename V>
	void conditionally_set_error (V error_a, bool optional, std::string const & key)
	{
		if (!*error)
		{
			*error = error_a;
			construct_error_message<T> (optional, key);
		}
	}

	/** We're a nano::error_aware type. Child nodes share the error state. */
	std::shared_ptr<nano::error> error;

	/** If set, automatically construct error messages based on parameters and type information. */
	bool auto_error_message{ true };
};
}
