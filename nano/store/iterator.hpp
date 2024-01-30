#pragma once

#include <nano/store/iterator_impl.hpp>

#include <memory>

namespace nano::store
{
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class iterator final
{
public:
	iterator (std::nullptr_t)
	{
	}
	iterator (std::unique_ptr<iterator_impl<T, U>> impl_a) :
		impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	iterator (iterator<T, U> && other_a) :
		current (std::move (other_a.current)),
		impl (std::move (other_a.impl))
	{
	}
	iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	iterator<T, U> & operator-- ()
	{
		--*impl;
		impl->fill (current);
		return *this;
	}
	iterator<T, U> & operator= (iterator<T, U> && other_a) noexcept
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	iterator<T, U> & operator= (iterator<T, U> const &) = delete;
	std::pair<T, U> * operator->()
	{
		return &current;
	}
	bool operator== (iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<iterator_impl<T, U>> impl;
};
} // namespace nano::store
