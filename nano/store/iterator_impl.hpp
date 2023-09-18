#pragma once

#include <utility>

namespace nano::store
{
template <typename T, typename U>
class iterator_impl
{
public:
	virtual ~iterator_impl () = default;
	virtual iterator_impl<T, U> & operator++ () = 0;
	virtual iterator_impl<T, U> & operator-- () = 0;
	virtual bool operator== (iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	iterator_impl<T, U> & operator= (iterator_impl<T, U> const &) = delete;
	bool operator== (iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
} // namespace nano::store
