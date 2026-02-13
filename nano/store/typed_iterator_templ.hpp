#pragma once

#include <nano/lib/utility.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/typed_iterator.hpp>

namespace nano::store
{
template <typename Key, typename Value>
void typed_iterator<Key, Value>::update ()
{
	if (!iter.is_end ())
	{
		auto const & data = *iter;
		nano::store::db_val key_val{ data.first };
		nano::store::db_val value_val{ data.second };
		current = std::make_pair (static_cast<Key> (key_val), static_cast<Value> (value_val));
	}
	else
	{
		current = std::monostate{};
	}
}

template <typename Key, typename Value>
typed_iterator<Key, Value>::typed_iterator (iterator && iter) noexcept :
	iter{ std::move (iter) }
{
	update ();
}

template <typename Key, typename Value>
typed_iterator<Key, Value>::typed_iterator (typed_iterator && other) noexcept :
	iter{ std::move (other.iter) },
	current{ std::move (other.current) }
{
}

template <typename Key, typename Value>
auto typed_iterator<Key, Value>::operator= (typed_iterator && other) noexcept -> typed_iterator &
{
	iter = std::move (other.iter);
	current = std::move (other.current);
	return *this;
}

template <typename Key, typename Value>
auto typed_iterator<Key, Value>::operator++ () -> typed_iterator<Key, Value> &
{
	++iter;
	update ();
	return *this;
}

template <typename Key, typename Value>
auto typed_iterator<Key, Value>::operator-- () -> typed_iterator<Key, Value> &
{
	--iter;
	update ();
	return *this;
}

template <typename Key, typename Value>
auto typed_iterator<Key, Value>::operator->() const -> const_pointer
{
	release_assert (!is_end ());
	return std::get_if<value_type> (&current);
}

template <typename Key, typename Value>
auto typed_iterator<Key, Value>::operator* () const -> const_reference
{
	release_assert (!is_end ());
	return std::get<value_type> (current);
}

template <typename Key, typename Value>
auto typed_iterator<Key, Value>::operator== (typed_iterator<Key, Value> const & other) const -> bool
{
	return iter == other.iter;
}

template <typename Key, typename Value>
auto typed_iterator<Key, Value>::is_end () const -> bool
{
	return std::holds_alternative<std::monostate> (current);
}

}
