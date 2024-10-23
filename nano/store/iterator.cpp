#include <nano/lib/utility.hpp>
#include <nano/store/iterator.hpp>

namespace nano::store
{
void iterator::update ()
{
	std::visit ([&] (auto && arg) {
		using T = std::remove_cvref_t<decltype (arg)>;
		if constexpr (std::is_same_v<T, lmdb::iterator>)
		{
			if (!arg.is_end ())
			{
				auto & current = *arg;
				std::span<uint8_t const> key{ reinterpret_cast<uint8_t const *> (current.first.mv_data), current.first.mv_size };
				std::span<uint8_t const> value{ reinterpret_cast<uint8_t const *> (current.second.mv_data), current.second.mv_size };
				this->current = std::make_pair (key, value);
			}
			else
			{
				current = std::monostate{};
			}
		}
		else if constexpr (std::is_same_v<T, rocksdb::iterator>)
			if (!arg.is_end ())
			{
				auto & current = *arg;
				std::span<uint8_t const> key{ reinterpret_cast<uint8_t const *> (current.first.data ()), current.first.size () };
				std::span<uint8_t const> value{ reinterpret_cast<uint8_t const *> (current.second.data ()), current.second.size () };
				this->current = std::make_pair (key, value);
			}
			else
			{
				current = std::monostate{};
			}
		else
		{
			static_assert (sizeof (T) == 0, "Missing variant handler for type T");
		}
	},
	internals);
}

iterator::iterator (std::variant<lmdb::iterator, rocksdb::iterator> && internals) noexcept :
	internals{ std::move (internals) }
{
	update ();
}

iterator::iterator (iterator && other) noexcept :
	internals{ std::move (other.internals) }
{
	current = std::move (other.current);
}

auto iterator::operator= (iterator && other) noexcept -> iterator &
{
	internals = std::move (other.internals);
	current = std::move (other.current);
	return *this;
}

auto iterator::operator++ () -> iterator &
{
	std::visit ([] (auto && arg) {
		++arg;
	},
	internals);
	update ();
	return *this;
}

auto iterator::operator-- () -> iterator &
{
	std::visit ([] (auto && arg) {
		--arg;
	},
	internals);
	update ();
	return *this;
}

auto iterator::operator->() const -> const_pointer
{
	release_assert (!is_end ());
	return std::get_if<value_type> (&current);
}

auto iterator::operator* () const -> const_reference
{
	release_assert (!is_end ());
	return std::get<value_type> (current);
}

auto iterator::operator== (iterator const & other) const -> bool
{
	return internals == other.internals;
}

bool iterator::is_end () const
{
	return std::holds_alternative<std::monostate> (current);
}
}
