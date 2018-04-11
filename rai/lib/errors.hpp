#pragma once

#include <rai/lib/expected.hpp>

using nonstd::expected;
using nonstd::make_unexpected;

// Convenience macro to implement the standard boilerplate for using std::error_code with enums
#define ENABLE_ERRORS(enum_type)                              \
	namespace rai                                             \
	{                                                         \
		std::error_code make_error_code (enum_type);          \
	}                                                         \
	namespace std                                             \
	{                                                         \
		template <>                                           \
		struct is_error_code_enum<enum_type> : std::true_type \
		{                                                     \
		};                                                    \
	}
