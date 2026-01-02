#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/ledger/final_vote.hpp>

namespace nano::store::ledger
{
final_vote_view::final_vote_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

bool final_vote_view::put (nano::store::write_transaction const & txn, nano::qualified_root const & root, nano::block_hash const & hash)
{
	nano::store::db_val value;
	auto status = backend.get (txn, nano::store::table::final_votes, root, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	bool result = true;
	if (backend.success (status))
	{
		result = static_cast<nano::block_hash> (value) == hash;
	}
	else
	{
		status = backend.put (txn, nano::store::table::final_votes, root, hash);
		backend.release_assert_success (status);
	}
	return result;
}

std::optional<nano::block_hash> final_vote_view::get (nano::store::transaction const & txn, nano::qualified_root const & qualified_root) const
{
	nano::store::db_val result;
	auto status = backend.get (txn, nano::store::table::final_votes, qualified_root, result);
	std::optional<nano::block_hash> final_vote_hash;
	if (backend.success (status))
	{
		final_vote_hash = static_cast<nano::block_hash> (result);
	}
	return final_vote_hash;
}

void final_vote_view::del (nano::store::write_transaction const & txn, nano::qualified_root const & root)
{
	auto status = backend.del (txn, nano::store::table::final_votes, root);
	backend.release_assert_success (status);
}

size_t final_vote_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::final_votes);
}

bool final_vote_view::empty (nano::store::transaction const & txn) const
{
	return backend.empty (txn, nano::store::table::final_votes);
}

void final_vote_view::clear ()
{
	auto status = backend.clear (nano::store::table::final_votes);
	backend.release_assert_success (status);
}

auto final_vote_view::begin (nano::store::transaction const & txn, nano::qualified_root const & root) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::final_votes, root) };
}

auto final_vote_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::final_votes) };
}

auto final_vote_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::final_votes) };
}

void final_vote_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint512_t> (
	[&action, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
