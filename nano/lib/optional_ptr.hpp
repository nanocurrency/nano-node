#pragma once

#include <nano/lib/utility.hpp>

#include <cstddef>
#include <memory>

namespace nano
{
/**
 * A space efficient optional which does heap allocation when needed.
 * This is an alternative to boost/std::optional when the value type is
 * large and often not present.
 *
 * optional_ptr is similar to using std::unique_ptr as an optional, with the
 * main difference being that it's copyable.
 */
template <typename T>
class optional_ptr
{
	static_assert (sizeof (T) > alignof (std::max_align_t), "Use [std|boost]::optional");

public:
	optional_ptr () = default;

	optional_ptr (T const & value) :
		ptr (new T{ value })
	{
	}

	optional_ptr (optional_ptr const & other)
	{
		if (other && other.ptr)
		{
			ptr = std::make_unique<T> (*other.ptr);
		}
	}

	optional_ptr & operator= (optional_ptr const & other)
	{
		if (other && other.ptr)
		{
			ptr = std::make_unique<T> (*other.ptr);
		}
		return *this;
	}

	T & operator* ()
	{
		return *ptr;
	}

	T const & operator* () const
	{
		return *ptr;
	}

	T * const operator->()
	{
		return ptr.operator->();
	}

	T const * const operator->() const
	{
		return ptr.operator->();
	}

	T const * const get () const
	{
		debug_assert (is_initialized ());
		return ptr.get ();
	}

	T * const get ()
	{
		debug_assert (is_initialized ());
		return ptr.get ();
	}

	explicit operator bool () const
	{
		return static_cast<bool> (ptr);
	}

	bool is_initialized () const
	{
		return static_cast<bool> (ptr);
	}

private:
	std::unique_ptr<T> ptr{ nullptr };
};
}
