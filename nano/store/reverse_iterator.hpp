#pragma once

namespace nano::store
{
/**
 * @class reverse_iterator
 * @brief A reverse iterator adaptor for bidirectional iterators.
 *
 * This class template adapts any bidirectional iterator to reverse its direction of iteration.
 * It inverts the semantics of increment and decrement operations.
 *
 * Key characteristics:
 * - Incrementing (operator++) moves to the previous element in the sequence.
 * - Decrementing (operator--) moves to the next element in the sequence.
 * - Dereferencing refers to the same element as the adapted iterator.
 * - Compatible with any bidirectional iterator, not limited to specific container types.
 */
template <typename Iter>
class reverse_iterator
{
public:
	using iterator_category = std::bidirectional_iterator_tag;
	using value_type = Iter::value_type;
	using pointer = value_type *;
	using const_pointer = value_type const *;
	using reference = value_type &;
	using const_reference = value_type const &;

private:
	Iter internal;

public:
	reverse_iterator (Iter && other) noexcept;

	reverse_iterator (reverse_iterator const &) = delete;
	auto operator= (reverse_iterator const &) -> reverse_iterator & = delete;

	reverse_iterator (reverse_iterator && other) noexcept;
	auto operator= (reverse_iterator && other) noexcept -> reverse_iterator &;

	auto operator++ () -> reverse_iterator &;
	auto operator-- () -> reverse_iterator &;
	auto operator->() const -> const_pointer;
	auto operator* () const -> const_reference;
	auto operator== (reverse_iterator const & other) const -> bool;
	auto operator!= (reverse_iterator const & other) const -> bool
	{
		return !(*this == other);
	}
	bool is_end () const;
};
}
