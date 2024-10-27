#include <nano/lib/utility.hpp>
#include <nano/store/iterator.hpp>

namespace nano::store
{
void iterator::update ()
{
	std::visit ([&] (auto && arg) {
		if (!arg.is_end ())
		{
			this->current = arg.span ();
		}
		else
		{
			current = std::monostate{};
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
