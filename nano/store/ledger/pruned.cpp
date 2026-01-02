#include <nano/crypto_lib/random_pool.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/ledger/pruned.hpp>

namespace nano::store::ledger
{
pruned_view::pruned_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void pruned_view::put (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	auto status = backend.put (txn, nano::store::table::pruned, hash, nullptr);
	backend.release_assert_success (status);
}

void pruned_view::del (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	auto status = backend.del (txn, nano::store::table::pruned, hash);
	backend.release_assert_success (status);
}

bool pruned_view::exists (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	return backend.exists (txn, nano::store::table::pruned, hash);
}

nano::block_hash pruned_view::random (nano::store::transaction const & txn) const
{
	nano::block_hash random_hash;
	nano::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	auto existing = begin (txn, random_hash);
	if (existing == end (txn))
	{
		existing = begin (txn);
	}
	return existing != end (txn) ? existing->first : 0;
}

size_t pruned_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::pruned);
}

void pruned_view::clear ()
{
	auto status = backend.clear (nano::store::table::pruned);
	backend.release_assert_success (status);
}

auto pruned_view::begin (nano::store::transaction const & txn, nano::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::pruned, hash) };
}

auto pruned_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::pruned) };
}

auto pruned_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::pruned) };
}

void pruned_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint256_t> (
	[&action, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
