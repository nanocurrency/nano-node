#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/store/transaction.hpp>

/*
 * transaction_impl
 */

celerix::store::transaction_impl::transaction_impl (celerix::id_dispenser::id_t const store_id_a) :
	store_id{ store_id_a }
{
	debug_assert (!celerix::thread_role::is_network_io (), "database operations are not allowed to run on network IO threads");
}

/*
 * read_transaction_impl
 */

celerix::store::read_transaction_impl::read_transaction_impl (celerix::id_dispenser::id_t const store_id_a) :
	transaction_impl (store_id_a)
{
}

/*
 * write_transaction_impl
 */

celerix::store::write_transaction_impl::write_transaction_impl (celerix::id_dispenser::id_t const store_id_a) :
	transaction_impl (store_id_a)
{
}

/*
 * transaction
 */

auto celerix::store::transaction::epoch () const -> epoch_t
{
	return current_epoch;
}

std::chrono::steady_clock::time_point celerix::store::transaction::timestamp () const
{
	return start;
}

/*
 * read_transaction
 */

celerix::store::read_transaction::read_transaction (std::unique_ptr<store::read_transaction_impl> read_transaction_impl) :
	impl (std::move (read_transaction_impl))
{
	start = std::chrono::steady_clock::now ();
}

void * celerix::store::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

celerix::id_dispenser::id_t celerix::store::read_transaction::store_id () const
{
	return impl->store_id;
}

void celerix::store::read_transaction::reset ()
{
	++current_epoch;
	impl->reset ();
}

void celerix::store::read_transaction::renew ()
{
	++current_epoch;
	impl->renew ();
	start = std::chrono::steady_clock::now ();
}

void celerix::store::read_transaction::refresh ()
{
	reset ();
	renew ();
}

bool celerix::store::read_transaction::refresh_if_needed (std::chrono::milliseconds max_age)
{
	auto now = std::chrono::steady_clock::now ();
	if (now - start > max_age)
	{
		refresh ();
		return true;
	}
	return false;
}

/*
 * write_transaction
 */

celerix::store::write_transaction::write_transaction (std::unique_ptr<store::write_transaction_impl> write_transaction_impl) :
	impl (std::move (write_transaction_impl))
{
	/*
	 * For IO threads, we do not want them to block on creating write transactions.
	 */
	debug_assert (celerix::thread_role::get () != celerix::thread_role::name::io);

	start = std::chrono::steady_clock::now ();
}

void * celerix::store::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

celerix::id_dispenser::id_t celerix::store::write_transaction::store_id () const
{
	return impl->store_id;
}

void celerix::store::write_transaction::commit ()
{
	++current_epoch;
	impl->commit ();
}

void celerix::store::write_transaction::renew ()
{
	++current_epoch;
	impl->renew ();
	start = std::chrono::steady_clock::now ();
}

void celerix::store::write_transaction::refresh ()
{
	commit ();
	renew ();
}

void celerix::store::write_transaction::refresh_if_needed (std::chrono::milliseconds max_age)
{
	auto now = std::chrono::steady_clock::now ();
	if (now - start > max_age)
	{
		refresh ();
	}
}

bool celerix::store::write_transaction::contains (celerix::tables table_a) const
{
	return impl->contains (table_a);
}
