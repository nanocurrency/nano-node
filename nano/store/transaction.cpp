#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/transaction.hpp>

nano::read_transaction::read_transaction (std::unique_ptr<nano::read_transaction_impl> read_transaction_impl) :
	impl (std::move (read_transaction_impl))
{
}

void * nano::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

void nano::read_transaction::reset () const
{
	impl->reset ();
}

void nano::read_transaction::renew () const
{
	impl->renew ();
}

void nano::read_transaction::refresh () const
{
	reset ();
	renew ();
}

nano::write_transaction::write_transaction (std::unique_ptr<nano::write_transaction_impl> write_transaction_impl) :
	impl (std::move (write_transaction_impl))
{
	/*
	 * For IO threads, we do not want them to block on creating write transactions.
	 */
	debug_assert (nano::thread_role::get () != nano::thread_role::name::io);
}

void * nano::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

void nano::write_transaction::commit ()
{
	impl->commit ();
}

void nano::write_transaction::renew ()
{
	impl->renew ();
}

void nano::write_transaction::refresh ()
{
	impl->commit ();
	impl->renew ();
}

bool nano::write_transaction::contains (nano::tables table_a) const
{
	return impl->contains (table_a);
}
