#include <nano/lib/threading.hpp>
#include <nano/secure/blockstore.hpp>

nano::representative_visitor::representative_visitor (nano::transaction const & transaction_a, nano::block_store & store_a) :
	transaction (transaction_a),
	store (store_a),
	result (0)
{
}

void nano::representative_visitor::compute (nano::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block (store.block_get (transaction, current));
		debug_assert (block != nullptr);
		block->visit (*this);
	}
}

void nano::representative_visitor::send_block (nano::send_block const & block_a)
{
	current = block_a.previous ();
}

void nano::representative_visitor::receive_block (nano::receive_block const & block_a)
{
	current = block_a.previous ();
}

void nano::representative_visitor::open_block (nano::open_block const & block_a)
{
	result = block_a.hash ();
}

void nano::representative_visitor::change_block (nano::change_block const & block_a)
{
	result = block_a.hash ();
}

void nano::representative_visitor::state_block (nano::state_block const & block_a)
{
	result = block_a.hash ();
}

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
