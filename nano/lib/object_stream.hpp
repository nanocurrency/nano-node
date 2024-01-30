#pragma once

#include <cstdint>
#include <iomanip>
#include <memory>
#include <ostream>
#include <ranges>
#include <string_view>
#include <type_traits>

#include <fmt/ostream.h>
#include <magic_enum_iostream.hpp>

namespace nano
{
struct object_stream_config
{
	std::string field_name_begin{ "" };
	std::string field_name_end{ "" };
	std::string field_assignment{ ": " };
	std::string field_separator{ "," };

	std::string object_begin{ "{" };
	std::string object_end{ "}" };

	std::string array_begin{ "[" };
	std::string array_end{ "]" };

	std::string array_element_begin{ "" };
	std::string array_element_end{ "" };
	std::string array_element_separator{ "," };

	std::string string_begin{ "\"" };
	std::string string_end{ "\"" };

	std::string true_value{ "true" };
	std::string false_value{ "false" };
	std::string null_value{ "null" };

	std::string indent{ "   " };
	std::string newline{ "\n" };

	/** Number of decimal places to show for `float` and `double` */
	int precision{ 2 };

	static object_stream_config const & default_config ();
	static object_stream_config const & json_config ();
};

class object_stream_context
{
public:
	object_stream_config const & config;

	explicit object_stream_context (std::ostream & os, object_stream_config const & config = object_stream_config::default_config ()) :
		os{ os },
		config{ config }
	{
	}

	// Bump indent level when nesting objects
	object_stream_context (object_stream_context const & other) :
		os{ other.os },
		config{ other.config },
		indent_level{ other.indent_level + 1 }
	{
	}

private:
	std::ostream & os;
	int indent_level{ 0 };
	bool needs_newline{ false };

public: // Keep these defined in the header for inlining
	std::ostream & begin_stream ()
	{
		return os;
	}

	void begin_field (std::string_view name, bool first)
	{
		if (!first)
		{
			os << config.field_separator;
		}
		if (std::exchange (needs_newline, false))
		{
			os << config.newline;
		}
		indent ();
		os << config.field_name_begin << name << config.field_name_end << config.field_assignment;
	}

	void end_field ()
	{
		needs_newline = true;
	}

	void begin_object ()
	{
		os << config.object_begin;
		os << config.newline;
	}

	void end_object ()
	{
		os << config.newline;
		indent ();
		os << config.object_end;
		needs_newline = true;
	}

	void begin_array ()
	{
		os << config.array_begin;
		os << config.newline;
	}

	void end_array ()
	{
		os << config.newline;
		indent ();
		os << config.array_end;
		needs_newline = true;
	}

	void begin_array_element (bool first)
	{
		if (!first)
		{
			os << config.array_element_separator;
		}
		if (std::exchange (needs_newline, false))
		{
			os << config.newline;
		}
		indent ();
		os << config.array_element_begin;
	}

	void end_array_element ()
	{
		os << config.array_element_end;
		needs_newline = true;
	}

	void begin_string ()
	{
		os << config.string_begin;
	}

	void end_string ()
	{
		os << config.string_end;
	}

private:
	void indent ()
	{
		if (!config.indent.empty ())
		{
			for (int i = 0; i < indent_level; ++i)
			{
				os << config.indent;
			}
		}
	}
};

class object_stream;
class array_stream;

/*
 * Concepts used for choosing the correct writing function
 */

template <typename T>
concept object_streamable = requires (T const & obj, object_stream & obs) {
	{
		stream_as (obj, obs)
	};
};

template <typename T>
concept array_streamable = requires (T const & obj, array_stream & ars) {
	{
		stream_as (obj, ars)
	};
};

template <typename T>
concept object_or_array_streamable = object_streamable<T> || array_streamable<T>;

class object_stream_base
{
public:
	explicit object_stream_base (object_stream_context const & ctx) :
		ctx{ ctx }
	{
	}

	explicit object_stream_base (std::ostream & os, object_stream_config const & config = object_stream_config::default_config ()) :
		ctx{ os, config }
	{
	}

protected:
	object_stream_context ctx;
};

/**
 * Used to serialize an object.
 * Outputs: `field1: value1, field2: value2, ...` (without enclosing `{}`)
 */
class object_stream : private object_stream_base
{
public:
	// Inherit default constructors
	using object_stream_base::object_stream_base;

	object_stream (object_stream const &) = delete; // Disallow copying

public:
	template <class Value>
	void write (std::string_view name, Value const & value)
	{
		ctx.begin_field (name, std::exchange (first_field, false));
		stream_as_value (value, ctx);
		ctx.end_field ();
	}

	// Handle `.write_range ("name", container)`
	template <class Container>
	inline void write_range (std::string_view name, Container const & container);

	// Handle `.write_range ("name", container, [] (auto const & entry) { ... })`
	template <class Container, class Transform>
		requires (std::is_invocable_v<Transform, typename Container::value_type>)
	void write_range (std::string_view name, Container const & container, Transform transform)
	{
		write_range (name, std::views::transform (container, transform));
	}

	// Handle `.write_range ("name", container, [] (auto const & entry, nano::object_stream &) { ... })`
	template <class Container, class Writer>
		requires (std::is_invocable_v<Writer, typename Container::value_type, object_stream &>)
	void write_range (std::string_view name, Container const & container, Writer writer)
	{
		write_range (name, container, [&writer] (auto const & el) {
			return [&writer, &el] (object_stream & obs) {
				writer (el, obs);
			};
		});
	}

	// Handle `.write_range ("name", container, [] (auto const & entry, nano::array_stream &) { ... })`
	template <class Container, class Writer>
		requires (std::is_invocable_v<Writer, typename Container::value_type, array_stream &>)
	void write_range (std::string_view name, Container const & container, Writer writer)
	{
		write_range (name, container, [&writer] (auto const & el) {
			return [&writer, &el] (array_stream & obs) {
				writer (el, obs);
			};
		});
	}

private:
	bool first_field{ true };
};

/**
 * Used to serialize an array of objects.
 * Outputs: `[value1, value2, ...]`
 */
class array_stream : private object_stream_base
{
public:
	// Inherit default constructors
	using object_stream_base::object_stream_base;

	array_stream (array_stream const &) = delete; // Disallow copying

private:
	template <class Value>
	void write_single (Value const & value)
	{
		ctx.begin_array_element (std::exchange (first_element, false));
		stream_as_value (value, ctx);
		ctx.end_array_element ();
	}

public:
	// Handle `.write (container)`
	template <class Container>
	void write (Container const & container)
	{
		for (auto const & el : container)
		{
			write_single (el);
		};
	}

	// Handle `.write (container, [] (auto const & entry) { ... })`
	template <class Container, class Transform>
		requires (std::is_invocable_v<Transform, typename Container::value_type>)
	void write (Container const & container, Transform transform)
	{
		write (std::views::transform (container, transform));
	}

	// Handle `.write (container, [] (auto const & entry, nano::object_stream &) { ... })`
	template <class Container, class Writer>
		requires (std::is_invocable_v<Writer, typename Container::value_type, object_stream &>)
	void write (Container const & container, Writer writer)
	{
		write (container, [&writer] (auto const & el) {
			return [&writer, &el] (object_stream & obs) {
				writer (el, obs);
			};
		});
	}

	// Handle `.write_range (container, [] (auto const & entry, nano::array_stream &) { ... })`
	template <class Container, class Writer>
		requires (std::is_invocable_v<Writer, typename Container::value_type, array_stream &>)
	void write (Container const & container, Writer writer)
	{
		write (container, [&writer] (auto const & el) {
			return [&writer, &el] (array_stream & obs) {
				writer (el, obs);
			};
		});
	}

private:
	bool first_element{ true };
};

/**
 * Used for human readable object serialization. Should be used to serialize a single object.
 * Outputs: `{ field1: value1, field2: value2, ... }`
 */
class root_object_stream : private object_stream_base
{
public:
	// Inherit default constructors
	using object_stream_base::object_stream_base;

public:
	template <class Value>
	void write (Value const & value)
	{
		stream_as_value (value, ctx);
	}

	// Handle `.write_range (container)`
	template <class Container>
	inline void write_range (Container const & container);

	// Handle `.write_range (container, [] (auto const & entry) { ... })`
	template <class Container, class Transform>
		requires (std::is_invocable_v<Transform, typename Container::value_type>)
	void write_range (Container const & container, Transform transform)
	{
		write_range (std::views::transform (container, transform));
	}

	// Handle `.write_range (container, [] (auto const & entry, nano::object_stream &) { ... })`
	template <class Container, class Writer>
		requires (std::is_invocable_v<Writer, typename Container::value_type, object_stream &>)
	void write_range (Container const & container, Writer writer)
	{
		write_range (container, [&writer] (auto const & el) {
			return [&writer, &el] (object_stream & obs) {
				writer (el, obs);
			};
		});
	}

	// Handle `.write_range (container, [] (auto const & entry, nano::array_stream &) { ... })`
	template <class Container, class Writer>
		requires (std::is_invocable_v<Writer, typename Container::value_type, array_stream &>)
	void write_range (Container const & container, Writer writer)
	{
		write_range (container, [&writer] (auto const & el) {
			return [&writer, &el] (array_stream & obs) {
				writer (el, obs);
			};
		});
	}
};

/*
 * Implementation for `write_range` functions
 */

template <class Container>
inline void nano::object_stream::write_range (std::string_view name, Container const & container)
{
	write (name, [&container] (array_stream & ars) {
		ars.write (container);
	});
}

template <class Container>
inline void nano::root_object_stream::write_range (Container const & container)
{
	write ([&container] (array_stream & ars) {
		ars.write (container);
	});
}

/*
 * Writers
 */

template <class Value>
inline void stream_as_value (Value const & value, object_stream_context & ctx)
{
	// Automatically support printing all enums
	using magic_enum::iostream_operators::operator<<;

	ctx.begin_string ();
	ctx.begin_stream () << value; // Write using type specific ostream operator
	ctx.end_string ();
}

template <object_streamable Value>
inline void stream_as_value (Value const & value, object_stream_context & ctx)
{
	ctx.begin_object ();

	// Write as object
	nano::object_stream obs{ ctx };
	stream_as (value, obs);

	ctx.end_object ();
}

template <array_streamable Value>
inline void stream_as_value (Value const & value, object_stream_context & ctx)
{
	ctx.begin_array ();

	// Write as array
	nano::array_stream ars{ ctx };
	stream_as (value, ars);

	ctx.end_array ();
}

/*
 * Adapters for types implementing convenience `obj(object_stream &)` & `obj(array_stream &)` functions
 */

template <typename T>
concept simple_object_streamable = requires (T const & obj, object_stream & obs) {
	{
		obj (obs)
	};
};

template <typename T>
concept simple_array_streamable = requires (T const & obj, array_stream & ars) {
	{
		obj (ars)
	};
};

template <simple_object_streamable Value>
inline void stream_as (Value const & value, object_stream & obs)
{
	value (obs);
}

template <simple_array_streamable Value>
inline void stream_as (Value const & value, array_stream & ars)
{
	value (ars);
}
}

/*
 * Specializations for primitive types
 */

namespace nano
{
template <class Value>
	requires (std::is_integral_v<Value> && sizeof (Value) > 1) // Exclude bool, char, etc.
inline void stream_as_value (const Value & value, object_stream_context & ctx)
{
	ctx.begin_stream () << value;
}

template <class Value>
	requires (std::is_floating_point_v<Value>)
inline void stream_as_value (const Value & value, object_stream_context & ctx)
{
	ctx.begin_stream () << std::fixed << std::setprecision (ctx.config.precision) << value;
}

inline void stream_as_value (bool const & value, object_stream_context & ctx)
{
	ctx.begin_stream () << (value ? ctx.config.true_value : ctx.config.false_value);
}

inline void stream_as_value (const int8_t & value, object_stream_context & ctx)
{
	ctx.begin_stream () << static_cast<int32_t> (value); // Avoid printing as char
}

inline void stream_as_value (const uint8_t & value, object_stream_context & ctx)
{
	ctx.begin_stream () << static_cast<uint32_t> (value); // Avoid printing as char
}

template <class Opt>
inline void stream_as_optional (const Opt & opt, object_stream_context & ctx)
{
	if (opt)
	{
		stream_as_value (*opt, ctx);
	}
	else
	{
		ctx.begin_stream () << ctx.config.null_value;
	}
}

template <class Value>
inline void stream_as_value (std::shared_ptr<Value> const & value, object_stream_context & ctx)
{
	stream_as_optional (value, ctx);
}

template <class Value>
inline void stream_as_value (std::unique_ptr<Value> const & value, object_stream_context & ctx)
{
	stream_as_optional (value, ctx);
}

template <class Value>
inline void stream_as_value (std::weak_ptr<Value> const & value, object_stream_context & ctx)
{
	stream_as_optional (value.lock (), ctx);
}

template <class Value>
inline void stream_as_value (std::optional<Value> const & value, object_stream_context & ctx)
{
	stream_as_optional (value, ctx);
}
}