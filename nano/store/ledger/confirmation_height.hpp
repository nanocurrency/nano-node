#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/crawler.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

#include <functional>

namespace nano::store::ledger
{
class confirmation_height_view
{
public:
	using iterator = store::typed_iterator<nano::account, nano::confirmation_height_info>;

public:
	explicit confirmation_height_view (nano::store::backend &);

	void put (nano::store::write_transaction const &, nano::account const &, nano::confirmation_height_info const &);
	bool get (nano::store::transaction const &, nano::account const &, nano::confirmation_height_info &) const;
	std::optional<nano::confirmation_height_info> get (nano::store::transaction const &, nano::account const &);
	bool exists (nano::store::transaction const &, nano::account const &) const;
	void del (nano::store::write_transaction const &, nano::account const &);
	uint64_t count (nano::store::transaction const &) const;
	bool empty (nano::store::transaction const &) const;
	void clear ();
	iterator begin (nano::store::transaction const &, nano::account const &) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	void for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const;

	template <typename Transaction>
	auto crawl (Transaction & txn, nano::account const & start = { 0 }) const -> nano::store::crawler<confirmation_height_view, Transaction>
	{
		return nano::store::crawler<confirmation_height_view, Transaction>{ *this, txn, start };
	}

private:
	nano::store::backend & backend;
};
}
