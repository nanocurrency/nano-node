#pragma once

#include <nano/lib/assert.hpp>

#include <gtest/gtest.h>

namespace nano::test
{
/** Anything a test starts and stops, such as a node component */
template <class T>
concept startable = requires (T & component) {
	component.start ();
	component.stop ();
};

/** A pointer-like object, such as a raw pointer, std::shared_ptr or std::unique_ptr, to something startable */
template <class P>
concept startable_pointer = !startable<P> && requires (P & pointer) {
	*pointer;
	requires startable<decltype (*pointer)>;
};

/**
 * Starts a component on construction and stops it on destruction, so it runs until the end of the scope even when an ASSERT_* returns early
 * Declared through START; a test may still stop the component earlier, so its stop () must tolerate a second call
 */
template <nano::test::startable T>
class scoped_start final
{
public:
	explicit scoped_start (T & component_a);
	~scoped_start ();

	scoped_start (scoped_start const &) = delete;
	scoped_start & operator= (scoped_start const &) = delete;

private:
	T & component;
};

/** Starts the component and returns the guard that stops it, which must not outlive the component */
template <nano::test::startable T>
[[nodiscard]] nano::test::scoped_start<T> start (T & component);

/** Starts the component the pointer points to; the pointer must not be null */
template <nano::test::startable_pointer P>
[[nodiscard]] auto start (P & pointer);
}

/** Starts the component, or the component a pointer points to, until the end of the enclosing scope */
#define START(component) auto const GTEST_CONCAT_TOKEN_ (scoped_start_, __LINE__) = nano::test::start (component)

template <nano::test::startable T>
nano::test::scoped_start<T>::scoped_start (T & component_a) :
	component{ component_a }
{
	component.start ();
}

template <nano::test::startable T>
nano::test::scoped_start<T>::~scoped_start ()
{
	component.stop ();
}

template <nano::test::startable T>
nano::test::scoped_start<T> nano::test::start (T & component)
{
	return nano::test::scoped_start<T>{ component };
}

template <nano::test::startable_pointer P>
auto nano::test::start (P & pointer)
{
	release_assert (pointer != nullptr);
	return nano::test::start (*pointer);
}
