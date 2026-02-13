#include <nano/lib/utility.hpp>
#include <nano/store/iterator.hpp>
#include <nano/store/transaction.hpp>

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

iterator::iterator (transaction const & txn, backend_iterator && internals) noexcept :
	txn{ &txn },
	transaction_epoch{ txn.epoch () },
	internals{ std::move (internals) }
{
	update ();
}

iterator::~iterator ()
{
	if (txn)
	{
		release_assert (transaction_epoch == txn->epoch (), "invalid iterator-transaction lifetime detected");
	}
}

iterator::iterator (iterator && other) noexcept :
	txn{ other.txn },
	transaction_epoch{ other.transaction_epoch },
	internals{ std::move (other.internals) }
{
	other.txn = nullptr;
	current = std::move (other.current);
}

auto iterator::operator= (iterator && other) noexcept -> iterator &
{
	if (txn)
	{
		release_assert (transaction_epoch == txn->epoch (), "invalid iterator-transaction lifetime detected");
	}
	txn = other.txn;
	transaction_epoch = other.transaction_epoch;
	internals = std::move (other.internals);
	current = std::move (other.current);
	other.txn = nullptr;
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
