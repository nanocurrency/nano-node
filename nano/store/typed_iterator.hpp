#pragma once

#include <nano/store/iterator.hpp>

#include <cstddef>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

namespace nano::store
{
/**
 * @class typed_iterator
 * @brief A generic typed iterator for key-value stores.
 *
 * This class represents a typed iterator for key-value store databases, such as RocksDB.
 * It supports typing for both keys and values, providing type-safe access to the database contents.
 *
 * Key characteristics:
 * - Generic: Works with various key-value store implementations.
 * - Type-safe: Supports strongly typed keys and values.
 * - Circular: The end() sentinel value is always in the iteration cycle.
 * - Automatic deserialization: When pointing to a valid non-sentinel location, it loads and
 *   deserializes the database value into the appropriate type.
 *
 * Behavior:
 * - Decrementing the end iterator points to the last key-value pair in the database.
 * - Incrementing the end iterator points to the first key-value pair in the database.
 */
template <typename Key, typename Value>
class typed_iterator final
{
public:
	using iterator_category = std::bidirectional_iterator_tag;
	using value_type = std::pair<Key, Value>;
	using pointer = value_type *;
	using const_pointer = value_type const *;
	using reference = value_type &;
	using const_reference = value_type const &;

private:
	iterator iter;
	std::variant<std::monostate, value_type> current;
	void update ();

public:
	typed_iterator (iterator && iter) noexcept;

	typed_iterator (typed_iterator const &) = delete;
	auto operator= (typed_iterator const &) -> typed_iterator & = delete;

	typed_iterator (typed_iterator && other) noexcept;
	auto operator= (typed_iterator && other) noexcept -> typed_iterator &;

	auto operator++ () -> typed_iterator &;
	auto operator-- () -> typed_iterator &;
	auto operator->() const -> const_pointer;
	auto operator* () const -> const_reference;
	auto operator== (typed_iterator const & other) const -> bool;
	auto operator!= (typed_iterator const & other) const -> bool
	{
		return !(*this == other);
	}
	auto is_end () const -> bool;
};
} // namespace nano::store
