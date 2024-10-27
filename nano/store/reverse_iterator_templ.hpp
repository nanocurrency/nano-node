#include <nano/store/reverse_iterator.hpp>

namespace nano::store
{
template <typename Iter>
reverse_iterator<Iter>::reverse_iterator (Iter && other) noexcept :
	internal{ std::move (other) }
{
}

template <typename Iter>
reverse_iterator<Iter>::reverse_iterator (reverse_iterator && other) noexcept :
	internal{ std::move (other.internal) }
{
}

template <typename Iter>
auto reverse_iterator<Iter>::operator= (reverse_iterator && other) noexcept -> reverse_iterator &
{
	internal = std::move (other.internal);
	return *this;
}

template <typename Iter>
auto reverse_iterator<Iter>::operator++ () -> reverse_iterator &
{
	--internal;
	return *this;
}

template <typename Iter>
auto reverse_iterator<Iter>::operator-- () -> reverse_iterator &
{
	++internal;
	return *this;
}

template <typename Iter>
auto reverse_iterator<Iter>::operator->() const -> const_pointer
{
	release_assert (!is_end ());
	return internal.operator->();
}

template <typename Iter>
auto reverse_iterator<Iter>::operator* () const -> const_reference
{
	release_assert (!is_end ());
	return internal.operator* ();
}

template <typename Iter>
auto reverse_iterator<Iter>::operator== (reverse_iterator const & other) const -> bool
{
	return internal == other.internal;
}

template <typename Iter>
bool reverse_iterator<Iter>::is_end () const
{
	return internal.is_end ();
}
}
