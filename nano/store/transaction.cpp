#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/transaction.hpp>

/*
 * transaction_impl
 */

nano::store::transaction_impl::transaction_impl ()
{
}

/*
 * read_transaction_impl
 */

nano::store::read_transaction_impl::read_transaction_impl ()
{
}

/*
 * write_transaction_impl
 */

nano::store::write_transaction_impl::write_transaction_impl ()
{
}

/*
 * transaction
 */

nano::store::transaction::transaction (nano::id_dispenser::id_t store_id) :
	store_id_m{ store_id }
{
}

nano::id_dispenser::id_t nano::store::transaction::store_id () const
{
	return store_id_m;
}

auto nano::store::transaction::epoch () const -> epoch_t
{
	return current_epoch;
}

std::chrono::steady_clock::time_point nano::store::transaction::timestamp () const
{
	return start;
}

/*
 * read_transaction
 */

nano::store::read_transaction::read_transaction (std::unique_ptr<store::read_transaction_impl> read_transaction_impl, nano::id_dispenser::id_t store_id) :
	transaction{ store_id },
	impl (std::move (read_transaction_impl))
{
	start = std::chrono::steady_clock::now ();
}

void * nano::store::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

void nano::store::read_transaction::reset ()
{
	++current_epoch;
	impl->reset ();
}

void nano::store::read_transaction::renew ()
{
	++current_epoch;
	impl->renew ();
	start = std::chrono::steady_clock::now ();
}

void nano::store::read_transaction::refresh ()
{
	reset ();
	renew ();
}

void nano::store::read_transaction::refresh_if_needed (std::chrono::milliseconds max_age)
{
	auto now = std::chrono::steady_clock::now ();
	if (now - start > max_age)
	{
		refresh ();
	}
}

/*
 * write_transaction
 */

nano::store::write_transaction::write_transaction (std::unique_ptr<store::write_transaction_impl> write_transaction_impl, nano::id_dispenser::id_t store_id) :
	transaction{ store_id },
	impl (std::move (write_transaction_impl))
{
	/*
	 * For IO threads, we do not want them to block on creating write transactions.
	 */
	debug_assert (nano::thread_role::get () != nano::thread_role::name::io);

	start = std::chrono::steady_clock::now ();
}

void * nano::store::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

void nano::store::write_transaction::commit ()
{
	++current_epoch;
	impl->commit ();
}

void nano::store::write_transaction::renew ()
{
	++current_epoch;
	impl->renew ();
	start = std::chrono::steady_clock::now ();
}

void nano::store::write_transaction::refresh ()
{
	commit ();
	renew ();
}

void nano::store::write_transaction::refresh_if_needed (std::chrono::milliseconds max_age)
{
	auto now = std::chrono::steady_clock::now ();
	if (now - start > max_age)
	{
		refresh ();
	}
}

bool nano::store::write_transaction::contains (nano::tables table_a) const
{
	return impl->contains (table_a);
}
