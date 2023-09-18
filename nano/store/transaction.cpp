#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/transaction.hpp>

nano::store::read_transaction::read_transaction (std::unique_ptr<store::read_transaction_impl> read_transaction_impl) :
	impl (std::move (read_transaction_impl))
{
}

void * nano::store::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

void nano::store::read_transaction::reset () const
{
	impl->reset ();
}

void nano::store::read_transaction::renew () const
{
	impl->renew ();
}

void nano::store::read_transaction::refresh () const
{
	reset ();
	renew ();
}

nano::store::write_transaction::write_transaction (std::unique_ptr<store::write_transaction_impl> write_transaction_impl) :
	impl (std::move (write_transaction_impl))
{
	/*
	 * For IO threads, we do not want them to block on creating write transactions.
	 */
	debug_assert (nano::thread_role::get () != nano::thread_role::name::io);
}

void * nano::store::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

void nano::store::write_transaction::commit ()
{
	impl->commit ();
}

void nano::store::write_transaction::renew ()
{
	impl->renew ();
}

void nano::store::write_transaction::refresh ()
{
	impl->commit ();
	impl->renew ();
}

bool nano::store::write_transaction::contains (nano::tables table_a) const
{
	return impl->contains (table_a);
}
